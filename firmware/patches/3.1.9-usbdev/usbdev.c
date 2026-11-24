/*
 * USB MIDI device on the POWER jack (3.1.U, `--usbdev` build). MIDI only.
 *
 * Ported from blackbox-mod's src/usbdev.c (c4fadf6, (c) 2026 Justin Joe, MIT: LICENSE-blackbox-mod beside this
 * file). Taken out of it: the 6-channel USB audio stream (endpoint 2, its interrupt and the converter taps) and the
 * debug peek / poke requests. Changed: the receive FIFO is emptied by the poll (as blackbox-mod did before its 3.1.Q),
 * so no interrupt vector is touched.
 *
 * Stock runs one USB controller, OTG_FS at 0x40080000 on PA11 / PA12, as the host for the USB-A jack. The other one,
 * OTG_HS at 0x40040000, is never touched, and the pins of its built-in full-speed transceiver (PB14 = D-, PB15 = D+)
 * are never set up. The power jack's data lines go to those two pins (blackbox-mod's 3.1.K enumerated on a Mac), so
 * the computer feeding the power jack sees a full-speed, class-compliant MIDI device, "Blackbox", one port each way.
 *
 * usbdev_poll runs from the audio task's wait (diag.c cpu_wait), once per audio block, starts the controller ~2 s
 * after stock has started its own USB, and then answers enumeration and moves MIDI.
 *   in:  FUN_08044708(manager, events, bytes) queues events exactly as if they came from the USB-A jack, so MIDI In /
 *        Clock settings for USB apply to this port too.
 *   out: FUN_08044896 packs the "to USB" message queue for the host jack once per block (bl 0x08044B08, audio task).
 *        usbdev_midi_out reads the same queue first, without consuming it, from its own place in it (a burst
 *        over a block's 64 events, or a block the computer has not collected yet, goes out in the next blocks:
 *        the queue keeps the last 256 messages), and sends it to the computer: whatever is in it (stock's packer keeps only notes and clock / start / stop, so CCs and program changes, the MPE setup
 *        among them, reach the computer only here). Tools -> USB Dev Out governs both, as it fills that queue.
 * State: 32 bytes of backup SRAM at 0x38800650 and the MIDI queue at 0x38800908 (blackbox-mod's places; nothing else
 * on this line uses backup SRAM).
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC_D2CCIP2R  REG(0x58024454u)
#define USBSEL        (3u << 20)             /* the 48 MHz source both controllers share; stock sets it for the host */
#define RCC_AHB1ENR   REG(0x580244d8u)
#define RCC_AHB1LPENR REG(0x58024500u)
#define RCC_AHB4ENR   REG(0x580244e0u)
#define BKPRAMEN      (1u << 28)
#define PWR_CR1       REG(0x58024800u)
#define DBP           (1u << 8)
#define OTGHSEN       (1u << 25)
#define OTGHSULPIEN   (1u << 26)
#define OTGFSEN       (1u << 27)
#define PWR_CR3       REG(0x5802480cu)
#define USB33DEN      (1u << 24)

#define GPIOB         0x58020400u
#define GPIO_MODER    0x00
#define GPIO_OSPEEDR  0x08
#define GPIO_PUPDR    0x0c
#define GPIO_AFRH     0x24

