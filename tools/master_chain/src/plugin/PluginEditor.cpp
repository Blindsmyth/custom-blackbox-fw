#include "PluginEditor.h"
#include <cmath>

namespace {
const juce::Colour kBg(0xff141414), kEdge(0xff56565a), kWhite(0xffffffff), kPink(0xffff6af3),
    kCyan(0xff09d7f5), kTick(0xff444444), kTrack(0xff707070), kDead(0xff2b2b2b), kStrip(0xff1c1c1c);

const char* kTabs[] = { "DLY", "RVB", "AUX", "EQ", "CLR", "CMP", "MAX" };
constexpr float kTabY = 206.f, kGridY = 60.f, kCellW = 80.f, kCellH = 70.f;
constexpr float kPi = 3.14159265f;

float clamp01(float x) { return juce::jlimit(0.f, 1.f, x); }
} // namespace

ScreenComponent::ScreenComponent(MasterProcessor& p) : proc(p)
{
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

void ScreenComponent::resetHolds()
{
    cmpHold = maxHold = 0.f;
    eqHold.fill(0.f);
}

bool ScreenComponent::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::backspaceKey) { resetHolds(); return true; }
    return false;
}

// ---- parameters ------------------------------------------------------------------------------

float ScreenComponent::norm(const juce::String& id) const
{
    if (auto* p = proc.apvts.getParameter(id)) return p->convertTo0to1(get(id));
    return 0.f;
}

void ScreenComponent::setNorm(const juce::String& id, float n)
{
    if (auto* p = proc.apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(clamp01(n));
        p->endChangeGesture();
    }
}

void ScreenComponent::setValue(const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter(id)) setNorm(id, p->convertTo0to1(v));
}

juce::String ScreenComponent::valueText(const juce::String& id) const
{
    int i = params::find(id);
    if (i < 0) return {};
    const McParamDef* d = mc_param(i);
    float v = get(id);
    if (d->kind == MC_KIND_CHOICE) return params::choices(i)[juce::jlimit(0, params::choices(i).size() - 1, (int)std::round(v))];
    if (d->kind == MC_KIND_TOGGLE) return v > 0.5f ? "ON" : "OFF";
    if (id == "max_amount") return "+" + juce::String(12.f * v, 1) + "dB";
    if (id.endsWith("_freq")) return v >= 1000.f ? juce::String(v / 1000.f, 1) + "k" : juce::String((int)v);
    juce::String unit(d->unit);
    if (unit == "dB") return juce::String(v, 1) + "dB";
    if (unit == "ms") return juce::String(v < 10.f ? juce::String(v, 1) : juce::String((int)std::round(v))) + "ms";
    if (unit == ":1") return juce::String(v, 1) + ":1";
    return juce::String(v, 2);
}

std::array<ScreenComponent::Cell, 8> ScreenComponent::cells() const
{
    std::array<Cell, 8> c {};
    switch (page) {
    case Clr:   // side 0: Sidepass Glue Scope Girth, side 1: Skronk Drive (column-major like stock)
        c = { { { "clr_sidepass" }, { "clr_glue" }, { "clr_scope" }, { "clr_girth" },
                { "clr_skronk" }, { "clr_drive" }, {}, {} } };
        break;
    case Cmp:
        if (get("cmp_type") < 0.5f)
            c = { { { "cmp_type" }, { "sq_squish" }, { "sq_bassbloom" }, { "sq_output" }, { "sq_mix" }, {}, {}, {} } };
        else
            c = { { { "cmp_type" }, { "c_thresh" }, { "c_ratio" }, { "c_atk" }, { "c_rel" }, { "c_gain" }, {}, {} } };
        break;
    case Max:
        c = { { { "max_amount" }, { "max_relx" }, { "chain_on" }, { "", ValuesButton }, {}, {}, {}, {} } };
        break;
    default:
        break;
    }
    return c;
}

juce::Rectangle<float> ScreenComponent::cellRect(int i) const
{
    return { kCellW * (float)(i / 2), kGridY + kCellH * (float)(i % 2), kCellW, kCellH };
}

int ScreenComponent::cellAt(juce::Point<float> p) const
{
    for (int i = 0; i < 8; i++)
        if (cellRect(i).contains(p)) return i;
    return -1;
}

bool ScreenComponent::isButton(const Cell& c) const
{
    if (c.special == ValuesButton) return true;
    int i = params::find(c.id);
    return i >= 0 && mc_param(i)->kind == MC_KIND_TOGGLE;
}

