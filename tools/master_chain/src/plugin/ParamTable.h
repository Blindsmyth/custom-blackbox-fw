#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "mc_dsp.h"

// The parameter table lives in C (mc_dsp.c, mc_param_*) so mc_proto, the plugin and later the
// firmware share one list. These helpers adapt it to JUCE.
namespace params {

inline juce::String id(int i) { return mc_param(i)->id; }

inline juce::StringArray choices(int i)
{
    juce::StringArray s;
    s.addTokens(mc_param(i)->choices ? mc_param(i)->choices : "", "|", "");
    return s;
}

inline float defaultValue(int i)
{
    McParams p;
    mc_default_params(&p);
    return mc_param_get(&p, i);
}

inline int find(const juce::String& paramId) { return mc_param_find(paramId.toRawUTF8()); }

// Parameters outside the chain (not stored in the value sets).
inline const char* demo = "demo";

} // namespace params