#define USB           0x40040000u
#define GOTGCTL   REG(USB + 0x000)
#define GAHBCFG   REG(USB + 0x008)
#define GUSBCFG   REG(USB + 0x00c)
#define GRSTCTL   REG(USB + 0x010)
#define GINTSTS   REG(USB + 0x014)
#define GINTMSK   REG(USB + 0x018)
#define GRXSTSP   REG(USB + 0x020)
#define GRXFSIZ   REG(USB + 0x024)
#define DIEPTXF0  REG(USB + 0x028)
#define GCCFG     REG(USB + 0x038)
#define DIEPTXF1  REG(USB + 0x104)
#define DCFG      REG(USB + 0x800)
#define DCTL      REG(USB + 0x804)
#define DIEPCTL0  REG(USB + 0x900)
#define DIEPINT0  REG(USB + 0x908)
#define DIEPTSIZ0 REG(USB + 0x910)
#define DIEPCTL1  REG(USB + 0x920)
#define DIEPINT1  REG(USB + 0x928)
#define DIEPTSIZ1 REG(USB + 0x930)
#define DOEPCTL0  REG(USB + 0xb00)
#define DOEPINT0  REG(USB + 0xb08)
#define DOEPTSIZ0 REG(USB + 0xb10)
#define DOEPCTL1  REG(USB + 0xb20)
#define DOEPINT1  REG(USB + 0xb28)
#define DOEPTSIZ1 REG(USB + 0xb30)
#define PCGCCTL   REG(USB + 0xe00)
#define FIFO0     REG(USB + 0x1000)
#define FIFO1     REG(USB + 0x2000)

#define PHYSEL    (1u << 6)                  /* GUSBCFG: the built-in full-speed transceiver */
#define FDMOD     (1u << 30)                 /* GUSBCFG: device, whatever the ID pin says */
#define TRDT_6    (6u << 10)
#define CSRST     (1u << 0)
#define RXFFLSH   (1u << 4)
#define TXFFLSH_ALL ((1u << 5) | (0x10u << 6))
#define TXFFLSH_1 ((1u << 5) | (1u << 6))
#define PWRDWN    (1u << 16)                 /* GCCFG: transceiver on */
#define VBDEN     (1u << 21)
#define BVALO     ((1u << 6) | (1u << 7))    /* GOTGCTL: "VBUS is valid", without a sensing pin */
#define SDIS      (1u << 1)                  /* DCTL: soft disconnect (D+ pull-up off) */
#define CGINAK    (1u << 8)
#define RXFLVL    (1u << 4)
#define USBRST    (1u << 12)
#define ENUMDNE   (1u << 13)
#define EPENA     (1u << 31)
#define EPDIS     (1u << 30)
#define SNAK      (1u << 27)
#define CNAK      (1u << 26)
#define STALL     (1u << 21)
#define NAKSTS    (1u << 17)
#define USBAEP    (1u << 15)
#define SD0PID    (1u << 28)
#define STUP      (1u << 3)
#define XFRC      (1u << 0)
#define BULK_64   (64u | USBAEP | (2u << 18) | SD0PID)
#define TXFIFO_1  (1u << 22)

typedef void (*midi_in_fn)(void *manager, const uint8_t *events, unsigned bytes);
typedef int (*midi_pack_fn)(void *manager, uint8_t *queue, uint8_t *out, unsigned max, unsigned *len);
#define fw_midi_usb_in ((midi_in_fn)(0x08044708u | 1u))
#define fw_midi_pack   ((midi_pack_fn)(0x08044896u | 1u))
#define MIDI_MANAGER   ((void *)0x24002e88u)
#define Q_READ   0x500           /* a message queue: 256 x 5 bytes (up to 4 of message, then its length), two counters */
#define Q_WRITE  0x504
#define OUT_EVENTS 64            /* at most this many events to the computer per block (the endpoint's FIFO) */
#define SETUP_ARM ((3u << 29) | (1u << 19) | 64u)   /* three setups back to back, or one 64-byte packet */

struct usbdev_state {
    uint8_t setup[8];
    uint16_t count;
    uint8_t phase, config;
    uint8_t more_at, more_left;  /* rest of a descriptor longer than one packet */
    uint8_t mq_w, mq_r;          /* MIDI from the computer */
    uint8_t last_transport;      /* the last Start / Continue / Stop sent to the computer */
    uint8_t pad[3];
    uint32_t out_at;             /* the next "to USB" queue message for the computer (its own place) */
};
#define MQ_N 32u
#define MQ ((volatile uint32_t *)0x38800908u)                /* USB-MIDI events as they came off the wire */