void ScreenComponent::press(const Cell& c)
{
    if (c.special == ValuesButton) proc.setValuesPerPreset(!proc.valuesPerPreset());
    else setValue(c.id, get(c.id) > 0.5f ? 0.f : 1.f);
}

void ScreenComponent::text(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r,
                           juce::Justification j, juce::Colour c, float size)
{
    g.setColour(c);
    g.setFont(juce::Font(juce::FontOptions(size)));
    g.drawText(s, r, j, false);
}

// ---- timer -----------------------------------------------------------------------------------

void ScreenComponent::timerCallback()
{
    auto push = [](std::array<float, kHist>& h, float v) {
        std::copy(h.begin() + 1, h.end(), h.begin());
        h.back() = v;
    };
    bool chain = get("chain_on") > 0.5f;
    float cg = proc.meters.cmpGr.load(), mg = chain ? proc.meters.maxGr.load() : proc.meters.limGr.load();
    push(cmpHist, cg);
    push(maxHist, mg);
    cmpHold = juce::jmin(cmpHold, cg);
    maxHold = juce::jmin(maxHold, mg);
    for (int b = 0; b < MC_BANDS; b++) {
        float d = proc.meters.eq[b].load();
        if (std::abs(d) > std::abs(eqHold[(size_t)b])) eqHold[(size_t)b] = d;
        // short dips stay readable: jump to a deeper value, fall back over ~0.5 s
        float& s = eqShown[(size_t)b];
        s = std::abs(d) >= std::abs(s) ? d : s * 0.85f;
    }
    clipFlash = proc.meters.clrClip.load() > 0.5f ? 1.f : clipFlash * 0.85f;
    repaint();
}

// ---- painting --------------------------------------------------------------------------------

void ScreenComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.addTransform(juce::AffineTransform::scale(scale()));
    g.reduceClipRegion(0, 0, 320, 240);
    g.fillAll(kBg);
    drawTop(g);
    if (page == Eq) drawEqPage(g);
    else { drawHeader(g); drawGrid(g); }
    drawTabs(g);
}

void ScreenComponent::drawTop(juce::Graphics& g)
{
    g.setColour(kStrip);
    g.fillRect(0.f, 0.f, 320.f, 14.f);
    for (int i = 0; i < MasterProcessor::kPresets; i++) {
        juce::Rectangle<float> r(4.f + 17.f * (float)i, 2.f, 15.f, 10.f);
        bool act = i == proc.activePreset();
        g.setColour(act ? kPink : kDead);
        g.fillRect(r);
        text(g, "P" + juce::String(i + 1), r, juce::Justification::centred, act ? kBg : kTrack, 6.f);
    }
    text(g, proc.valuesPerPreset() ? "PRESET" : "GLOBAL", { 142, 0, 34, 14 },
         juce::Justification::centredLeft, proc.valuesPerPreset() ? kPink : kTrack, 6.f);
    g.setColour(kDead);
    g.fillRect(176.f, 2.f, 32.f, 10.f);
    text(g, "BACK", { 176, 2, 32, 10 }, juce::Justification::centred, kWhite, 6.f);

    auto bar = [&](const char* label, float x, float gr) {
        text(g, label, { x, 0, 8, 14 }, juce::Justification::centredLeft, kTrack, 6.f);
        juce::Rectangle<float> r(x + 8.f, 4.f, 22.f, 6.f);
        g.setColour(kDead); g.fillRect(r);
        g.setColour(kCyan); g.fillRect(r.withWidth(r.getWidth() * clamp01(-gr / 12.f)));
    };
    bool on = get("chain_on") > 0.5f;
    bar("C", 214.f, proc.meters.cmpGr.load());
    bar("M", 246.f, on ? proc.meters.maxGr.load() : proc.meters.limGr.load());

    bool demo = get(params::demo) > 0.5f;
    g.setColour(demo ? kPink : kDead);
    g.fillRect(282.f, 2.f, 34.f, 10.f);
    text(g, "DEMO", { 282, 2, 34, 10 }, juce::Justification::centred, demo ? kBg : kTrack, 6.f);
}

