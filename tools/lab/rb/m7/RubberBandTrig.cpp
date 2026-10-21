/* Like RubberBandOpt.cpp but with Rubber Band's own FFT: isolates the fast-trig changes.
 * GPL-2.0-or-later (Rubber Band). Build via tools/lab/rb/m7/build.sh OPT=trig. */
#define ALREADY_CONFIGURED 1

#define USE_BQRESAMPLER 1
#define NO_TIMING 1
#define NO_THREADING 1
#define NO_THREAD_CHECKS 1
#define NO_EXCEPTIONS 1
#define USE_BUILTIN_FFT 1
#define USE_APPROXIMATE_ATAN2 1
#define USE_FAST_PHASOR 1
#define PROCESS_SAMPLE_TYPE float
#define LACK_POSIX_MEMALIGN 1
#define USE_OWN_ALIGNED_MALLOC 1

#include "../rubberband/single/RubberBandSingle.cpp"