#define S ((volatile struct usbdev_state *)0x38800650u)
typedef char usbdev_fits[(sizeof(struct usbdev_state) <= 0x40) ? 1 : -1];

enum { WAIT_BOOT, WAIT_RESET, WAIT_MODE, RUNNING, FAILED };
#define BOOT_BLOCKS 400          /* ~2 s of audio blocks after the host controller's clock is seen running */
#define MODE_BLOCKS 10           /* the core wants 25 ms after a mode change */

static const uint8_t DEVICE[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,
    0x09, 0x12, 0x02, 0x00,      /* 1209:0002, a pid.codes test id (blackbox-mod's MIDI-only build) */
    0x00, 0x01, 1, 2, 0, 1,
};
/* USB MIDI 1.0: an empty audio control interface and the MIDI streaming one, one port each way. */
static const uint8_t CONFIG[101] = {
    9, 2, 101, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 1, 1, 0, 0,                   /* audio control */
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1,          /* its header: one streaming interface, number 1 */
    9, 4, 1, 0, 2, 1, 3, 0, 0,                   /* MIDI streaming */
    7, 0x24, 1, 0x00, 0x01, 65, 0,
    6, 0x24, 2, 1, 1, 0,                         /* jack 1: in, embedded */
    6, 0x24, 2, 2, 2, 0,                         /* jack 2: in, external */
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,                /* jack 3: out, embedded, fed by 2 */
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,                /* jack 4: out, external, fed by 1 */
    9, 5, 0x01, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 1,
    9, 5, 0x81, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 3,
};
static const char MAKER[] = "Blackbox lineup";
static const char PRODUCT[] = "Blackbox";

static void bkp_enable(void)
{
    if (!(RCC_AHB4ENR & BKPRAMEN)) {
        RCC_AHB4ENR |= BKPRAMEN;
        (void)RCC_AHB4ENR;                                   /* let the clock settle before the first access */
    }
    if (!(PWR_CR1 & DBP))
        PWR_CR1 |= DBP;                                      /* backup domain writes */
}

static void send(const uint8_t *p, unsigned len)
{
    unsigned i, k;
    DIEPTSIZ0 = (1u << 19) | len;
    DIEPCTL0 |= EPENA | CNAK;
    for (i = 0; i < len; i += 4) {
        uint32_t w = 0;
        for (k = 0; k < 4 && i + k < len; k++)
            w |= (uint32_t)p[i + k] << (8 * k);
        FIFO0 = w;
    }
}

static void flush(uint32_t bits)
{
    int i;
    GRSTCTL = bits;
    for (i = 0; i < 2000 && (GRSTCTL & bits & (RXFFLSH | (1u << 5))); i++)
        ;
}

static void out_arm(void)
{
    DOEPTSIZ1 = (1u << 19) | 64u;
    DOEPCTL1 |= EPENA | CNAK;
}

/* Empty the receive FIFO: setup packets for request(), MIDI from the computer into MQ. */
static void receive(void)
{
    int guard;
    for (guard = 0; guard < 8 && (GINTSTS & RXFLVL); guard++) {
        uint32_t st = GRXSTSP, bytes = (st >> 4) & 0x7ffu, kind = (st >> 17) & 0xfu, ep = st & 0xfu, i, k;
        for (i = 0; i < bytes; i += 4) {
            uint32_t w = FIFO0;
            if (kind == 6 && i < 8)                          /* a setup packet's eight bytes */
                for (k = 0; k < 4; k++)
                    S->setup[i + k] = (uint8_t)(w >> (8 * k));
            if (kind != 2 || ep != 1 || !(w & 0x8u) || (uint8_t)(S->mq_w - S->mq_r) >= MQ_N)
                continue;                                    /* MIDI: only what stock's parser knows, while there is room */
            MQ[S->mq_w % MQ_N] = w;
            S->mq_w++;
        }
    }
}