void ScreenComponent::drawTabs(juce::Graphics& g)
{
    float w = 320.f / (float)kPages;
    bool chain = get("chain_on") > 0.5f;
    for (int i = 0; i < kPages; i++) {
        juce::Rectangle<float> r(w * (float)i, kTabY, w, 240.f - kTabY);
        bool aux = i == Aux;
        g.setColour(i == page ? kPink : kDead);
        g.fillRect(r.reduced(1.f));
        text(g, kTabs[i], r, juce::Justification::centred, i == page ? kBg : (aux ? kTick : kWhite), 9.f);
        bool dot = (i == Eq && get("eq_on") > 0.5f) || ((i == Clr || i == Cmp || i == Max) && chain);
        if (i == Eq || i == Clr || i == Cmp || i == Max) {
            g.setColour(dot ? kCyan : kTick);
            g.fillEllipse(r.getRight() - 8.f, r.getY() + 3.f, 4.f, 4.f);
        }
    }
}

void ScreenComponent::drawGrStrip(juce::Graphics& g, const juce::String& label, float gr, float hold,
                                  const std::array<float, kHist>& hist)
{
    text(g, label + "  " + juce::String(gr, 1) + " dB", { 4, 16, 120, 12 }, juce::Justification::centredLeft, kPink, 8.f);
    text(g, "MAX " + juce::String(hold, 1) + " dB", { 120, 16, 64, 12 }, juce::Justification::centredRight, kWhite, 7.f);
    juce::Rectangle<float> bar(4.f, 31.f, 180.f, 10.f);
    g.setColour(kDead); g.fillRect(bar);
    g.setColour(kPink); g.fillRect(bar.withWidth(bar.getWidth() * clamp01(-gr / 12.f)));
    g.setColour(kWhite); g.fillRect(bar.getX() + bar.getWidth() * clamp01(-hold / 12.f) - 1.f, bar.getY() - 2.f, 2.f, bar.getHeight() + 4.f);
    g.setColour(kTick);
    for (int d = 3; d < 12; d += 3) g.fillRect(bar.getX() + bar.getWidth() * (float)d / 12.f, bar.getY(), 1.f, bar.getHeight());
    g.setColour(kEdge); g.drawRect(bar, 1.f);
    text(g, "0", { 4, 42, 20, 8 }, juce::Justification::centredLeft, kTrack, 6.f);
    text(g, "-12", { 164, 42, 20, 8 }, juce::Justification::centredRight, kTrack, 6.f);

    juce::Rectangle<float> h(192.f, 16.f, 124.f, 40.f);
    g.setColour(kDead); g.fillRect(h);
    juce::Path p;
    for (int i = 0; i < kHist; i++) {
        float x = h.getX() + h.getWidth() * (float)i / (float)(kHist - 1);
        float y = h.getY() + h.getHeight() * clamp01(-hist[(size_t)i] / 12.f);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    {
        juce::Graphics::ScopedSaveState keep(g);
        g.reduceClipRegion(h.toNearestInt());
        g.setColour(kPink); g.strokePath(p, juce::PathStrokeType(1.2f));
    }
    g.setColour(kEdge); g.drawRect(h, 1.f);
}

std::array<juce::String, 4> ScreenComponent::eqFaders() const
{
    juce::String b = "b" + juce::String(band + 1) + "_";
    bool cut = mc_band_is_cut((int)std::round(get(b + "type")));
    if (!eqInfo) return { b + "freq", cut ? juce::String() : b + "gain", b + "q", b + "type" };
    if (cut) return {};                                    // cut bands have no dynamics
    return { b + "thresh", b + "range", b + "atk", b + "rel" };
}

void ScreenComponent::drawEqPage(juce::Graphics& g)
{
    static const char* labels[2][4] = { { "FREQ", "GAIN", "Q", "TYPE" }, { "THRSH", "RANGE", "ATK", "REL" } };
    static const char* typeShort[] = { "LC", "LS", "PK", "HS", "HC" };
    static const float faderX[4] = { 4.f, 28.f, 272.f, 296.f };
    McParams cur = proc.currentParams();
    const McBand& sel = cur.band[band];
    auto typeOf = [&](int b) { return juce::jlimit(0, 4, (int)std::round(cur.band[b].type)); };
    bool selCut = mc_band_is_cut(typeOf(band)), selOn = sel.on > 0.5f;

    // corner faders: the static controls, or the dynamic ones with INFO
    auto ids = eqFaders();
    for (int i = 0; i < 4; i++) {
        if (ids[(size_t)i].isEmpty()) continue;
        float cx = faderX[i] + 10.f, n = norm(ids[(size_t)i]), yh = 32.f + 150.f * (1.f - n);
        text(g, labels[eqInfo][i], { faderX[i] - 3.f, 19.f, 26.f, 11.f }, juce::Justification::centred, eqInfo ? kPink : kWhite, 7.f);
        g.setColour(kTrack); g.fillRect(cx - 1.f, 32.f, 2.f, 150.f);
        g.setColour(eqInfo ? kPink : kCyan); g.fillRect(cx - 1.f, yh, 2.f, 182.f - yh);
        g.setColour(i == dragFader ? kWhite : kPink); g.fillRect(cx - 9.f, yh - 3.f, 18.f, 6.f);
        juce::String v = ids[(size_t)i].endsWith("_type") ? juce::String(typeShort[typeOf(band)])
                                                          : valueText(ids[(size_t)i]).upToFirstOccurrenceOf("d", false, false)
                                                                .upToFirstOccurrenceOf("m", false, false);
        text(g, v, { faderX[i] - 3.f, 184.f, 26.f, 12.f }, juce::Justification::centred, kWhite, 7.f);
    }

    // header: EQ on | B1..B5 | band on | INFO
    bool eqOn = cur.eq_on > 0.5f;
    g.setColour(eqOn ? kCyan : kDead); g.fillRect(56.f, 18.f, 26.f, 13.f);
    text(g, "EQ", { 56, 18, 26, 13 }, juce::Justification::centred, eqOn ? kBg : kTrack, 8.f);
    for (int b = 0; b < MC_BANDS; b++) {
        juce::Rectangle<float> r(86.f + 24.f * (float)b, 18.f, 22.f, 13.f);
        bool on = cur.band[b].on > 0.5f;
        if (b == band && on) { g.setColour(kPink); g.fillRect(r); }
        else { g.setColour(kDead); g.fillRect(r); if (b == band) { g.setColour(kPink); g.drawRect(r, 1.f); } }
        text(g, "B" + juce::String(b + 1), r, juce::Justification::centred,
             b == band && on ? kBg : (on ? kWhite : kTick), 8.f);
    }
    g.setColour(selOn ? kCyan : kDead); g.fillRect(208.f, 18.f, 24.f, 13.f);
    text(g, selOn ? "ON" : "OFF", { 208, 18, 24, 13 }, juce::Justification::centred, selOn ? kBg : kTrack, 7.f);
    g.setColour(eqInfo ? kPink : kDead); g.fillRect(235.f, 18.f, 29.f, 13.f);
    text(g, "INFO", { 235, 18, 29, 13 }, juce::Justification::centred, eqInfo ? kBg : kWhite, 7.f);

    // graph background
    auto G = eqGraph();
    g.setColour(kDead); g.fillRect(G);
    g.setColour(kTick);
    for (float hz : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f }) g.fillRect(eqX(hz), G.getY(), 1.f, G.getHeight());
    for (float db : { -12.f, -6.f, 6.f, 12.f }) g.fillRect(G.getX(), eqY(db), G.getWidth(), 1.f);
    g.setColour(kEdge); g.fillRect(G.getX(), eqY(0.f), G.getWidth(), 1.f);
    for (auto [hz, s] : { std::pair<float, const char*> { 100.f, "100" }, { 1000.f, "1k" }, { 10000.f, "10k" } })
        text(g, s, { eqX(hz) - 8.f, G.getBottom() - 8.f, 16.f, 8.f }, juce::Justification::centred, kTrack, 6.f);

    // Each band evaluated on its own: static, live (with its current dynamic gain), the end of its
    // range, and its deepest point since BACK. The EQ curve is the meter, as in Pro-Q / Ozone.
    const int cols = (int)G.getWidth() / 2 + 1;
    std::vector<float> xs((size_t)cols), hz((size_t)cols), totStat((size_t)cols, 0.f), totLive((size_t)cols, 0.f);
    for (int i = 0; i < cols; i++) {
        xs[(size_t)i] = G.getX() + 2.f * (float)i;
        hz[(size_t)i] = 20.f * std::pow(10.f, 3.f * (2.f * (float)i) / G.getWidth());
    }
    auto curve = [&](int b, float gainDb, std::vector<float>& out) {
        McCoef k;
        mc_bq_design(mc_band_filter(typeOf(b)), 48000.f, cur.band[b].freq, cur.band[b].q, gainDb, &k);
        out.resize((size_t)cols);
        for (int i = 0; i < cols; i++) out[(size_t)i] = mc_bq_mag_db(&k, hz[(size_t)i], 48000.f);
    };
    auto yOf = [&](float db) { return juce::jlimit(G.getY(), G.getBottom(), eqY(db)); };
    auto band_area = [&](const std::vector<float>& a, const std::vector<float>& b) {
        juce::Path p;
        p.startNewSubPath(xs[0], yOf(a[0]));
        for (int i = 1; i < cols; i++) p.lineTo(xs[(size_t)i], yOf(a[(size_t)i]));
        for (int i = cols - 1; i >= 0; i--) p.lineTo(xs[(size_t)i], yOf(b[(size_t)i]));
        p.closeSubPath();
        return p;
    };
    auto line = [&](const std::vector<float>& a) {
        juce::Path p;
        p.startNewSubPath(xs[0], yOf(a[0]));
        for (int i = 1; i < cols; i++) p.lineTo(xs[(size_t)i], yOf(a[(size_t)i]));
        return p;
    };
    std::vector<float> zero((size_t)cols, 0.f), st, lv, rg, hd;
    float dyn[MC_BANDS] {};
    for (int b = 0; b < MC_BANDS; b++) {
        const McBand& B = cur.band[b];
        if (B.on < 0.5f) continue;
        bool cut = mc_band_is_cut(typeOf(b)), isSel = b == band;
        dyn[b] = cut ? 0.f : eqShown[(size_t)b];
        curve(b, cut ? 0.f : B.gain_db, st);
        curve(b, cut ? 0.f : B.gain_db + dyn[b], lv);
        for (int i = 0; i < cols; i++) { totStat[(size_t)i] += st[(size_t)i]; totLive[(size_t)i] += lv[(size_t)i]; }
        if (isSel) { g.setColour(kCyan.withAlpha(0.10f)); g.fillPath(band_area(zero, st)); }   // selected band's own shape
        if (cut || std::abs(B.range_db) < 0.05f) continue;
        curve(b, B.gain_db + B.range_db, rg);                                                // how far it may move
        g.setColour(kPink.withAlpha(isSel ? 0.16f : 0.08f)); g.fillPath(band_area(st, rg));
        g.setColour(kPink.withAlpha(0.65f)); g.fillPath(band_area(st, lv));                   // how far it moves now
        float hold = eqHold[(size_t)b];
        if (std::abs(hold) > 0.05f) {
            curve(b, B.gain_db + hold, hd);                                                  // deepest since BACK
            g.setColour(kPink.withAlpha(0.9f)); g.strokePath(line(hd), juce::PathStrokeType(0.8f));
        }
    }
    g.setColour(kTrack); g.strokePath(line(totStat), juce::PathStrokeType(1.f));
    g.setColour(eqOn ? kCyan : kTrack); g.strokePath(line(totLive), juce::PathStrokeType(1.6f));

    // INFO: threshold readout and a level meter beside the THRSH fader
    if (eqInfo && !selCut) {
        float det = proc.meters.eqDet[band].load();
        float n = clamp01((det + 60.f) / 60.f), top = 32.f + 150.f * (1.f - n);
        g.setColour(kDead); g.fillRect(faderX[0] + 16.f, 32.f, 3.f, 150.f);
        g.setColour(det > sel.thresh_db ? kPink : kCyan); g.fillRect(faderX[0] + 16.f, top, 3.f, 182.f - top);
    }

    // nodes: at the live position; a ring marks the static position while the band moves
    for (int b = 0; b < MC_BANDS; b++) {
        const McBand& B = cur.band[b];
        bool on = B.on > 0.5f, cut = mc_band_is_cut(typeOf(b));
        float x = eqX(B.freq);
        float ys = juce::jlimit(G.getY() + 4.f, G.getBottom() - 4.f, eqY(cut ? 0.f : B.gain_db));
        float yl = juce::jlimit(G.getY() + 4.f, G.getBottom() - 4.f, eqY(cut ? 0.f : B.gain_db + dyn[b]));
        juce::Colour c = b == band ? kPink : kCyan;
        if (!on) {
            g.setColour(kTick); g.drawEllipse(x - 4.f, ys - 4.f, 8.f, 8.f, 1.f);
            text(g, juce::String(b + 1), { x - 4.f, ys - 4.f, 8.f, 8.f }, juce::Justification::centred, kTrack, 6.f);
            continue;
        }
        if (std::abs(dyn[b]) > 0.3f) { g.setColour(c.withAlpha(0.7f)); g.drawEllipse(x - 4.f, ys - 4.f, 8.f, 8.f, 1.f); }
        g.setColour(c); g.fillEllipse(x - 4.f, yl - 4.f, 8.f, 8.f);
        text(g, juce::String(b + 1), { x - 4.f, yl - 4.f, 8.f, 8.f }, juce::Justification::centred, kBg, 6.f);
    }

    // one readout line for the selected band
    juce::String info = "B" + juce::String(band + 1) + " " + typeShort[typeOf(band)] + " " + valueText("b" + juce::String(band + 1) + "_freq");
    if (!selOn) info += "  OFF";
    else if (selCut) info += "  cut, no dynamics";
    else if (eqInfo) {
        float det = proc.meters.eqDet[band].load();
        info += "  level " + (det > -100.f ? juce::String(det, 1) : juce::String("-inf")) + "  thr " + juce::String(sel.thresh_db, 1);
    } else if (std::abs(sel.range_db) >= 0.05f)
        info += "  dyn " + juce::String(std::abs(dyn[band]) < 0.05f ? 0.f : dyn[band], 1) + "  max " + juce::String(eqHold[(size_t)band], 1) + " dB";
    else info += "  static (set RANGE under INFO)";
    text(g, info, G.reduced(5.f, 3.f).withHeight(9.f), juce::Justification::centredLeft, kWhite, 7.f);
    g.setColour(kEdge); g.drawRect(G, 1.f);
}

