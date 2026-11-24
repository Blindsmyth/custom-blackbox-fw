/* Program Change launcher (3.1.U): what bank 1 (pcl.c) and bank 2 (pcl_b2.c) share. docs/pc-launcher.md. */
#ifndef PCL_H
#define PCL_H

#include <stdint.h>

#define PCL_N        4
#define PCL_ID       0x1E8         /* params 0x1E8-0x1EB, global: program | (channel - 1) << 8 */
#define PCL_VAL(p, c) ((p) | ((c) - 1) << 8)
#define PCL_BTN_ID   0x1D4         /* the buttons' widget ids 0x1D4-0x1D7 */
#define APP_SCREEN   0x8CA4        /* App: the screen shown */
#define SCR_PSET     7             /* the Presets screen */
#define PCL_MAGIC    0x314C4350u   /* 'PCL1' */

/* RbExt.pcl_ui's entries (pcl_b2.c), in this order */
enum { PCL_TICK, PCL_CHILD, PCL_EVENT, PCL_UI_N };

/* Backup SRAM 0x38800624-0x38800650 (between Seq MIDI out's start counters and the USB device's state). Written
 * by the UI task, except `sent` (audio task, pcl_pump); the mailbox is ch, pgm, then seq last. */
typedef struct {
    uint32_t magic;
    uint32_t blk;          /* PclBlk in SDRAM, from the boot allocator; 0 = none */
    uint8_t want;          /* PSET again on the Presets screen toggles it */
    uint8_t shown;         /* the layer is up */
    uint8_t mode;          /* the encoders edit: 0 the program, 1 the channel (INFO toggles) */
    uint8_t info_prev;     /* INFO's level last tick (0 = held) */
    uint8_t changed;       /* a value changed since the layer opened: save the settings when it closes */
    uint8_t seq, sent;     /* mailbox: seq bumped by a press, sent by pcl_pump */
    uint8_t ch;            /* mailbox: channel 0-15 */
    uint8_t pgm;           /* mailbox: program 0-127 */
    uint8_t pad[3];
} PclState;
#define PCL ((volatile PclState *)0x38800624u)
typedef char pcl_fits[(0x38800624u + sizeof(PclState) <= 0x38800650u) ? 1 : -1];

#define PCL_KIDS     40            /* stock widgets of the Presets screen hidden under the layer, at most */

/* SDRAM: the four buttons and what the layer hides */
typedef struct {
    unsigned btn[PCL_N][0x28];     /* stock frame buttons (0xA0 bytes) */
    void *screen;                  /* the Presets screen they were built on */
    void *kid[PCL_KIDS];           /* stock's widgets, hidden while the layer is up */
    uint8_t kid_hid[PCL_KIDS];     /* ... and their hidden flag before */
    uint32_t lit_ms[PCL_N];        /* a press lights its button until then */
    int16_t acc[PCL_N];            /* encoder counts not yet a step */
    uint8_t nkid, ask;             /* ask: the GUI passes our widgets need (1 partial, 2 full) */
} PclBlk;

static inline int pcl_live(void) { return PCL->magic == PCL_MAGIC && PCL->blk; }

#endif
