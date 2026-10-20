/* Optimized Rubber Band single-TU: float process, CMSIS-DSP FFT, fast trig.
 * GPL-2.0-or-later (Rubber Band). Build via tools/lab/rb/m7/build.sh OPT=cmsis. */
#define ALREADY_CONFIGURED 1

#define USE_BQRESAMPLER 1
#define NO_TIMING 1
#define NO_THREADING 1
#define NO_THREAD_CHECKS 1
#define NO_EXCEPTIONS 1
#define HAVE_CMSIS_DSP 1
#define USE_APPROXIMATE_ATAN2 1
#define USE_FAST_PHASOR 1
#define PROCESS_SAMPLE_TYPE float
#define LACK_POSIX_MEMALIGN 1
#define USE_OWN_ALIGNED_MALLOC 1
#define RB_FIXED_PITCH 1

#include "../rubberband/single/RubberBandSingle.cpp"