bool ScreenComponent::eqMouseDown(juce::Point<float> p)
{
    static const float faderX[4] = { 4.f, 28.f, 272.f, 296.f };
    if (p.y >= 18.f && p.y < 31.f) {
        if (juce::Rectangle<float>(56.f, 18.f, 26.f, 13.f).contains(p)) { setValue("eq_on", get("eq_on") > 0.5f ? 0.f : 1.f); return true; }
        for (int b = 0; b < MC_BANDS; b++)
            if (juce::Rectangle<float>(86.f + 24.f * (float)b, 18.f, 22.f, 13.f).contains(p)) { band = b; return true; }
        if (juce::Rectangle<float>(208.f, 18.f, 24.f, 13.f).contains(p)) {
            juce::String id = "b" + juce::String(band + 1) + "_on";
            setValue(id, get(id) > 0.5f ? 0.f : 1.f);
            return true;
        }
        if (juce::Rectangle<float>(235.f, 18.f, 29.f, 13.f).contains(p)) { eqInfo = 1 - eqInfo; return true; }
    }
    auto ids = eqFaders();
    for (int i = 0; i < 4; i++)
        if (ids[(size_t)i].isNotEmpty() && juce::Rectangle<float>(faderX[i], 19.f, 20.f, 181.f).contains(p)) {
            dragFader = i;
            setNorm(ids[(size_t)i], 1.f - (p.y - 32.f) / 150.f);
            return true;
        }
    if (eqGraph().contains(p)) {
        McParams cur = proc.currentParams();
        float best = 14.f;
        for (int b = 0; b < MC_BANDS; b++) {
            bool cut = mc_band_is_cut((int)std::round(cur.band[b].type));
            float y = eqY(cut ? 0.f : cur.band[b].gain_db + (cut ? 0.f : eqShown[(size_t)b]));
            float d = std::hypot(p.x - eqX(cur.band[b].freq), p.y - y);
            if (d < best) { best = d; dragBand = b; }
        }
        if (dragBand >= 0) band = dragBand;
        return true;
    }
    return false;
}