static void request(void)
{
    uint8_t buf[64];
    unsigned type = S->setup[0], req = S->setup[1], lo = S->setup[2], hi = S->setup[3];   /* wValue */
    unsigned want = S->setup[6] | (unsigned)S->setup[7] << 8, len = 0, ok = 0, data = 1;
    const uint8_t *from = 0;

    S->more_left = 0;
    if (type == 0x80 && req == 6) {                          /* GET_DESCRIPTOR */
        const char *text = 0;
        if (hi == 1)
            from = DEVICE;
        else if (hi == 2)
            from = CONFIG;
        else if (hi == 3 && lo == 1)
            text = MAKER;
        else if (hi == 3 && lo == 2)
            text = PRODUCT;
        if (from) {
            len = from == DEVICE ? sizeof DEVICE : sizeof CONFIG;
            ok = 1;
        } else if (text) {
            for (len = 2; *text; text++) {
                buf[len++] = (uint8_t)*text;
                buf[len++] = 0;
            }
            ok = 1;
        } else if (hi == 3 && lo == 0) {
            buf[2] = 0x09;
            buf[3] = 0x04;
            len = 4;
            ok = 1;
        }
        if (hi == 3 && ok) {
            buf[0] = (uint8_t)len;
            buf[1] = 3;
        }
    } else if (type == 0x00 && req == 5) {                   /* SET_ADDRESS: the core applies it after the status */
        DCFG = (DCFG & ~(0x7fu << 4)) | ((lo & 0x7fu) << 4);
        ok = 1;
        data = 0;
    } else if (type == 0x00 && req == 9) {                   /* SET_CONFIGURATION */
        S->config = (uint8_t)lo;
        S->out_at = *(volatile uint32_t *)((uint8_t *)MIDI_MANAGER + 0x4B68 + Q_WRITE);   /* from now on */
        DIEPCTL1 = DIEPCTL1 & USBAEP ? DIEPCTL1 | SD0PID : BULK_64 | TXFIFO_1;
        DOEPCTL1 = DOEPCTL1 & USBAEP ? DOEPCTL1 | SD0PID : BULK_64;
        ok = 1;
        data = 0;
    } else if ((type == 0x01 && req == 11) || (type == 0x02 && req == 1)) {   /* SET_INTERFACE, clear an endpoint halt */
        ok = 1;
        data = 0;
    } else if (type == 0x81 && req == 10) {                  /* GET_INTERFACE */
        buf[0] = 0;
        len = 1;
        ok = 1;
    } else if (type == 0x80 && req == 8) {                   /* GET_CONFIGURATION */
        buf[0] = S->config;
        len = 1;
        ok = 1;
    } else if ((type & 0xe0) == 0x80 && req == 0) {          /* GET_STATUS */
        buf[0] = buf[1] = 0;
        len = 2;
        ok = 1;
    }

    if (!(DOEPCTL0 & EPENA))
        DOEPTSIZ0 = SETUP_ARM;
    if (!ok) {
        DIEPCTL0 |= STALL;
        DOEPCTL0 |= STALL;
        return;
    }
    if (data)
        DOEPCTL0 |= EPENA | CNAK;                            /* the host's empty status packet after our data */
    if (len > want)
        len = want;
    if (from && len > 64) {                                  /* the rest follows, a packet per block (serve) */
        S->more_at = 64;
        S->more_left = (uint8_t)(len - 64);
        len = 64;
    }
    send(from ? from : buf, len);
}

