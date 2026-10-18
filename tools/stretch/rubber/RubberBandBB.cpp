/* Rubber Band Library (GPL-2.0-or-later) as one translation unit, configured for the Blackbox:
 * float processing, CMSIS-DSP FFT, table sin/cos, approximate atan2, fixed pitch (R2 keeps no
 * resampler and one window size), no threads, timing or exceptions. Built into flash bank 2. */
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

#include "rubberband/single/RubberBandSingle.cpp"
