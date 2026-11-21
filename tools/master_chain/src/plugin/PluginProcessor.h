#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "ParamTable.h"

class MasterProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kPresets = 8;   // simulated Blackbox project presets P1..P8

    MasterProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Blackbox Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    McParams currentParams() const;
    float value(const juce::String& id) const;
    float bpm() const { return bpmNow.load(); }

    // Values: the chain is the same for every preset. GLOBAL = one value set for all presets;
    // PRESET = each preset keeps its own values (seeded from the global set the first time).
    bool valuesPerPreset() const { return perPreset; }
    void setValuesPerPreset(bool on);
    void selectPreset(int i);
    int activePreset() const { return active; }

    struct Meters {
        std::atomic<float> eq[MC_BANDS] {}, eqDet[MC_BANDS] {};
        std::atomic<float> clrClip { 0.f }, cmpGr { 0.f }, compIn { -120.f }, maxGr { 0.f },
            maxOver { 0.f }, limGr { 0.f }, outPeak { -120.f };
    } meters;

    juce::AudioProcessorValueTreeState apvts;

private:
    using Vals = std::vector<float>;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    Vals capture() const;
    void apply(const Vals& v);
    static juce::String encode(const Vals& v);
    static bool decode(const juce::String& s, Vals& v);

    McChain chain;
    McDemo demo;
    double sampleRate = 48000.0;
    std::vector<std::atomic<float>*> raw;
    std::atomic<float> bpmNow { 120.f };
    int lastLatency = -1;

    // message-thread state
    bool perPreset = false;
    int active = 0;
    Vals globalVals;
    std::array<Vals, kPresets> presetVals;
    std::array<bool, kPresets> hasVals {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterProcessor)
};
