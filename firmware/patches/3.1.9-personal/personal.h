/* Personal features (WITH_PERSONAL; build_patch.py --release leaves them out). README.md. */
#ifndef PERSONAL_H
#define PERSONAL_H

#include <stdint.h>

#define ID_TAPOUT   0x1D9          /* Tools -> Clock "Tap Out:", global */
#define TAP_N       13             /* Off, then the Delay's 12 note values (1/64 ... 1 bar) */
#define ID_TAPCH    0x1F3          /* Tools -> Clock "Tap Ch:" 1-16, global, under Tap Out */
#define TAP_CC      28             /* Meris: tap */
#define TAP_VAL     127

/* Backup SRAM 0x38800638-0x38800650: after the PC launcher's state, before the USB device's. Audio task only. */
typedef struct {
    uint8_t count;                 /* half clock ticks since the last tap (< 194) */
    uint8_t due;                   /* the next tick taps (after Start, or when turned on) */
    uint8_t high;                  /* the CC is at TAP_VAL: 0 goes out half a note value after the tap */
    uint8_t pad;
} TapState;
#define TAP ((volatile TapState *)0x38800638u)

void personal_register(void *reg);
void personal_global_tpl(void *bag);
void personal_msg(const unsigned char *msg);

#endif