void ScreenComponent::drawHeader(juce::Graphics& g)
{
    bool chain = get("chain_on") > 0.5f;
    switch (page) {
    case Dly:
    case Rvb:
        text(g, page == Dly ? "A  DELAY RETURN" : "B  REVERB RETURN", { 4, 18, 312, 14 }, juce::Justification::centredLeft, kWhite, 9.f);
        text(g, "Stock return page; not simulated in this mockup.", { 4, 36, 312, 12 }, juce::Justification::centredLeft, kTrack, 7.f);
        break;
    case Clr: {
        text(g, "CLR  Mastering2", { 4, 16, 200, 14 }, juce::Justification::centredLeft, kWhite, 9.f);
        text(g, "left: Sidepass Glue Scope Girth   right: Skronk Drive", { 4, 32, 260, 12 },
             juce::Justification::centredLeft, kTrack, 7.f);
        juce::Rectangle<float> lamp(272.f, 20.f, 44.f, 16.f);
        g.setColour(kDead); g.fillRect(lamp);
        g.setColour(kPink.withAlpha(clamp01(clipFlash))); g.fillRect(lamp);
        g.setColour(kEdge); g.drawRect(lamp, 1.f);
        text(g, "CLIP", lamp, juce::Justification::centred, clipFlash > 0.5f ? kBg : kWhite, 8.f);
        break;
    }
    case Cmp:
        drawGrStrip(g, get("cmp_type") < 0.5f ? "SQUISH GR" : "STOCK GR", proc.meters.cmpGr.load(), cmpHold, cmpHist);
        break;
    case Max: {
        drawGrStrip(g, chain ? "MAX GR" : "STOCK LIM GR", chain ? proc.meters.maxGr.load() : proc.meters.limGr.load(), maxHold, maxHist);
        float bpm = proc.bpm();
        float rel = juce::jlimit(80.f, 600.f, 30000.f / bpm) * (get("max_relx") < 0.5f ? 0.5f : (get("max_relx") > 1.5f ? 2.f : 1.f));
        if (chain)
            text(g, "CEIL -0.3dB  REL " + juce::String((int)rel) + "ms @" + juce::String((int)std::round(bpm)) + "bpm"
                        + (proc.meters.maxOver.load() > 0.01f ? "  OVER!" : ""),
                 { 4, 48, 186, 10 }, juce::Justification::centredLeft, kTrack, 6.f);
        break;
    }
    default:
        break;
    }
    if (!chain && (page == Clr || page == Cmp || page == Max))
        text(g, "CHAIN OFF: no master FX, safety limiter only", { 4, 48, 186, 10 }, juce::Justification::centredLeft, kPink, 6.f);
}

