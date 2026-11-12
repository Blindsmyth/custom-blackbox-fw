/* Just enough of the VST2 SDK surface for the Airwindows classes in reference/airwindows to
 * compile and run offline as the double-precision reference. Not a plugin host. */
#ifndef __audioeffect__
#define __audioeffect__
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;
typedef void *audioMasterCallback;
enum VstPlugCategory { kPlugCategEffect = 1 };
enum {
    kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 8, kVstMaxVendorStrLen = 64,
    kVstMaxProductStrLen = 64, kVstMaxEffectNameLen = 32
};

class AudioEffect {
public:
    virtual ~AudioEffect() {}
};

class AudioEffectX : public AudioEffect {
public:
    AudioEffectX(audioMasterCallback, VstInt32, VstInt32) {}
    void setNumInputs(VstInt32) {}
    void setNumOutputs(VstInt32) {}
    void setUniqueID(VstInt32) {}
    void canProcessReplacing(bool = true) {}
    void canDoubleReplacing(bool = true) {}
    void programsAreChunks(bool = true) {}
    float getSampleRate() { return sampleRate; }
    float sampleRate = 48000.0f;
};

inline void vst_strncpy(char *dst, const char *src, size_t n) { strncpy(dst, src, n); dst[n] = 0; }
inline void float2string(float v, char *text, VstInt32 n) { snprintf(text, (size_t)n + 1, "%f", v); }
#endif
