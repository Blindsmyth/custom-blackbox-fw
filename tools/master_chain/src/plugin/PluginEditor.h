#pragma once
#include "PluginProcessor.h"

// One 320x240 "Blackbox screen" in logical pixels, scaled to the window. It copies the stock
// FX Return / pad INFO page: a bottom row of FX buttons and an 8-knob grid (4 columns x 2 rows,
// two sides of four; touching a knob makes its side active), with a header band above the grid
// for meters.
class ScreenComponent : public juce::Component, private juce::Timer
{
public:
    explicit ScreenComponent(MasterProcessor& p);
    ~ScreenComponent() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;   // Esc / Backspace = BACK

    // for tools/ui_snapshot: show a page and advance the meter histories without a message loop
    void showPage(int p, int s = 0, int eqBand = 0, bool info = false) { page = p; side = s; band = eqBand; eqInfo = info; }
    void pumpMeters() { timerCallback(); }

private:
    enum Page { Dly, Rvb, Aux, Eq, Clr, Cmp, Max, kPages };
    enum Special { None, ValuesButton };
    struct Cell { juce::String id; Special special = None; };
    static constexpr int kHist = 100;

    void timerCallback() override;
    float scale() const { return (float)getWidth() / 320.f; }
    std::array<Cell, 8> cells() const;
    juce::Rectangle<float> cellRect(int i) const;
    int cellAt(juce::Point<float> p) const;

    float get(const juce::String& id) const { return proc.value(id); }
    float norm(const juce::String& id) const;
    void setNorm(const juce::String& id, float n);
    void setValue(const juce::String& id, float v);
    juce::String valueText(const juce::String& id) const;
    bool isButton(const Cell& c) const;
    void press(const Cell& c);

    void drawTop(juce::Graphics&);
    void drawTabs(juce::Graphics&);
    void drawHeader(juce::Graphics&);
    void drawGrid(juce::Graphics&);
    void drawKnob(juce::Graphics&, juce::Rectangle<float> r, const Cell& c, bool activeSide);
    void drawGrStrip(juce::Graphics&, const juce::String& label, float gr, float hold, const std::array<float, kHist>& hist);
    void resetHolds();   // BACK: clear the max-reduction holds
    // EQ keeps the existing page: graph with draggable band nodes, band buttons and four corner
    // faders (Freq Gain Q Type); INFO swaps them for the dynamics (Thresh Range Attack Release).
    // The curve is the meter: per band a faint zone for its range, a solid fill for its current
    // gain change and an outline of its deepest point since BACK.
    void drawEqPage(juce::Graphics&);
    bool eqMouseDown(juce::Point<float> p);
    std::array<juce::String, 4> eqFaders() const;
    juce::Rectangle<float> eqGraph() const { return { 56.f, 35.f, 208.f, 165.f }; }
    float eqX(float hz) const { auto g = eqGraph(); return g.getX() + g.getWidth() * std::log10(hz / 20.f) / 3.f; }
    float eqY(float db) const { auto g = eqGraph(); return g.getY() + g.getHeight() * (18.f - db) / 36.f; }
    void text(juce::Graphics&, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
              juce::Colour c, float size = 8.f);

    MasterProcessor& proc;
    int page = Clr, band = 0, side = 0, eqInfo = 0;   // eqInfo: INFO shows the dynamic controls
    int dragCell = -1, dragFader = -1, dragBand = -1;
    float dragStartY = 0.f, dragStartNorm = 0.f;
    bool dragMoved = false;
    std::array<float, kHist> cmpHist {}, maxHist {};
    float cmpHold = 0.f, maxHold = 0.f;              // deepest reduction since BACK
    std::array<float, MC_BANDS> eqHold {};
    std::array<float, MC_BANDS> eqShown {};        // falling display of each band's reduction
    float clipFlash = 0.f;
};

class MasterEditor : public juce::AudioProcessorEditor
{
public:
    explicit MasterEditor(MasterProcessor&);
    void resized() override { screen.setBounds(getLocalBounds()); }

private:
    ScreenComponent screen;
};