static void core_setup(void)
{
    GAHBCFG &= ~1u;                                          /* no interrupt: everything is polled */
    GCCFG &= ~VBDEN;
    GOTGCTL |= BVALO;
    PCGCCTL = 0;
    DCFG = (DCFG & ~3u) | 3u;                                /* full speed */
    flush(TXFFLSH_ALL);
    flush(RXFFLSH);
    GINTMSK = 0;
    GINTSTS = 0xbfffffffu;
    GRXFSIZ = 0x80;
    DIEPTXF0 = (0x40u << 16) | 0x80u;
    DIEPTXF1 = (0x40u << 16) | 0xc0u;
    S->mq_w = S->mq_r = S->last_transport = 0;
    GUSBCFG = (GUSBCFG & ~(0xfu << 10)) | TRDT_6;
    DOEPTSIZ0 = SETUP_ARM;
    DCTL &= ~SDIS;                                           /* D+ pull-up on: "a device is here" */
}

static void serve(void)
{
    uint32_t ints = GINTSTS, out, in;
    uint8_t upto;
    if (ints & USBRST) {
        GINTSTS = USBRST;
        DCFG &= ~(0x7fu << 4);
        DOEPTSIZ0 = SETUP_ARM;
        S->config = 0;
        if (DIEPCTL1 & EPENA)
            DIEPCTL1 |= EPDIS | SNAK;
        flush(TXFFLSH_1);
        DOEPCTL1 |= SNAK;
    }
    if (ints & ENUMDNE) {
        GINTSTS = ENUMDNE;
        DIEPCTL0 &= ~3u;                                     /* 64-byte packets on endpoint 0 */
        DCTL |= CGINAK;
    }
    receive();
    upto = S->mq_w;
    while (S->mq_r != upto) {                                /* MIDI from the computer, to stock's USB event parser */
        uint8_t events[68];
        unsigned n = 0, k;
        while (S->mq_r != upto && n < 64) {
            uint32_t w = MQ[S->mq_r % MQ_N];
            for (k = 0; k < 4; k++)
                events[n++] = (uint8_t)(w >> (8 * k));
            S->mq_r++;
        }
        events[n] = 0;                                       /* stock looks at the next event's first byte */
        fw_midi_usb_in(MIDI_MANAGER, events, n);
    }
    out = DOEPINT0;
    in = DIEPINT0;
    DOEPINT0 = out;
    DIEPINT0 = in;
    DOEPINT1 = DOEPINT1;
    DIEPINT1 = DIEPINT1;
    if (out & STUP) {
        request();
    } else if ((in & XFRC) && S->more_left) {                /* next packet of a long descriptor */
        unsigned n = S->more_left > 64 ? 64 : S->more_left;
        send(CONFIG + S->more_at, n);
        S->more_at = (uint8_t)(S->more_at + n);
        S->more_left = (uint8_t)(S->more_left - n);
    }
    if (S->config) {                                         /* the endpoint for the computer's MIDI, armed when idle */
        uint32_t c = DOEPCTL1;
        if (!(c & EPENA)) {
            if ((uint8_t)(S->mq_w - S->mq_r) <= MQ_N - 16u)
                out_arm();
        } else if (c & NAKSTS) {
            DOEPCTL1 = c | CNAK;
        }
    }
}

/* The USB-MIDI event code for a message's status byte; 0 = not sent. *last: the last Start / Continue / Stop sent, a
 * repeat of which is not sent again (slaved, stock queues each twice). */
unsigned usbdev_event_code(unsigned status, uint8_t *last)
{
    if (status == 0xfa || status == 0xfb || status == 0xfc) {
        if (*last == status)
            return 0;
        *last = (uint8_t)status;
    }
    if (status < 0x80)
        return 0;
    if (status < 0xf0)
        return status >> 4;
    if (status >= 0xf8)
        return 0xf;
    if (status == 0xf2)
        return 3;
    if (status == 0xf1 || status == 0xf3)
        return 2;
    return 0;
}

/*
 * Replaces the call that packs the "to USB" queue for the host jack (bl FUN_08044896 at 0x08044B08, audio task, once
 * per block). The queue's messages go to the computer first, from out_at, at most 64 a block; then stock packs and
 * consumes them as before. Stock's read counter does not matter here: a message stays in its slot until 256 newer
 * ones are written. If the computer has not taken the previous packet yet, they wait for the next block.
 */