void ScreenComponent::drawKnob(juce::Graphics& g, juce::Rectangle<float> r, const Cell& c, bool activeSide)
{
    if (c.id.isEmpty() && c.special == None) return;
    if (isButton(c)) {
        bool on = c.special == ValuesButton ? proc.valuesPerPreset() : get(c.id) > 0.5f;
        juce::String title = c.special == ValuesButton ? (on ? "PRESET" : "GLOBAL")
                                                       : juce::String(mc_param(params::find(c.id))->name).toUpperCase();
        if (c.id == "chain_on") title = on ? "CHAIN ON" : "CHAIN OFF";
        auto b = r.reduced(10.f, 18.f);
        g.setColour(on ? kCyan : kDead); g.fillRoundedRectangle(b, 3.f);
        g.setColour(activeSide ? kEdge : kTick); g.drawRoundedRectangle(b, 3.f, 1.f);
        text(g, title, b, juce::Justification::centred, on ? kBg : kWhite, 8.f);
        if (c.special == ValuesButton)
            text(g, "values", { r.getX(), r.getBottom() - 14.f, r.getWidth(), 10.f }, juce::Justification::centred, kTrack, 6.f);
        return;
    }
    int i = params::find(c.id);
    text(g, juce::String(mc_param(i)->name).toUpperCase(), { r.getX(), r.getY() + 3.f, r.getWidth(), 10.f },
         juce::Justification::centred, activeSide ? kWhite : kTrack, 7.f);
    float cx = r.getCentreX(), cy = r.getY() + 36.f, rad = 17.f;
    float a0 = kPi * 1.25f, a1 = kPi * 2.75f, n = norm(c.id), av = a0 + (a1 - a0) * n;
    juce::Path track, val;
    track.addCentredArc(cx, cy, rad, rad, 0.f, a0, a1, true);
    val.addCentredArc(cx, cy, rad, rad, 0.f, a0, av, true);
    g.setColour(kDead); g.strokePath(track, juce::PathStrokeType(4.f));
    g.setColour(activeSide ? kCyan : kTrack); g.strokePath(val, juce::PathStrokeType(4.f));
    g.setColour(kWhite);
    g.drawLine(cx, cy, cx + std::sin(av) * (rad - 6.f), cy - std::cos(av) * (rad - 6.f), 1.5f);
    text(g, valueText(c.id), { r.getX(), r.getY() + 56.f, r.getWidth(), 11.f }, juce::Justification::centred,
         activeSide ? kWhite : kTrack, 7.f);
}

