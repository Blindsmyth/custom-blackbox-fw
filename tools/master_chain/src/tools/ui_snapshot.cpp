// ui_snapshot outdir [music.wav]: renders every page of the simulated Blackbox screen to PNG while
// the demo loop (or the given file) runs through the chain, so layout and meters can be checked
// without clicking. With a file, an extra eq_music.png shows band 4 ducking on real material.
#include "../plugin/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File out(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                            : juce::File::getCurrentWorkingDirectory());
    out.createDirectory();

    MasterProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000.0, 512);
    proc.prepareToPlay(48000.0, 512);
    if (argc <= 2)
        if (auto* d = proc.apvts.getParameter(params::demo)) d->setValueNotifyingHost(1.f);

    ScreenComponent screen(proc);
    screen.setSize(960, 720);
    juce::AudioBuffer<float> buf(2, 512), music;
    if (argc > 2) {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])));
        if (r) { music.setSize(2, (int)r->lengthInSamples); r->read(&music, 0, (int)r->lengthInSamples, 0, true, true); }
    }
    int musicPos = 0;
    juce::MidiBuffer midi;
    auto run = [&](double seconds) {
        for (int i = 0; i < (int)(seconds * 48000.0 / 512.0); i++) {
            buf.clear();
            if (music.getNumSamples() > 512) {
                if (musicPos + 512 > music.getNumSamples()) musicPos = 0;
                for (int c = 0; c < 2; c++) buf.copyFrom(c, 0, music, c, musicPos, 512);
                musicPos += 512;
            }
            proc.processBlock(buf, midi);
            if (i % 3 == 0) screen.pumpMeters();   // ~30 fps
        }
    };
    auto shot = [&](const char* name) {
        auto img = screen.createComponentSnapshot(screen.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream s(out.getChildFile(name));
        s.setPosition(0); s.truncate();
        juce::PNGImageFormat().writeImageToStream(img, s);
    };

    const char* names[] = { "dly.png", "rvb.png", "aux.png", "eq.png", "clr.png", "cmp.png", "max.png" };
    run(3.0);
    for (int p = 0; p < 7; p++) { screen.showPage(p); run(0.3); shot(names[p]); }
    screen.showPage(4, 1); shot("clr_side1.png");
    // EQ with dynamics working at the default thresholds: low shelf +3 dB, bands 3-5 ducking
    auto set = [&](const char* id, float v) { if (auto* q = proc.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); };
    set("eq_on", 1.f); set("b2_gain", 3.f); set("b3_range", -6.f); set("b4_range", -6.f); set("b5_range", -4.f);  // default thresholds
    run(2.0); screen.showPage(3, 0, 2, false); shot("eq_dyn.png");
    screen.showPage(3, 0, 2, true); shot("eq_info.png");
    screen.showPage(3, 0, 0, true); shot("eq_info_lowcut.png");
    screen.showPage(3, 0, 3, true); shot("eq_info_b4.png");
    if (argc > 2) {   // real music, only band 4 dynamic at its default threshold: the case that looked empty
        set("b3_range", 0.f); set("b5_range", 0.f); set("b2_gain", 0.f);
        screen.showPage(3, 0, 3, false);
        for (int k = 0; k < 40; k++) run(1.0 / 30.0);   // ~1.3 s of meter updates
        shot("eq_music.png");
    }
    {   // same page through the platform renderer (CoreGraphics on macOS), as a plugin window draws
        screen.showPage(3, 0, 2, false);
        juce::Image img(juce::Image::ARGB, screen.getWidth(), screen.getHeight(), true, juce::NativeImageType());
        { juce::Graphics g(img); screen.paintEntireComponent(g, false); }
        juce::FileOutputStream s(out.getChildFile("eq_dyn_native.png"));
        s.setPosition(0); s.truncate();
        juce::PNGImageFormat().writeImageToStream(img, s);
    }
    if (auto* t = proc.apvts.getParameter("cmp_type")) t->setValueNotifyingHost(1.f);
    run(3.0); screen.showPage(5); shot("cmp_stock.png");
    if (auto* c = proc.apvts.getParameter("chain_on")) c->setValueNotifyingHost(0.f);
    run(3.0); screen.showPage(6); shot("max_chain_off.png");
    proc.setValuesPerPreset(true);
    proc.selectPreset(2);
    screen.showPage(6); shot("max_preset_values.png");
    printf("wrote snapshots to %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