int usbdev_midi_out(void *manager, uint8_t *queue, uint8_t *out, unsigned max, unsigned *len)
{
    if ((RCC_AHB1ENR & OTGHSEN) && S->phase == RUNNING && S->config && !(DIEPCTL1 & EPENA)) {
        uint32_t write = *(uint32_t *)(queue + Q_WRITE), read = S->out_at, waiting, events = 0, i;
        uint8_t last = S->last_transport, counted = last;
        if (write - read > 256u)                             /* lapped: the oldest are gone */
            read = write - 256u;
        waiting = write - read;
        if (waiting > OUT_EVENTS)
            waiting = OUT_EVENTS;
        S->out_at = read + waiting;
        for (i = 0; i < waiting; i++)
            events += usbdev_event_code(queue[((read + i) & 0xffu) * 5], &counted) != 0;
        S->last_transport = counted;
        if (events) {
            DIEPTSIZ1 = (((events + 15u) / 16u) << 19) | (events * 4u);
            DIEPCTL1 |= EPENA | CNAK;
            for (i = 0; i < waiting; i++) {
                const uint8_t *m = queue + ((read + i) & 0xffu) * 5;
                uint32_t w = usbdev_event_code(m[0], &last);
                unsigned k;
                if (!w)
                    continue;
                for (k = 0; k < 3 && k < m[4]; k++)
                    w |= (uint32_t)m[k] << (8 * (k + 1));
                FIFO1 = w;
            }
        }
    }
    return fw_midi_pack(manager, queue, out, max, len);
}

/* Once per audio block, from cpu_wait. */
void usbdev_poll(void)
{
    bkp_enable();
    if (!(RCC_AHB1ENR & OTGHSEN)) {                          /* first calls after a reset: the clock is still off */
        volatile uint32_t *b = (volatile uint32_t *)GPIOB;
        if (S->phase != WAIT_BOOT) {
            S->phase = WAIT_BOOT;
            S->count = 0;
        }
        if (!(RCC_AHB1ENR & OTGFSEN) || !(RCC_D2CCIP2R & USBSEL))
            return;                                          /* stock has not started its own USB yet */
        if (++S->count < BOOT_BLOCKS)
            return;
        PWR_CR3 |= USB33DEN;
        RCC_AHB1LPENR &= ~OTGHSULPIEN;                       /* no external transceiver: its clock must not gate ours */
        RCC_AHB1ENR |= OTGHSEN;
        (void)RCC_AHB1ENR;
        b[GPIO_AFRH / 4] = (b[GPIO_AFRH / 4] & 0x00ffffffu) | 0xcc000000u;   /* PB14, PB15: AF12, fastest, no pull */
        b[GPIO_OSPEEDR / 4] |= 0xf0000000u;
        b[GPIO_PUPDR / 4] &= 0x0fffffffu;
        b[GPIO_MODER / 4] = (b[GPIO_MODER / 4] & 0x0fffffffu) | 0xa0000000u;
        GUSBCFG |= PHYSEL;
        DCTL |= SDIS;
        GRSTCTL |= CSRST;
        S->count = 0;
        S->phase = WAIT_RESET;
        return;
    }
    switch (S->phase) {
    case WAIT_RESET:
        if (GRSTCTL & CSRST) {
            if (++S->count > 200)
                S->phase = FAILED;                           /* the core never came out of reset: no 48 MHz clock */
            return;
        }
        GCCFG |= PWRDWN;
        GUSBCFG = (GUSBCFG & ~(1u << 29)) | FDMOD | PHYSEL;
        DCTL |= SDIS;
        S->count = 0;
        S->phase = WAIT_MODE;
        return;
    case WAIT_MODE:
        if (++S->count < MODE_BLOCKS)
            return;
        core_setup();
        S->config = 0;
        S->phase = RUNNING;
        return;
    case RUNNING:
        serve();
        return;
    default:
        return;
    }
}