void ScreenComponent::drawGrid(juce::Graphics& g)
{
    auto c = cells();
    bool any = false;
    for (auto& x : c) any |= x.id.isNotEmpty() || x.special != None;
    if (!any) return;
    bool right = false;
    for (int k = 4; k < 8; k++) right |= c[(size_t)k].id.isNotEmpty() || c[(size_t)k].special != None;
    if (!right) side = 0;
    juce::Rectangle<float> frame(160.f * (float)side, kGridY, 160.f, 2.f * kCellH);
    g.setColour(kEdge); g.drawRect(frame.reduced(1.f), 1.f);
    for (int k = 0; k < 8; k++) drawKnob(g, cellRect(k), c[(size_t)k], k / 4 == side);
}

// ---- mouse -----------------------------------------------------------------------------------

void ScreenComponent::mouseDown(const juce::MouseEvent& e)
{
    auto p = e.position / scale();
    dragCell = dragFader = dragBand = -1;
    dragMoved = false;

    if (p.y >= kTabY) {
        int t = juce::jlimit(0, kPages - 1, (int)(p.x / (320.f / (float)kPages)));
        if (t != Aux) { page = t; side = 0; }
        return;
    }
    if (p.y < 14.f) {
        if (p.x >= 282.f) setValue(params::demo, get(params::demo) > 0.5f ? 0.f : 1.f);
        else if (p.x >= 176.f && p.x < 208.f) resetHolds();
        else if (p.x >= 4.f && p.x < 4.f + 17.f * MasterProcessor::kPresets) proc.selectPreset((int)((p.x - 4.f) / 17.f));
        return;
    }
    if (page == Eq) { eqMouseDown(p); return; }
    int k = cellAt(p);
    if (k < 0) return;
    auto c = cells()[(size_t)k];
    if (c.id.isEmpty() && c.special == None) return;
    side = k / 4;   // touching a knob makes its side active, as on the device
    if (isButton(c)) { press(c); return; }
    dragCell = k;
    dragStartY = p.y;
    dragStartNorm = norm(c.id);
}

