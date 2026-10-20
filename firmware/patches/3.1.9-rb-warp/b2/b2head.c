#include <stdint.h>

#ifndef B2_ID
#define B2_ID 0x52425731u
#endif

#define B2_MAGIC 0x21324242u

__attribute__((used, section(".b2head")))
const uint32_t b2_header[4] = { B2_MAGIC, B2_ID, 0, 0 };
