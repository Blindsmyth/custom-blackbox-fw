/* Minimal CMSIS-Compiler stubs for building CMSIS-DSP offline (probe / bank2).
 * Not a full CMSIS-Core. Apache-2.0. */
#ifndef __CMSIS_COMPILER_H
#define __CMSIS_COMPILER_H

#include <stdint.h>

#ifndef __ASM
#define __ASM __asm
#endif
#ifndef __INLINE
#define __INLINE inline
#endif
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#endif
#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((__noreturn__))
#endif
#ifndef __USED
#define __USED __attribute__((used))
#endif
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __PACKED
#define __PACKED __attribute__((packed, aligned(1)))
#endif
#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif
#ifndef __RESTRICT
#define __RESTRICT __restrict
#endif
#ifndef __COMPILER_BARRIER
#define __COMPILER_BARRIER() __ASM volatile("":::"memory")
#endif
#ifndef __CLZ
#define __CLZ(x) ((uint8_t)__builtin_clz(x))
#endif
#ifndef __SSAT
#define __SSAT(ARG1, ARG2) \
  __extension__({ int32_t __res, __arg = (ARG1); \
    __ASM("ssat %0, %1, %2" : "=r"(__res) : "I"(ARG2), "r"(__arg)); \
    __res; })
#endif
#ifndef __USAT
#define __USAT(ARG1, ARG2) \
  __extension__({ uint32_t __res, __arg = (ARG1); \
    __ASM("usat %0, %1, %2" : "=r"(__res) : "I"(ARG2), "r"(__arg)); \
    __res; })
#endif
#ifndef __PKHBT
#define __PKHBT(ARG1, ARG2, ARG3) \
  (((((uint32_t)(ARG1))) & 0x0000FFFFUL) | \
   ((((uint32_t)(ARG2)) << (ARG3)) & 0xFFFF0000UL))
#endif
#ifndef __PKHTB
#define __PKHTB(ARG1, ARG2, ARG3) \
  (((((uint32_t)(ARG1))) & 0xFFFF0000UL) | \
   ((((uint32_t)(ARG2)) >> (ARG3)) & 0x0000FFFFUL))
#endif
#ifndef __QADD8
#define __QADD8(a, b) ({ uint32_t __r; __ASM("qadd8 %0, %1, %2" : "=r"(__r) : "r"(a), "r"(b)); __r; })
#endif
#ifndef __QSUB8
#define __QSUB8(a, b) ({ uint32_t __r; __ASM("qsub8 %0, %1, %2" : "=r"(__r) : "r"(a), "r"(b)); __r; })
#endif
#ifndef __QADD16
#define __QADD16(a, b) ({ uint32_t __r; __ASM("qadd16 %0, %1, %2" : "=r"(__r) : "r"(a), "r"(b)); __r; })
#endif
#ifndef __QSUB16
#define __QSUB16(a, b) ({ uint32_t __r; __ASM("qsub16 %0, %1, %2" : "=r"(__r) : "r"(a), "r"(b)); __r; })
#endif
#ifndef __SMUAD
#define __SMUAD(a, b) ({ uint32_t __r; __ASM("smuad %0, %1, %2" : "=r"(__r) : "r"(a), "r"(b)); __r; })
#endif
#ifndef __SMUSD
#define __SMUSD(a, b) ({ uint32_t __r; __ASM("smusd %0, %1, %2" : "=r"(__r) : "r"(a), "r"(b)); __r; })
#endif
#ifndef __SMLALD
#define __SMLALD(a, b, c) ({ uint64_t __r = (c); __ASM("smlald %0, %1, %2" : "+r"(__r) : "r"(a), "r"(b)); __r; })
#endif

#endif