void ScreenComponent::mouseDrag(const juce::MouseEvent& e)
{
    auto p = e.position / scale();
    if (dragFader >= 0) { setNorm(eqFaders()[(size_t)dragFader], 1.f - (p.y - 32.f) / 150.f); return; }
    if (dragBand >= 0) {
        auto G = eqGraph();
        juce::String b = "b" + juce::String(dragBand + 1) + "_";
        setValue(b + "freq", 20.f * std::pow(10.f, 3.f * clamp01((p.x - G.getX()) / G.getWidth())));
        if (!mc_band_is_cut((int)std::round(get(b + "type"))))
            setValue(b + "gain", juce::jlimit(-12.f, 12.f, 18.f - (p.y - G.getY()) / G.getHeight() * 36.f));
        return;
    }
    if (dragCell < 0) return;
    if (std::abs(p.y - dragStartY) > 2.f) dragMoved = true;
    if (dragMoved) setNorm(cells()[(size_t)dragCell].id, dragStartNorm + (dragStartY - p.y) / 120.f);
}

void ScreenComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragCell >= 0 && !dragMoved) {   // a tap on a choice knob steps to the next choice
        auto id = cells()[(size_t)dragCell].id;
        int i = params::find(id);
        if (i >= 0 && mc_param(i)->kind == MC_KIND_CHOICE) {
            int n = params::choices(i).size();
            setValue(id, (float)(((int)std::round(get(id)) + 1) % n));
        }
    }
    dragCell = dragFader = dragBand = -1;
}

void ScreenComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (page == Eq) {
        static const float faderX[4] = { 4.f, 28.f, 272.f, 296.f };
        auto p = e.position / scale();
        auto ids = eqFaders();
        for (int i = 0; i < 4; i++)
            if (ids[(size_t)i].isNotEmpty() && juce::Rectangle<float>(faderX[i], 19.f, 20.f, 181.f).contains(p))
                setValue(ids[(size_t)i], params::defaultValue(params::find(ids[(size_t)i])));
        return;
    }
    int k = cellAt(e.position / scale());
    if (k < 0 || e.position.y / scale() >= kTabY) return;
    auto c = cells()[(size_t)k];
    int i = params::find(c.id);
    if (i >= 0 && !isButton(c)) setValue(c.id, params::defaultValue(i));
}

void ScreenComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int k = cellAt(e.position / scale());
    if (k < 0) return;
    auto c = cells()[(size_t)k];
    if (c.id.isEmpty() || isButton(c)) return;
    side = k / 4;
    setNorm(c.id, norm(c.id) + (w.deltaY > 0 ? 0.01f : -0.01f) * (w.isReversed ? -1.f : 1.f));
}

// ---- editor ----------------------------------------------------------------------------------

MasterEditor::MasterEditor(MasterProcessor& p) : juce::AudioProcessorEditor(&p), screen(p)
{
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(4.0 / 3.0);
    setResizeLimits(480, 360, 1280, 960);
    setSize(960, 720);
    addAndMakeVisible(screen);
}
