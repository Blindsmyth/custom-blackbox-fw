#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MasterProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < mc_param_count(); i++) {
        const McParamDef* d = mc_param(i);
        juce::ParameterID pid { d->id, 1 };
        float def = params::defaultValue(i);
        switch (d->kind) {
        case MC_KIND_TOGGLE:
            layout.add(std::make_unique<juce::AudioParameterBool>(pid, d->name, def > 0.5f));
            break;
        case MC_KIND_CHOICE:
            layout.add(std::make_unique<juce::AudioParameterChoice>(pid, d->name, params::choices(i), (int)def));
            break;
        default: {
            juce::NormalisableRange<float> r(d->mn, d->mx);
            if (d->kind == MC_KIND_LOG) r.setSkewForCentre(std::sqrt(d->mn * d->mx));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                pid, d->name, r, def, juce::AudioParameterFloatAttributes().withLabel(d->unit)));
        }
        }
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { params::demo, 1 }, "Demo loop", false));
    return layout;
}

MasterProcessor::MasterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", makeLayout())
{
    for (int i = 0; i < mc_param_count(); i++) raw.push_back(apvts.getRawParameterValue(params::id(i)));
    globalVals = capture();
    mc_init(&chain, 48000.f);
    mc_demo_init(&demo, 48000.f);
}

void MasterProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr;
    mc_init(&chain, (float)sr);
    mc_demo_init(&demo, (float)sr);
}

bool MasterProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float MasterProcessor::value(const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue(id)) return p->load();
    return 0.f;
}

McParams MasterProcessor::currentParams() const
{
    McParams mp;
    mc_default_params(&mp);
    for (int i = 0; i < (int)raw.size(); i++) mc_param_set(&mp, i, raw[(size_t)i]->load());
    return mp;
}

void MasterProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    if (buf.getNumChannels() < 2) return;
    float* L = buf.getWritePointer(0);
    float* R = buf.getWritePointer(1);

    float tempo = 120.f;   // standalone and hosts without a tempo: the demo loop's 120
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) tempo = (float)*b;
    bpmNow.store(tempo);

    if (value(params::demo) > 0.5f) mc_demo_fill(&demo, L, R, n);

    McParams mp = currentParams();
    mc_set_params(&chain, &mp);
    mc_set_bpm(&chain, tempo);
    mc_process(&chain, L, R, n);

    int lat = mp.chain_on > 0.5f ? mc_latency(&chain) : 0;
    if (lat != lastLatency) { setLatencySamples(lat); lastLatency = lat; }

    const McMeters& m = chain.m;
    for (int b = 0; b < MC_BANDS; b++) { meters.eq[b].store(m.eq_dyn_db[b]); meters.eqDet[b].store(m.eq_det_db[b]); }
    meters.clrClip.store(m.clr_clip);
    meters.cmpGr.store(m.cmp_gr_db);
    meters.compIn.store(m.comp_in_db);
    meters.maxGr.store(m.max_gr_db);
    meters.maxOver.store(m.max_over_db);
    meters.limGr.store(m.lim_gr_db);
    float held = meters.outPeak.load() - 20.f * (float)n / (float)sampleRate;   // 20 dB/s fall
    meters.outPeak.store(juce::jmax(m.out_peak_db, held));
}

// ---- values: global or per preset ------------------------------------------------------------

MasterProcessor::Vals MasterProcessor::capture() const
{
    Vals v(raw.size());
    for (size_t i = 0; i < raw.size(); i++) v[i] = raw[i]->load();
    return v;
}

void MasterProcessor::apply(const Vals& v)
{
    for (int i = 0; i < mc_param_count() && i < (int)v.size(); i++)
        if (auto* p = apvts.getParameter(params::id(i))) p->setValueNotifyingHost(p->convertTo0to1(v[(size_t)i]));
}

void MasterProcessor::setValuesPerPreset(bool on)
{
    if (on == perPreset) return;
    if (on) {
        globalVals = capture();
        if (!hasVals[(size_t)active]) { presetVals[(size_t)active] = globalVals; hasVals[(size_t)active] = true; }
        perPreset = true;
        apply(presetVals[(size_t)active]);
    } else {
        presetVals[(size_t)active] = capture();
        perPreset = false;
        apply(globalVals);
    }
}

void MasterProcessor::selectPreset(int i)
{
    i = juce::jlimit(0, kPresets - 1, i);
    if (i == active) return;
    if (perPreset) {
        presetVals[(size_t)active] = capture();
        if (!hasVals[(size_t)i]) { presetVals[(size_t)i] = globalVals; hasVals[(size_t)i] = true; }
        active = i;
        apply(presetVals[(size_t)i]);
    } else {
        active = i;   // global values: the chain sounds the same on every preset
    }
}

// ---- state -----------------------------------------------------------------------------------

juce::String MasterProcessor::encode(const Vals& v)
{
    juce::StringArray s;
    for (float f : v) s.add(juce::String(f, 6));
    return s.joinIntoString(",");
}

bool MasterProcessor::decode(const juce::String& str, Vals& v)
{
    juce::StringArray s;
    s.addTokens(str, ",", "");
    if (s.size() != mc_param_count()) return false;
    v.resize((size_t)s.size());
    for (int k = 0; k < s.size(); k++) v[(size_t)k] = s[k].getFloatValue();
    return true;
}

void MasterProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (perPreset) presetVals[(size_t)active] = capture();
    else globalVals = capture();
    std::unique_ptr<juce::XmlElement> xml(apvts.copyState().createXml());
    xml->setAttribute("activePreset", active);
    xml->setAttribute("perPreset", perPreset);
    xml->createNewChildElement("GLOBALVALS")->setAttribute("v", encode(globalVals));
    for (int p = 0; p < kPresets; p++) {
        if (!hasVals[(size_t)p]) continue;
        auto* e = xml->createNewChildElement("PRESETVALS");
        e->setAttribute("i", p);
        e->setAttribute("v", encode(presetVals[(size_t)p]));
    }
    copyXmlToBinary(*xml, dest);
}

void MasterProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml) return;
    if (auto* g = xml->getChildByName("GLOBALVALS")) decode(g->getStringAttribute("v"), globalVals);
    hasVals.fill(false);
    for (auto* e : xml->getChildWithTagNameIterator("PRESETVALS")) {
        int p = e->getIntAttribute("i", -1);
        if (p >= 0 && p < kPresets) hasVals[(size_t)p] = decode(e->getStringAttribute("v"), presetVals[(size_t)p]);
    }
    active = juce::jlimit(0, kPresets - 1, xml->getIntAttribute("activePreset", 0));
    perPreset = xml->getBoolAttribute("perPreset", false);
    xml->deleteAllChildElementsWithTagName("GLOBALVALS");
    xml->deleteAllChildElementsWithTagName("PRESETVALS");
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MasterProcessor::createEditor() { return new MasterEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterProcessor(); }
