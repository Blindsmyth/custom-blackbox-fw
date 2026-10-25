/* Clip Start / End / Loop Start / Loop End (ids 0x19D..0x1A0), in beats, and 8 hot cues per pad
 * (Cue Mode 0x1A1, Cue Note 0x1A2, Cue 1..8 0x1A3..0x1AA).
 *
 * The Clip engine plays one grain per sync unit (clip+0x1FC samples, clip+0x208 ticks; 960 ticks = one
 * beat). Positions are stored in beats and snapped down to whole units here, so the region only applies
 * when Sync is a note value: with Sync None the clip is one grain, with Sync Slice the units are slices.
 * Values per pad live in SRAM4 (the clip object is full) and are written by both the UI post and the
 * engine's 0x39 message. A clip whose region is the whole file behaves exactly like stock.
 *
 * Hot cues: with Cue Mode on, notes Cue Note .. Cue Note + 7 that reach a playing Clip (the pad's own
 * MIDI In channel; Global In triggers carry note 0) set a pending jump instead of retriggering. The next
 * grain that starts on a Quant Size boundary plays the cue's unit instead. On a stopped Clip the note
 * launches it at the cue. See docs/map-319.md "MIDI note path".
 */

/* SRAM4 0x38004930..0x38005000: past the folder state (fm_init clears S_SIZE 0x4930) and below the
 * Warp flags (0x38005000). Free on this line and on the Bungee / Mix line, whose ADSR, Mix, EQ, Seq Rec
 * and Looper state starts at 0x38005110 (docs/memory.md). 0x6D0 bytes; DkState uses 0x688. */
#define DK_BASE      0x38004930u
#define DK_END       0x38005000u
#define DK_MAGIC     0x34434544u  /* 'DEC4': positions in 1/16 notes; 'DEC3' was beats */
#define DK_PADS      16           /* the 4 x 4 pad grid */
#define GRID_MAX     64           /* grid markers drawn at once */
#ifndef DECK_ID_BASE
#define DECK_ID_BASE 0x19D     /* 3.1.E; the combined 3.1.T build moves them past the Mix line's ids */
#endif
#define ID_START     (DECK_ID_BASE + 0)
#define ID_CUEMODE   (DECK_ID_BASE + 4)
#define ID_CUENOTE   (DECK_ID_BASE + 5)
#define ID_CUE1      (DECK_ID_BASE + 6)
#define ID_GRID      (DECK_ID_BASE + 14)   /* Grid size: GRID_* list index */
#define ID_GRIDOFS   (DECK_ID_BASE + 15)   /* grid offset, samples */
#define DECK_IDS     16        /* 0x19D..0x1AC on 3.1.E */
#define TICKS_S16    240       /* positions are 1/16 notes */
#define POS_END      0xFFFFu   /* End / Loop End: the end of the file */
#define POS_MAX      0xFFFEu
#define GRID_SIZES   9
#define GRID_DEF     4         /* 1 bar */
#define OFS_MAX      (48000 * 8)
#define CUE_BEATS    4096      /* cue params: 0 = Off, k = beat k - 1 */
#define CUES         8
#define CUE_NONE     0xFFFFu
#define CUE_NOTE_DEF 36
#define CUE_SWALLOW  0x100     /* deck_cue_note: drop the voice start */
#define ID_PADNOTE   0xF5
#define ID_LOOPMODE  0x8D
#define TICKS_BEAT   960

typedef struct {
    unsigned short v[4];      /* start, end, loop start, loop end (1/16 notes; end 0 or POS_END = file end) */
    float spu;                /* samples per unit (clip+0x1FC) */
    unsigned short unit_ticks;/* clip+0x208 when it is a real unit, else 0 */
    unsigned short units;     /* clip+0x240 + clip+0x244 */
} DkPad;

typedef struct {
    unsigned short cue[CUES]; /* 1/16 notes, CUE_NONE = unset */
    unsigned short pending;   /* unit to jump to at the next Quant Size boundary, CUE_NONE = none */
    unsigned short launch;    /* unit the next note start begins at (cue note on a stopped clip) */
    unsigned char note;       /* Cue Note: first of the 8 notes */
    unsigned char mode;       /* Cue Mode: 0 Off, 1 On */
    unsigned short play;      /* unit the engine started last (the playhead), CUE_NONE = never */
} DkCue;

typedef struct {
    int ofs;                  /* grid offset in samples: unit k starts at ofs + k * spu */
    unsigned char size;       /* Grid size, GRID_* index */
    unsigned char loop;       /* LOOP_*: where the playhead is against the loop (in_loop) */
    unsigned char rsv[2];
} DkGrid;

typedef struct {
    unsigned char popup;      /* +0x00 POP_*: the popup open on a Clip pad */
    unsigned char sel;        /* +0x01 selected cue 0..7 */
    unsigned char raw;        /* +0x02 Speed: 1 = Raw (encoders x RAW_MUL) */
    unsigned char grid;       /* +0x03 grid lines on the waveform */
    unsigned press_id;        /* +0x04 button id of the last press (type 1) */
    unsigned press_tick;      /* +0x08 UI tick at that press */
    void *row_view;           /* +0x0C view whose ... button was last shown with a redraw */
    int acc[4];               /* +0x10 encoder counts not yet turned into a step, per knob */
    unsigned cue_keys;        /* +0x20 Keys screen: UI key of the Clip shown as A-H, 0xFFFF = none */
    unsigned char keys_on;    /* +0x24 Keys shows cue keys (read by hook_keys_count) */
    unsigned char layout;     /* +0x25 LAY_* the row was last laid out for, 0 = never */
    unsigned char rsv26;      /* +0x26 */
    unsigned char cue_used;   /* +0x27 a letter was used while Cue was held: its release does nothing */
    unsigned save[4];         /* +0x28 register save for the UI-task stubs (the audio task preempts them) */
    void *btn_view;           /* +0x38 view whose row got our three SRAM buttons */
    unsigned title[4];        /* +0x3C hash of each knob's title, so unchanged titles are not redrawn */
    void *wave;               /* +0x4C waveform widget showing a Clip pad (edit_target), 0 = none */
    unsigned seen;            /* +0x50 UI tick of its last draw (it redraws every frame while shown) */
    int cursor;               /* +0x54 sample under the white marker, less the grid offset */
    unsigned char edit_pad;   /* +0x58 that Clip's pad index + 1, 0 = none */
    unsigned char line_s16;   /* +0x59 grid line step in 1/16 notes, 0 = lines off */
    unsigned char rsv[6];
} DkUi;                       /* 0x60 */

typedef struct {
    const unsigned *pos;      /* same shape as a cell's slice list: positions, count */
    unsigned count;
} DkMarks;

typedef struct {
    unsigned magic;           /* +0x00 */
    unsigned save[7];         /* +0x04 register save for the no-push stubs in deck.S */
    unsigned wrap_clip;       /* +0x20 clip whose next unit-0 grain is a loop wrap */
    unsigned wrap_unit;       /* +0x24 unit to restart at */
    unsigned char bar_mode;   /* +0x28 cellmode the bottom-bar label was last set for */
    unsigned char rsv[23];
    DkPad pad[DK_PADS];       /* +0x40 */
    DkCue cue[DK_PADS];       /* +0x140, 24 bytes each */
    DkGrid grid[DK_PADS];     /* +0x2C0 */
    DkUi ui;                  /* +0x340 */
    DkMarks marks;            /* +0x3A0 */
    unsigned grid_pos[GRID_MAX]; /* +0x3A8 */
    unsigned btn[3][0x28];    /* +0x4A8: three stock buttons (0xA0 each) for the row; to +0x688 */
} DkState;

typedef char dk_fits[(DK_BASE + sizeof(DkState) <= DK_END) ? 1 : -1];
typedef char dk_keys_on_at[(__builtin_offsetof(DkState, ui.keys_on) == 0x364) ? 1 : -1];   /* KEYS_ON in deck.S */
typedef char dk_save_at[(__builtin_offsetof(DkState, save) == 0x04) ? 1 : -1];                /* DK_SAVE */
typedef char dk_ui_save_at[(__builtin_offsetof(DkState, ui.save) == 0x368) ? 1 : -1];          /* UI_SAVE */
typedef char dk_btn_at[(__builtin_offsetof(DkState, btn) == 0x4A8) ? 1 : -1];

typedef struct { unsigned s, e, ls, le, n; } DkRegion;

#define ST ((DkState *)DK_BASE)

typedef int (*param_get_fn)(void *, unsigned);
typedef void (*bind_fn)(unsigned, unsigned, void *, unsigned);
typedef void (*label_fn)(void *, const char *);
typedef void (*count_fn)(void *, int);
typedef void (*view_fn)(void *);
typedef void (*range_fn)(void *, int, int);
typedef void (*value_fn)(void *, int);
typedef void (*beats_fn)(void *, void *);
typedef void (*span_fn)(void *, int, int);
typedef void (*loop_fn)(void *, int, int, unsigned);
typedef void (*bar_fn)(void *, unsigned);
typedef void (*text_fn)(void *, const char *);
typedef void (*state_fn)(void *, unsigned);
typedef void (*post_fn)(void *, void *);

#define PARAM_GET   ((param_get_fn)0x08093E9Du)
#define KNOB_BIND   ((bind_fn)0x080A7EE1u)     /* id, value, widget: title, type, range, value */
#define KNOB_LABEL  ((label_fn)0x080B03C1u)
#define KNOB_COUNT  ((count_fn)0x080B0985u)    /* view+0x47C8: how many knobs take focus */
#define VIEW_ZOOM   ((view_fn)0x080A53A3u)
#define KNOB_RANGE  ((range_fn)0x080B09E5u)
#define KNOB_VALUE  ((value_fn)0x080B0791u)
#define WAVE_BEATS  ((beats_fn)0x080C6E8Du)    /* slice/beat markers; clears the region flags */
#define WAVE_SPAN   ((span_fn)0x080C6F15u)     /* start, length (samples) */
#define WAVE_LOOP   ((loop_fn)0x080C6EDDu)     /* loop start, loop end (samples), shown */
#define VIEW_BAR    ((bar_fn)0x080A5481u)      /* 1: slicer bar (+0x1568), 0: plain bar (+0x11F0) */
#define BTN_TEXT    ((text_fn)0x080A3DEDu)
#define BTN_STATE   ((state_fn)0x080A3E01u)
#define UI_POST_UP  ((post_fn)0x080AED15u)
#define HIDE        ((state_fn)0x080C4E61u)    /* widget hidden flag (+0x30) + redraw */
#define VIEW_UPDATE ((state_fn)0x080A54A1u)    /* waveform screen update (view, force) */

static unsigned u8(const void *p, unsigned off) { return ((const unsigned char *)p)[off]; }
static unsigned u32(const void *p, unsigned off) { return *(const unsigned *)((const char *)p + off); }
static void *at(void *p, unsigned off) { return (char *)p + off; }

static unsigned idx_unpacked(unsigned key)   /* engine key: row << 8 | col */
{
    return ((key >> 8) & 3) << 2 | (key & 3);
}

static DkPad *pad_unpacked(unsigned key)
{
    return &ST->pad[idx_unpacked(key)];
}

static DkCue *cue_unpacked(unsigned key)
{
    return &ST->cue[idx_unpacked(key)];
}

static DkPad *pad_packed(unsigned key)     /* UI key: row << 4 | col */
{
    return &ST->pad[((key >> 4) & 3) << 2 | (key & 3)];
}

static int live(void)
{
    return ST->magic == DK_MAGIC;
}

#ifdef SONG_CUES
/* Song cues (3.1.T, docs/song-cues.md): the code is at the end of this file. */
static void song_init(void);
static unsigned song_start_unit(void *clip, DkCue *c);
static unsigned song_grain(void *clip, DkCue *c, const long long *tick);
static void song_drop(void *clip);
#endif

void deck_init(void)
{
    unsigned *w = (unsigned *)DK_BASE;
    unsigned i;
    if (ST->magic != DK_MAGIC) {
        for (i = 0; i < sizeof(DkState) / 4; i++)
            w[i] = 0;
        for (i = 0; i < DK_PADS; i++) {
            DkCue *c = &ST->cue[i];
            unsigned k;
            for (k = 0; k < CUES; k++)
                c->cue[k] = CUE_NONE;
            c->note = CUE_NOTE_DEF;
        }
        ST->magic = DK_MAGIC;
    }
    for (i = 0; i < DK_PADS; i++) {
        ST->cue[i].pending = CUE_NONE;
        ST->cue[i].launch = CUE_NONE;
        ST->cue[i].play = CUE_NONE;
        if (ST->grid[i].size >= GRID_SIZES)
            ST->grid[i].size = GRID_DEF;
    }
    for (i = 0; i < sizeof(DkUi) / 4; i++)
        ((unsigned *)&ST->ui)[i] = 0;
    ST->ui.cue_keys = 0xFFFF;
    ST->ui.grid = 1;       /* grid lines on: the Grid popup's Lines turns them off */
    ST->wrap_clip = 0;
    ST->bar_mode = 0xFF;   /* the view is rebuilt on boot with the stock "..." label */
#ifdef SONG_CUES
    song_init();
#endif
}

int deck_is_id(unsigned id)
{
    return id - ID_START < DECK_IDS;
}

static DkGrid *grid_unpacked(unsigned key)
{
    return &ST->grid[idx_unpacked(key)];
}

/* Hook_post: r1 is the unpacked key. Positions are 1/16 notes; a cue value is 0 = Off, k = beat k - 1
 * (the SRAM4 table keeps 1/16 notes). */
void deck_store_unpacked(unsigned key, unsigned id, unsigned value)
{
    DkCue *c;
    if (!live() || !deck_is_id(id))
        return;
    if (id < ID_CUEMODE) {
        pad_unpacked(key)->v[id - ID_START] = (unsigned short)(value > 0xFFFF ? 0xFFFF : value);
        return;
    }
    if (id == ID_GRID) {
        grid_unpacked(key)->size = (unsigned char)(value < GRID_SIZES ? value : GRID_DEF);
        return;
    }
    if (id == ID_GRIDOFS) {
        int o = (int)value;
        grid_unpacked(key)->ofs = o > OFS_MAX ? OFS_MAX : o < -OFS_MAX ? -OFS_MAX : o;
        return;
    }
    c = cue_unpacked(key);
    if (id == ID_CUEMODE) {
        c->mode = value != 0;
        if (!c->mode)
            c->pending = CUE_NONE;
    } else if (id == ID_CUENOTE) {
        c->note = (unsigned char)(value > 127 ? 127 : value);
    } else {
        c->cue[id - ID_CUE1] = (unsigned short)(value == 0 || value > CUE_BEATS ? CUE_NONE : (value - 1) * 4);
    }
}

typedef void (*reg_num_fn)(void *, unsigned, unsigned, const char *, int, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
typedef void (*reg_enum_fn)(void *, unsigned, const char *, const char *const *, int, const char *);
typedef const char *const *(*labels_fn)(unsigned);
typedef int (*count_of_fn)(unsigned);
#define PARAM_NUMBER ((reg_num_fn)0x0808C12Du)   /* reg, id, type, title, min, max, xml */
#define PARAM_ENUM   ((reg_enum_fn)0x0808C0D9u)  /* reg, id, title, labels, count, xml (type 5) */
#define PARAM_LABELS ((labels_fn)0x0808E279u)
#define PARAM_NLABEL ((count_of_fn)0x0808E2A5u)
#define PARAM_ADD    ((param_add_fn)0x08093EF7u)

/* XML names changed with the switch from beats to 1/16 notes (3.1.E rev 2), so an older save's beat
 * values are not read as 1/16 notes; those attributes are ignored now. */
static const char t_start[] = "Start:";
static const char t_end[] = "End:";
static const char t_lpst[] = "Loop Start:";
static const char t_lpend[] = "Loop End:";
static const char x_start[] = "cstart16";
static const char x_end[] = "cend16";
static const char x_lpst[] = "cloopst16";
static const char x_lpend[] = "cloopend16";
static const char t_grid[] = "Grid:";
static const char x_grid[] = "cgrid";
static const char t_gridofs[] = "Grid Offset:";
static const char x_gridofs[] = "cgridofs";
static const char g_16[] = "1/16";
static const char g_8[] = "1/8";
static const char g_4[] = "1/4";
static const char g_2[] = "1/2";
static const char g_1b[] = "1 bar";
static const char g_2b[] = "2 bars";
static const char g_4b[] = "4 bars";
static const char g_8b[] = "8 bars";
static const char g_16b[] = "16 bars";
static const char *const grid_names[GRID_SIZES] = {g_16, g_8, g_4, g_2, g_1b, g_2b, g_4b, g_8b, g_16b};
static const unsigned short grid_s16[GRID_SIZES] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
static const char t_cuemode[] = "Cue Mode:";
static const char t_cuenote[] = "Cue Note:";
static const char x_cuemode[] = "cuemode";
static const char x_cuenote[] = "cuenote";
static const char s_off[] = "Off";
static const char s_on[] = "On";
static const char *const onoff[2] = {s_off, s_on};
static const char t_cue[CUES][7] = {"Cue A:", "Cue B:", "Cue C:", "Cue D:", "Cue E:", "Cue F:", "Cue G:", "Cue H:"};
static const char x_cue[CUES][7] = {"cueb1", "cueb2", "cueb3", "cueb4", "cueb5", "cueb6", "cueb7", "cueb8"};

/* Cue Mode is Off/On. Cue Note reuses stock Pad Note's note names (index = MIDI note; 0 reads
 * "Default" there). Cues are numbers in beats: 0 = Off, k = beat k - 1 (the Info list shows "Off" or
 * bar.beat; 4097 steps, so the list knob works: 0..0xFFFF did not). */
static void deck_register_cues(void *reg)
{
    const char *const *notes = PARAM_LABELS(ID_PADNOTE);
    int n = PARAM_NLABEL(ID_PADNOTE);
    unsigned i;
    PARAM_ENUM(reg, ID_CUEMODE, t_cuemode, onoff, 2, x_cuemode);
    if (!notes || n < 128)
        PARAM_NUMBER(reg, ID_CUENOTE, 1, t_cuenote, 0, 127, x_cuenote);
    else
        PARAM_ENUM(reg, ID_CUENOTE, t_cuenote, notes, 128, x_cuenote);
    for (i = 0; i < CUES; i++)
        PARAM_NUMBER(reg, ID_CUE1 + i, 1, t_cue[i], 0, CUE_BEATS, x_cue[i]);
}

/* Param_RegisterAll tail (hook_register). Type 1: plain integer list, like Slice (0x4E). */
void deck_register(void *reg)
{
    PARAM_NUMBER(reg, ID_START + 0, 1, t_start, 0, POS_MAX, x_start);
    PARAM_NUMBER(reg, ID_START + 1, 1, t_end, 1, POS_END, x_end);
    PARAM_NUMBER(reg, ID_START + 2, 1, t_lpst, 0, POS_MAX, x_lpst);
    PARAM_NUMBER(reg, ID_START + 3, 1, t_lpend, 1, POS_END, x_lpend);
    deck_register_cues(reg);
    PARAM_ENUM(reg, ID_GRID, t_grid, grid_names, GRID_SIZES, x_grid);
    PARAM_NUMBER(reg, ID_GRIDOFS, 1, t_gridofs, -OFS_MAX, OFS_MAX, x_gridofs);
#ifdef SONG_CUES
    {
        void song_register(void *reg);
        song_register(reg);
    }
#endif
}

static int bag_has(const void *bag, unsigned id)
{
    unsigned n = *(const unsigned short *)((const char *)bag + 10);
    const unsigned short *row = *(const unsigned short *const *)((const char *)bag + 4);
    if (!row)
        return 0;
    for (; n; n--, row += 4)
        if (*row == id)
            return 1;
    return 0;
}

/* Default rows so the ids are saved (hook_template) and exist on Clip/Slicer pads (hook_clip_def).
 * End and Loop End default to POS_END = the end of the file, so an untouched clip loops as a whole.
 * Cue Mode Off, Cue Note 36, Cue A at beat 0 (the rest Off), Grid 1 bar, no grid offset. */
void deck_bag_defaults(void *bag, unsigned only_missing)
{
    static const unsigned short def[DECK_IDS] = {0, POS_END, 0, POS_END, 0, CUE_NOTE_DEF,
                                                 1, 0, 0, 0, 0, 0, 0, 0, GRID_DEF, 0};   /* Cue A at the start */
    unsigned i;
    for (i = 0; i < DECK_IDS; i++)
        if (!only_missing || !bag_has(bag, ID_START + i))
            PARAM_ADD(bag, ID_START + i, def[i]);
}

/* 1/16 notes -> Sync units, snapped down (the engine plays one grain per unit). */
static unsigned to_units(unsigned s16, unsigned ut)
{
    return s16 * TICKS_S16 / ut;
}

/* The file's length in 1/16 notes (whole ones). */
static unsigned total_s16(unsigned n, unsigned ut)
{
    return n * ut / TICKS_S16;
}

/* End / Loop End: 0, POS_END (the default) and anything at or past the file's end mean the end of the
 * file. (In 3.1.E rev 1 a 512-beat sentinel made long files stop at beat 512.) */
static unsigned end_units(unsigned s16, unsigned ut, unsigned n)
{
    if (s16 == 0 || s16 >= POS_END || s16 >= total_s16(n, ut))
        return n;
    return to_units(s16, ut);
}

/* Region in units, clamped. Returns 0 when the clip should play like stock. */
static int region(const void *clip, DkRegion *r)
{
    const DkPad *p;
    unsigned ut, n, s, e, ls, le;

    if (!live() || u8(clip, 0x254))
        return 0;                                       /* Slicer */
    if (!u8(clip, 0x1F5))
        return 0;                                       /* Sync None: the file is one grain */
    if (u32(clip, 0x220) == 0xF0000 && u32(clip, 0x224) == 0)
        return 0;                                       /* Sync Slice */
    if (u32(clip, 0x20C) != 0)
        return 0;
    ut = u32(clip, 0x208);
    n = u32(clip, 0x240) + u32(clip, 0x244);
    if (ut == 0 || ut > 0xFFFF || n < 2)
        return 0;
    p = pad_unpacked(u32(clip, 0x18));
    s = to_units(p->v[0], ut);
    e = end_units(p->v[1], ut, n);
    ls = to_units(p->v[2], ut);
    le = end_units(p->v[3], ut, n);
    if (e > n)
        e = n;
    if (s >= n)
        s = n - 1;
    if (e <= s)
        e = s + 1;
    if (ls < s)
        ls = s;
    if (le > e)
        le = e;
    if (le <= ls) {
        ls = s;
        le = e;
    }
    r->s = s;
    r->e = e;
    r->ls = ls;
    r->le = le;
    r->n = n;
    return !(s == 0 && e == n && ls == 0 && le == n);
}

/* End of Clip_RecalcRates (hook_rate) and after a region param reaches the engine. Publishes the unit
 * size for the waveform screen. With a region, the wrap re-syncs to one unit instead of the clip's
 * loop length or quant size (+0x230): Loop Start rarely sits on that grid. */
void deck_rate(void *clip)
{
    DkPad *p;
    DkRegion r;
    unsigned ut;

    if (!live() || !clip)
        return;
    p = pad_unpacked(u32(clip, 0x18));
    if (u8(clip, 0x254) == 0) {
        ut = u32(clip, 0x208);
        p->spu = *(const float *)at(clip, 0x1FC);
        p->unit_ticks = (u8(clip, 0x1F5) && u32(clip, 0x20C) == 0 && ut <= 0xFFFF
                         && !(u32(clip, 0x220) == 0xF0000 && u32(clip, 0x224) == 0)) ? (unsigned short)ut : 0;
        p->units = (unsigned short)(u32(clip, 0x240) + u32(clip, 0x244));
    }
    if (region(clip, &r)) {
        *(unsigned *)at(clip, 0x230) = u32(clip, 0x208);
        *(unsigned *)at(clip, 0x234) = u32(clip, 0x20C);
    }
}

/* Clip engine msg 0x39 for a region id. */
void deck_clip_param(void *clip, unsigned id, unsigned value)
{
    deck_store_unpacked(u32(clip, 0x18), id, value);
    deck_rate(clip);
}

/* Units of a Clip on a note-value Sync grid, or 0 (Slicer, Sync None, Sync Slice). */
static unsigned grid_units(const void *clip, unsigned *ut)
{
    unsigned n;
    if (!live() || u8(clip, 0x254) || !u8(clip, 0x1F5))
        return 0;
    if ((u32(clip, 0x220) == 0xF0000 && u32(clip, 0x224) == 0) || u32(clip, 0x20C) != 0)
        return 0;
    *ut = u32(clip, 0x208);
    n = u32(clip, 0x240) + u32(clip, 0x244);
    if (*ut == 0 || *ut > 0xFFFF || n < 2)
        return 0;
    return n;
}

static DkCue *clip_cue(const void *clip)
{
    return cue_unpacked(u32(clip, 0x18));
}

/* Cues work when Cue Mode is on or any cue is set (Cue A is set by default). */
static int cue_active(const DkCue *c)
{
    unsigned k;
    if (c->mode)
        return 1;
    for (k = 0; k < CUES; k++)
        if (c->cue[k] != CUE_NONE)
            return 1;
    return 0;
}

/* hook_lane: Smooth/Capi/Vari play stock grains while a region is set or Cue Mode is on (they keep
 * their own read head, so a cue jump would not be heard). */
int deck_active(const void *clip)
{
    DkRegion r;
    unsigned ut;
    if (region(clip, &r))
        return 1;
    return grid_units(clip, &ut) && cue_active(clip_cue(clip));
}

/* The unit a grain is about to play: the playhead the UI reads for "set cue" and "instant loop". */
/* Loop on: once the playhead is in the loop it stays there, whatever moves the loop (Loop Start / End,
 * Move, Nudge, Instant, the Pos list). A unit the loop no longer covers is taken to the same place in
 * the loop, before or after it, so moving a loop by a bar moves the playhead with it (Traktor / rekordbox
 * loop move). `placed` = a note start or cue jump: it may land outside the loop; from before Loop Start
 * the clip plays into the loop, from past Loop End it plays on to End and then loops. */
enum { LOOP_NONE, LOOP_IN, LOOP_OUT };

static unsigned in_loop(void *clip, unsigned unit, int placed)
{
    DkGrid *g = grid_unpacked(u32(clip, 0x18));
    DkRegion r;
    unsigned len;
    int inside;
    if (!region(clip, &r) || !u8(clip, 0x1F4) || u8(clip, 0x1F8) == 1) {
        g->loop = LOOP_NONE;
        return unit;
    }
    inside = unit >= r.ls && unit < r.le;
    if (placed || inside) {
        g->loop = inside ? LOOP_IN : LOOP_OUT;
        return unit;
    }
    if (g->loop != LOOP_IN)
        return unit;
    len = r.le - r.ls;
    return unit >= r.le ? r.ls + (unit - r.ls) % len : r.le - 1 - (r.ls - 1 - unit) % len;
}

static unsigned played(void *clip, unsigned unit)
{
    clip_cue(clip)->play = (unsigned short)(unit > 0xFFFE ? 0xFFFE : unit);
    return unit;
}

static int edit_start(void *clip, unsigned n, unsigned ut, unsigned *unit);

/* Note start (0x08065A9A): the first grain. Stock passes unit 0 for a Clip. A cue note that launched
 * this clip starts at the cue; with the editor open on this pad, at the bar of the white marker
 * (edit_start); otherwise the region's Start. Any pending jump belongs to the old play. */
unsigned deck_note_unit(void *clip, unsigned unit)
{
    DkRegion r;
    DkCue *c;
    unsigned ut, n, at;
    if (!live())
        return unit;
    ST->wrap_clip = 0;
    c = clip_cue(clip);
    at = c->launch;
    c->launch = CUE_NONE;
    c->pending = CUE_NONE;
#ifdef SONG_CUES
    if (at == CUE_NONE)
        at = song_start_unit(clip, c);   /* the playing Song scene's cue for this pad */
#endif
    if (unit != 0)
        return played(clip, unit);
    if (at != CUE_NONE && (n = grid_units(clip, &ut)) != 0)
        return played(clip, in_loop(clip, at < n ? at : n - 1, 1));
    if ((n = grid_units(clip, &ut)) != 0 && edit_start(clip, n, ut, &at)) {
        if (region(clip, &r))
            at = at < r.s ? r.s : at >= r.e ? r.e - 1 : at;
        return played(clip, in_loop(clip, at, 1));
    }
    if (region(clip, &r))
        return played(clip, in_loop(clip, r.s, 1));
    return played(clip, unit);
}

typedef void (*seek_fn)(void *, int);
#define SAMPLE_SEEK ((seek_fn)0x0806375Du)   /* (sample ref, frame): provider vt+0x10 = 0x08074CE8 */

/* Ask pcmStreamer for the audio a cue jump will play, ahead of the jump. The sample ref's seek has no
 * read head: it only refreshes the LRU stamp of blocks frame>>13 .. +2 or queues them for loading
 * (FUN_08074554, RAM + semaphore; Clip_StartGrain makes the same call from this task). Calling it on
 * every grain until the jump keeps those blocks the youngest in the cache, so they are not evicted.
 * Same position as Clip_StartGrain: (int)(unit * spu) - 8, clamped at 0. docs/map-319.md.
 * The request ring (provider +0x47F8, 128 entries, write +0x4FFC, read +0x4FF8) has no overflow check
 * and pcmStreamer drains it between card jobs, so re-touches (again = 1) skip while 32 are waiting.
 * Duplicates are harmless: its handler (FUN_080740B4) only refreshes a block that is already cached. */
#define RQ_WRITE 0x4FFC
#define RQ_READ  0x4FF8
#define RQ_BUSY  32

static void cue_prefetch(void *clip, unsigned unit, int again)
{
    void *ref = *(void **)at(clip, 0x24);
    float spu = *(const float *)at(clip, 0x1FC);
    void *prov;
    int pos;
    if (!ref || !(spu > 0.0f) || !(prov = *(void **)at(ref, 0x20)))
        return;
    if (again && u32(prov, RQ_WRITE) - u32(prov, RQ_READ) >= RQ_BUSY)
        return;
    pos = (int)((float)unit * spu) + grid_unpacked(u32(clip, 0x18))->ofs - 8;
    SAMPLE_SEEK(ref, pos < 0 ? 0 : pos);
}

/* Clip_StartGrain at 0x080656E2 (hook_grain_ofs): the grain's first frame, (int)(unit * spu). The
 * pad's grid offset moves the whole unit grid against the audio, so the beat grid can sit on the
 * first downbeat. Clamped to the file. */
int deck_grain_start(void *clip, int frame)
{
    const DkPad *p;
    int ofs, last;
    if (!live() || u8(clip, 0x254))
        return frame;
    ofs = grid_unpacked(u32(clip, 0x18))->ofs;
    if (!ofs)
        return frame;
    frame += ofs;
    p = pad_unpacked(u32(clip, 0x18));
    last = (int)((float)p->units * p->spu) - 1;
    if (last > 0 && frame > last)
        frame = last;
    return frame < 0 ? 0 : frame;
}

/* Voice start (FUN_08065908) from the three calls in FUN_08067FA4. Returns the note to pass on, or
 * CUE_SWALLOW to skip the call. A cue note on a playing Clip sets the pending jump (unset cue: nothing
 * happens). On a stopped Clip a set cue launches it there, with note 0 like a pad press. */
unsigned deck_cue_note(void *clip, unsigned note)
{
    DkCue *c;
    unsigned ut, n, k, unit;
    if (!live() || !(n = grid_units(clip, &ut)))
        return note;
    c = clip_cue(clip);
    c->launch = CUE_NONE;
    if (!cue_active(c) || note - c->note >= CUES)
        return note;
    k = note - c->note;
    unit = c->cue[k] == CUE_NONE ? CUE_NONE : to_units(c->cue[k], ut);
    if (unit != CUE_NONE && unit >= n)
        unit = n - 1;
    if (u8(clip, 0x28) && (u8(clip, 0x260) || u8(clip, 0x670))) {
        if (unit != CUE_NONE) {
            c->pending = (unsigned short)unit;
            cue_prefetch(clip, unit, 0);
#ifdef SONG_CUES
            song_drop(clip);   /* a cue note after a scene start wins over the scene's cue */
#endif
        }
        return CUE_SWALLOW;
    }
    if (unit == CUE_NONE)
        return note;
    c->launch = (unsigned short)unit;
    return 0;
}

/* Next-unit compare in the Sync branch (0x08066C46): stock compares against the whole file. Loop on
 * wraps at Loop End (stock then re-syncs, RecalcRates, unit 0, which deck_wrap_unit turns into Loop
 * Start). Loop off or Trigger stops at End. `next` is the unit about to start (r8): past Loop End
 * (a cue jumped there, or Loop End moved behind the playhead) the clip plays on to End, then loops. */
unsigned deck_limit(void *clip, unsigned next)
{
    DkRegion r;
    if (!region(clip, &r))
        return u32(clip, 0x240) + u32(clip, 0x244);
    ST->wrap_clip = (unsigned)clip;
    ST->wrap_unit = r.ls;
    if (u8(clip, 0x1F4) && u8(clip, 0x1F8) != 1 && next <= r.le)
        return r.le;
    return r.e;
}

typedef long long (*ldiv_fn)(long long, long long);
#define LDIV ((ldiv_fn)0x080C8495u)    /* stock __aeabi_ldivmod; the quotient is all we read */

/* Next grain (0x08066CF2). Unit 0 there only comes from a wrap. `tick` is the grain's start tick
 * (the lane render's [sp+0x58], snapped to the unit grid). A pending cue jump is taken on the first
 * grain that starts on a Quant Size (+0x218) boundary, or on the next grain with Quant None. */
unsigned deck_wrap_unit(void *clip, unsigned unit, const long long *tick)
{
    DkCue *c;
    long long q, t;
    unsigned ut, n;
    if (!live())
        return unit;
    if (unit == 0 && ST->wrap_clip == (unsigned)clip)
        unit = ST->wrap_unit;
    ST->wrap_clip = 0;
    c = clip_cue(clip);
#ifdef SONG_CUES
    {
        unsigned at_scene = song_grain(clip, c, tick);    /* a Song scene starts at this grain */
        if (at_scene != CUE_NONE)
            return played(clip, in_loop(clip, at_scene, 1));
    }
#endif
    if (c->pending == CUE_NONE || !(n = grid_units(clip, &ut)))
        return played(clip, in_loop(clip, unit, 0));
    q = *(const long long *)at(clip, 0x218);
    t = *tick;
    if (q > 0 && t - LDIV(t, q) * q != 0) {
        cue_prefetch(clip, c->pending, 1);   /* keep the target cached until the boundary */
        return played(clip, in_loop(clip, unit, 0));
    }
    unit = c->pending < n ? c->pending : n - 1;
    c->pending = CUE_NONE;
    return played(clip, in_loop(clip, unit, 1));
}

/* For bank 2's unit sequence (tools/stretch/bungee/lane.h DeckSeq), which chooses each unit a little
 * before it starts: the unit at `tick` was already chosen when a cue note came in. If `tick` is a
 * Quant Size boundary, the pending jump takes it after all (the sequencer's note for that same
 * boundary arrives a block or so after the choice; without this it waited a whole Quant). The caller
 * decides how late is still that boundary's note. Returns the cue's unit and clears the pending
 * jump, or CUE_NONE. */
unsigned deck_cue_late(void *clip, const long long *tick)
{
    DkCue *c;
    long long q, t;
    unsigned ut, n, unit;
    if (!live())
        return CUE_NONE;
    c = clip_cue(clip);
    if (c->pending == CUE_NONE || !(n = grid_units(clip, &ut)))
        return CUE_NONE;
    q = *(const long long *)at(clip, 0x218);
    t = *tick;
    if (q <= 0 || t - LDIV(t, q) * q != 0)
        return CUE_NONE;
    unit = c->pending < n ? c->pending : n - 1;
    c->pending = CUE_NONE;
    return played(clip, in_loop(clip, unit, 1));
}

/* ---- Beat Count Auto from a BPM in the file name (engine task, Clip_RecalcRates) ---- */

typedef int (*ref_int_fn)(void *);
#define SAMPLE_FRAMES ((ref_int_fn)0x080636F1u)   /* sample ref -> length in frames */
#define SAMPLE_RATE   ((ref_int_fn)0x0806377Du)   /* sample ref -> sample rate */
#define BPM_MIN       40
#define BPM_MAX       300

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int digit(int c)
{
    return c >= '0' && c <= '9';
}

/* BPM x 100 from a file name: "128bpm", "128 BPM", "Loop_174.5_bpm", "BPM 90", "bpm128". Only the part
 * after the last '/' or '\'. 0 if there is none or it is outside 40..300. */
unsigned deck_name_bpm(const char *path)
{
    const char *name = path, *cur, *p, *q;
    unsigned whole, frac, scale;
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    for (cur = name; *cur; cur++) {
        if (lower(cur[0]) != 'b' || lower(cur[1]) != 'p' || lower(cur[2]) != 'm')
            continue;
        p = cur;
        /* number before: skip separators back, then digits[.digits] */
        q = p;
        while (q > name && (q[-1] == ' ' || q[-1] == '_' || q[-1] == '-'))
            q--;
        if (q > name && digit(q[-1])) {
            const char *e = q;
            while (q > name && (digit(q[-1]) || q[-1] == '.'))
                q--;
            p = q;
            q = e;
        } else {
            /* number after: "bpm128", "BPM 90" */
            q = p + 3;
            while (*q == ' ' || *q == '_' || *q == '-')
                q++;
            if (!digit(*q))
                continue;
            p = q;
            while (digit(*q) || *q == '.')
                q++;
        }
        whole = 0;
        frac = 0;
        scale = 0;
        for (; p < q; p++) {
            if (*p == '.') {
                if (scale)
                    break;
                scale = 1;
            } else if (!scale) {
                whole = whole * 10 + (*p - '0');
                if (whole > 1000)
                    break;
            } else if (scale < 100) {
                frac = frac * 10 + (*p - '0');
                scale *= 10;
            }
        }
        if (scale == 10)
            frac *= 10;
        if (whole >= BPM_MIN && whole <= BPM_MAX && !(whole == BPM_MAX && frac))
            return whole * 100 + frac;
    }
    return 0;
}

/* Clip_RecalcRates at 0x08064BA2 (replaces `ldr r3, [r4, #0xB80]`): the beat count stock uses for the
 * clip's length, 0 = Auto (stock's power-of-two guess). An explicit Beat Count wins. On Auto, a BPM in
 * the file name gives round(seconds x BPM / 60) beats. */
unsigned deck_auto_beats(void *clip)
{
    unsigned beats = u32(clip, 0xB80), bpm;
    void *ref, *mgr;
    const char *name;
    int frames, rate;
    if (beats || !live())
        return beats;
    ref = *(void **)at(clip, 0x24);
    if (!ref || !(mgr = *(void **)at(ref, 0x20)))
        return 0;
    name = (const char *)(u32(mgr, 0x4348) + u32(ref, 0x24) * 0x170 + 0x5C);
    if (u32(ref, 0x24) >= 0x240 || !(bpm = deck_name_bpm(name)))
        return 0;
    frames = SAMPLE_FRAMES(ref);
    rate = SAMPLE_RATE(ref);
    if (frames <= 0 || rate <= 0)
        return 0;
    /* beats = frames / rate * bpm / 60, bpm in hundredths; 64-bit, rounded */
    beats = (unsigned)LDIV((long long)frames * bpm + (long long)rate * 3000, (long long)rate * 6000);
    return beats;
}

/* ---- Info list (UI task): our position params read as bar.beat.16th ---- */

typedef int (*snprintf_fn)(char *, unsigned, const char *, ...);
#define SNPRINTF     ((snprintf_fn)0x080C89F5u)

/* 0x080A9218 in the Info list row's value draw: snprintf(buf, 0x1F, "%d", value) for number params.
 * row = the list row (r4 there): id at +0x94. Cues read "Off" or bar.beat, positions "17.1.1". */
/* Beat Count (0xD2): stock registers a list of 513 labels, Auto and 1..512 (0x0808D708). Registered
 * here as a number 0..1024 instead: the same values (0 = Auto, else beats), so presets and the engine
 * (clip+0xB80, beats x 960 ticks, no cap) are unchanged, and no 1025-label table is needed.
 * deck_list_value prints 0 as "Auto". Runs at boot, before deck_init. */
#define ID_BEATS  0xD2
#define BEATS_MAX 1024

void deck_reg_beats(void *reg, unsigned id, const char *title, const void *labels, unsigned count,
                    const char *xml)
{
    (void)labels;
    (void)count;
    PARAM_NUMBER(reg, id, 1, title, 0, BEATS_MAX, xml);
}

int deck_list_value(char *buf, unsigned size, const char *fmt, int value, const void *row)
{
    unsigned id = *(const unsigned short *)((const char *)row + 0x94);
    if (id == ID_BEATS && value <= 0)
        return SNPRINTF(buf, size, "Auto");
    if (live() && id - ID_CUE1 < CUES) {
        if (value <= 0)
            return SNPRINTF(buf, size, "Off");
        value -= 1;
        return SNPRINTF(buf, size, "%d.%d", value / 4 + 1, value % 4 + 1);   /* bar.beat */
    }
    if (live() && id - ID_START < 4) {
        if ((id == ID_START + 1 || id == ID_START + 3) && (value <= 0 || value >= (int)POS_END))
            return SNPRINTF(buf, size, "end");
        return SNPRINTF(buf, size, "%d.%d.%d", value / 16 + 1, value % 16 / 4 + 1, value % 4 + 1);
    }
    return SNPRINTF(buf, size, fmt, value);
}

/* ---- Keys screen (UI task): a Clip in Cue Mode shows stock's Slicer key layout as 8 keys A-H ---- */

typedef int (*cell_get_fn)(void *, void *, unsigned, unsigned *);
typedef void (*view_fn2)(void *);
#define APP          ((void *)0x24020088u)
#define CELL_GET     ((cell_get_fn)0x08099505u)  /* (app, cell ref, id, &value) -> found */
#define KEYS_REFRESH ((view_fn2)0x080AD979u)     /* shows the Slicer or chromatic keyboard */
#define K_CELL       0x1C2C                      /* view: cell ref of the pad shown */
#define K_KEY        0x1C30                      /* view: its UI key (row << 4 | col) */
#define K_SLICER     0x4550                      /* view: 1 = Slicer layout */
#define NOTE_STOP    35                          /* the Slicer layout's STOP button */
#define NOTE_KEY1    36                          /* first Slicer key */

static const char s_cues[] = "CUES";

static DkCue *cue_packed(unsigned key)
{
    return &ST->cue[((key >> 4) & 3) << 2 | (key & 3)];
}

/* Pad selected on the Keys screen (0x080AE036): stock just set K_SLICER = (cellmode == 2). */
void deck_keys_refresh(void *view)
{
    unsigned mode = 0, key = *(const unsigned short *)at(view, K_KEY);
    if (live()) {
        CELL_GET(APP, at(view, K_CELL), 0x8B, &mode);
        if (mode == 1 && cue_active(cue_packed(key))) {
            *(unsigned char *)at(view, K_SLICER) = 1;
            ST->ui.cue_keys = key;
            ST->ui.keys_on = 1;
        } else {
            ST->ui.cue_keys = 0xFFFF;
            ST->ui.keys_on = 0;
        }
    }
    KEYS_REFRESH(view);
}

/* Slicer key label (0x080AD768): stock prints note - 35 = 1..32. */
int deck_keys_label(char *buf, unsigned size, const char *fmt, int n)
{
    if (live() && ST->ui.keys_on && n >= 1 && n <= CUES && size >= 2) {
        buf[0] = (char)('A' + n - 1);
        buf[1] = 0;
        return 1;
    }
    return SNPRINTF(buf, size, fmt, n);
}

/* Root button label of the Slicer layout (0x080AD99C): "SLICE" -> "CUES". */
void deck_keys_root(void *label, const char *text)
{
    BTN_TEXT(label, live() && ST->ui.keys_on ? s_cues : text);
}

/* Key draw (0x080AD728: bl FUN_080ba3fc(note), "in the scale"): the key fills highlighted when that
 * returns non-zero and key+0x35 is set. On cue keys a set cue is highlighted (an "on" toggle); every
 * layout rebuild rewrites +0x35, so stock layouts are unaffected. */
typedef int (*scale_fn)(unsigned);
#define IN_SCALE ((scale_fn)0x080BA3FDu)

int deck_key_lit(unsigned note, unsigned char *key)
{
    if (live() && ST->ui.keys_on) {
        int lit = note - NOTE_KEY1 < CUES && cue_packed(ST->ui.cue_keys)->cue[note - NOTE_KEY1] != CUE_NONE;
        key[0x35] = (unsigned char)lit;
        return lit;
    }
    return IN_SCALE(note);
}

/* Keys note on / off (0x080A0AA2 / 0x0809B78C), engine key row << 8 | col. For the pad shown as cue
 * keys: keys 36..43 play Cue Note + 0..7, STOP (35) is dropped. Returns the note or CUE_SWALLOW. */
unsigned deck_keys_note(unsigned key, unsigned note)
{
    unsigned packed = ((key >> 8) & 0xF) << 4 | (key & 0xF);
    if (!live() || !ST->ui.keys_on || packed != ST->ui.cue_keys)
        return note;
    if (note == NOTE_STOP)
        return CUE_SWALLOW;
    if (note - NOTE_KEY1 < CUES)
        return cue_packed(packed)->note + (note - NOTE_KEY1);
    return note;
}

/* ---- waveform screen (UI task) ----
 *
 * Clip pads show the Slicer bottom bar. Its "..." opens the stock edit row with three tabs,
 * Cue | Loop | Grid. A tab opens a popup at the place of Slicer's Scan / Grid box {20, 50, 280, 40}:
 *   Cue   A B C D E F G H     lit = set: tap plays it; dark: tap stores the playhead there.
 *                             Hold Cue + tap a letter: overwrite; tap it again while holding: clear.
 *   Loop  On | Instant | Nudge
 *   Grid  Lines | < 1 | 1 > | Raw
 * "..." closes an open popup, then (stock) the row. All four knobs are ours (titles in bar.beat.16th);
 * the bottom left is the Grid size on every layer:
 *   main  Start, End, Grid, -        Cue   select, position, Grid, swap
 *   Loop  Loop Start, Loop End, size x2 / /2, move      Grid  nudge offset, -, Grid, -
 * Start, End, Loop and cues move by the Grid size (at least the clip's Sync unit). Raw = turns x 8.
 * The row has 4 stock buttons and 5 spares; three more stock buttons live in SRAM4 (DkState.btn).
 */

typedef void (*knob_fn)(void *, int);
typedef void (*rect_fn)(void *, const int *);
typedef void (*add_btn_fn)(void *, const char *, unsigned);
typedef void (*latch_fn)(void *, unsigned);
typedef void (*note_fn)(void *, unsigned, unsigned, unsigned, unsigned);
typedef void (*add_child_fn)(void *, void *);
typedef void (*set_id_fn)(void *, unsigned);
#define KNOB_TURN   ((knob_fn)0x080B0D55u)     /* (knob, encoder delta) */
#define KNOB_RESET  ((view_fn)0x080B04C9u)     /* blank knob: no param, no value */
#define KNOB_SHOW   ((view_fn)0x080B0481u)     /* copy the title (knob+0x75) into the shown label (knob+0xF4) */
#define BTN_RECT    ((rect_fn)0x080A3759u)     /* button vt+0x20: x, y, w, h */
#define BTN_CTOR    ((view_fn)0x080A3D35u)     /* stock button constructor (0xA0 bytes) */
#define ROW_ADD     ((add_btn_fn)0x080C4FF1u)  /* (row, label, id): one of its 5 spare buttons; a 6th hangs */
#define ADD_CHILD   ((add_child_fn)0x080AEC45u) /* append to a container (tail: drawn first, hit last) */
#define BTN_LATCH   ((latch_fn)0x080A3773u)
#define NOTE_ON_FN  ((note_fn)0x0804C61Du)     /* (engine, key, note, velocity, flag): msg 0x42 */
#define NOTE_OFF_FN ((note_fn)0x0804C5D5u)     /* msg 0x43 */
#define ENGINE      ((void *)0x2400A9C0u)
#define UI_TICK     (*(volatile const unsigned *)0x2402EEE0u)   /* osKernelSysTick, ms */

#define V_BAG       0x2370
#define V_PAD       0x29C
#define V_KNOB_TL   0x670      /* encoder 0, top left on the device */
#define V_KNOB_TR   0xC30      /* encoder 2, top right */
#define V_KNOB_BL   0x950      /* encoder 1, bottom left */
#define V_KNOB_BR   0xF10      /* encoder 3, bottom right */
#define V_KNOBS     0x47C8
#define V_BTN_EDIT  0x1CE8     /* slicer bar's "..." toggle, id 0x4C */
#define V_ROW       0x2F8      /* edit row: Grid / Scan / Join / Split on Slicer */
#define V_STRIP     0x2168     /* overview strip the row replaces */
#define V_WAVE      0x38       /* waveform widget */
#define ID_DOTS     0x4C
#define ID_SPARE    0x1C0      /* our buttons: 0x1C0..0x1C4 row spares, 0x1C5..0x1C7 SRAM4 */
#define ROW_SPARE   0x3C       /* row: spare buttons, 0xA0 each */
#define ROW_NSPARE  0x38       /* row: u16 spare buttons added */
#define BTN_SIZE    0xA0
#define BTN_TOUCHES 0x98       /* touches held on a button */
#define EVT_COOKIE  (*(const unsigned *)0x080A6D20u)
#define STEP_COUNTS 400        /* encoder counts per step, like stock list knobs */
#define RAW_MUL     8
#define NBTN        12
#define FRAME       11         /* popup background: last child, so drawn first and hit last */
#define BOX_X       20         /* Slicer's Scan / Grid popup box */
#define BOX_Y       50
#define BOX_W       280
#define BOX_H       40
#define NUDGE       48         /* grid offset step, samples (1 ms at 48 kHz) */
#define PRESS_HOLD  0x10000    /* press_id flags: the press was a hold-Cue edit (release does nothing) */
#define PRESS_NOTE  0x20000    /*   a note-on went out (release sends the note-off) */

enum { POP_NONE, POP_CUE, POP_LOOP, POP_GRID };
enum { LAY_STOCK = 1, LAY_CLIP };          /* + popup << 4 */

static const unsigned short stock_btn[4] = {0x20C8, 0x2028, 0x1EE8, 0x1F88};   /* Split Join Scan Grid */
static const unsigned short stock_id[4] = {0x45, 0x46, 0x47, 0x48};
static const unsigned stock_text[4] = {0x080CF428u, 0x080CF420u, 0x080CF418u, 0x080CF410u};
static const unsigned char pop_btn[CUES] = {3, 4, 5, 6, 7, 8, 9, 10};

static const char s_tab[3][5] = {"Cue", "Loop", "Grid"};
static const char s_loopb[3][8] = {"On", "Instant", "Nudge"};
static const char s_gridb[4][6] = {"Lines", "< 1", "1 >", "Raw"};
static const char s_letter[CUES][2] = {"A", "B", "C", "D", "E", "F", "G", "H"};
static const char s_blank[] = "";

static int bag_get(void *view, unsigned id)
{
    return PARAM_GET(at(view, V_BAG), id);
}

static unsigned view_key(void *view)
{
    return *(const unsigned short *)at(view, V_PAD);
}

static int is_clip(void *view)
{
    return bag_get(view, 0x8B) == 1;
}

static DkGrid *grid_packed(unsigned key)
{
    return &ST->grid[((key >> 4) & 3) << 2 | (key & 3)];
}

/* Same shape as the knob-change post (0x3D -> 0x6E): the app sets the bag, the engine (msg 0x39) and
 * preset.xml, and hook_post copies the deck ids to SRAM4. */
static void post_param(void *view, unsigned id, int value)
{
    unsigned ev[6];
    ev[0] = 0x6E;
    ev[1] = EVT_COOKIE;
    ev[2] = view_key(view);
    ev[3] = id;
    ev[4] = (unsigned)value;
    ev[5] = 0;
    UI_POST_UP(view, ev);
}

static void *row_btn(void *view, unsigned i)   /* 0..3 stock, 4..8 row spares, 9..11 SRAM4 */
{
    if (i < 4)
        return at(view, stock_btn[i]);
    if (i < 9)
        return at(view, V_ROW + ROW_SPARE + (i - 4) * BTN_SIZE);
    return ST->btn[i - 9];
}

static int btn_index(unsigned id)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        if (id == stock_id[i])
            return (int)i;
    if (id - ID_SPARE < 8)
        return (int)(id - ID_SPARE) + 4;
    return -1;
}

static void set_rect(void *btn, int x, int y, int w, int h)
{
    int r[4];
    r[0] = x;
    r[1] = y;
    r[2] = w;
    r[3] = h;
    BTN_RECT(btn, r);
}

/* Once per view: the row's five spares (row+0x38 counts them; stock adds none), then three stock
 * buttons built in SRAM4 and appended to the row. */
static int row_build(void *view)
{
    void *row = at(view, V_ROW);
    unsigned n = *(const unsigned short *)at(row, ROW_NSPARE), i;
    for (; n < 5; n++) {
        ROW_ADD(row, s_blank, ID_SPARE + n);
        BTN_LATCH(row_btn(view, n + 4), 1);
    }
    if (ST->ui.btn_view == view)
        return 1;
    if (ST->ui.btn_view)
        return 0;          /* another view owns them: only one waveform view is expected */
    for (i = 0; i < 3; i++) {
        void *b = ST->btn[i];
        BTN_CTOR(b);
        ((set_id_fn)(*(unsigned **)b)[9])(b, ID_SPARE + 5 + i);
        BTN_LATCH(b, 1);
        ADD_CHILD(row, b);
    }
    ST->ui.btn_view = view;
    return 1;
}

static unsigned pop_items(unsigned popup)
{
    return popup == POP_CUE ? CUES : popup == POP_LOOP ? 3 : popup == POP_GRID ? 4 : 0;
}

static void row_layout(void *view, unsigned lay)
{
    const char *row = (const char *)at(view, V_ROW);
    int rx = (int)u32(row, 4), ry = (int)u32(row, 8), rw = (int)u32(row, 0xC), rh = (int)u32(row, 0x10);
    unsigned i, popup = lay >> 4, n = pop_items(popup), have = row_build(view) ? NBTN : 9;
    for (i = 0; i < have; i++)
        HIDE(row_btn(view, i), 1);
    if ((lay & 0xF) == LAY_STOCK) {
        for (i = 0; i < 4; i++) {
            set_rect(row_btn(view, i), rx + (int)i * rw / 4, ry, rw / 4 - 1, rh);
            BTN_TEXT(row_btn(view, i), (const char *)stock_text[i]);
            BTN_STATE(row_btn(view, i), 0);
            HIDE(row_btn(view, i), 0);
        }
        return;
    }
    for (i = 0; i < 3; i++) {
        set_rect(row_btn(view, i), rx + (int)i * rw / 3, ry, rw / 3 - 1, rh);
        BTN_TEXT(row_btn(view, i), s_tab[i]);
        HIDE(row_btn(view, i), 0);
    }
    if (!n || have < NBTN)
        return;
    set_rect(row_btn(view, FRAME), BOX_X, BOX_Y, BOX_W, BOX_H);
    BTN_TEXT(row_btn(view, FRAME), s_blank);
    BTN_STATE(row_btn(view, FRAME), 0);
    HIDE(row_btn(view, FRAME), 0);
    for (i = 0; i < n; i++) {
        int w = (BOX_W - 8 - 2 * ((int)n - 1)) / (int)n;
        void *b = row_btn(view, pop_btn[i]);
        set_rect(b, BOX_X + 4 + (int)i * (w + 2), BOX_Y + 4, w, BOX_H - 8);
        BTN_TEXT(b, popup == POP_CUE ? s_letter[i] : popup == POP_LOOP ? s_loopb[i] : s_gridb[i]);
        HIDE(b, 0);
    }
}

static void row_lights(void *view)
{
    unsigned i, popup = ST->ui.popup;
    if ((ST->ui.layout & 0xF) != LAY_CLIP)
        return;
    for (i = 0; i < 3; i++)
        BTN_STATE(row_btn(view, i), popup == i + 1);
    if (ST->ui.btn_view != view)
        return;
    if (popup == POP_CUE) {
        for (i = 0; i < CUES; i++)
            BTN_STATE(row_btn(view, pop_btn[i]), bag_get(view, ID_CUE1 + i) != 0);
    } else if (popup == POP_LOOP) {
        BTN_STATE(row_btn(view, pop_btn[0]), bag_get(view, ID_LOOPMODE) != 0);
        BTN_STATE(row_btn(view, pop_btn[1]), 0);
        BTN_STATE(row_btn(view, pop_btn[2]), 0);
    } else if (popup == POP_GRID) {
        BTN_STATE(row_btn(view, pop_btn[0]), ST->ui.grid);
        BTN_STATE(row_btn(view, pop_btn[1]), 0);
        BTN_STATE(row_btn(view, pop_btn[2]), 0);
        BTN_STATE(row_btn(view, pop_btn[3]), ST->ui.raw);
    }
}

static void row_apply(void *view, unsigned mode)
{
    unsigned lay = mode == 1 ? (unsigned)LAY_CLIP | ST->ui.popup << 4 : LAY_STOCK;
    if (lay != ST->ui.layout) {
        row_layout(view, lay);
        ST->ui.layout = (unsigned char)lay;
    }
    row_lights(view);
}

/* ---- positions (1/16 notes) ---- */

typedef struct { int total, s, e, ls, le, g, sync; } Pos;

static int grid_min(const DkPad *p)
{
    int sync = p->unit_ticks ? p->unit_ticks / TICKS_S16 : 1, i;
    for (i = 0; i < GRID_SIZES - 1; i++)
        if (grid_s16[i] >= sync)
            return i;
    return GRID_SIZES - 1;
}

static int grid_index(void *view)
{
    const DkPad *p = pad_packed(view_key(view));
    int i = bag_get(view, ID_GRID), lo = grid_min(p);
    if (i < 0 || i >= GRID_SIZES || !bag_has(at(view, V_BAG), ID_GRID))
        i = GRID_DEF;   /* a pad saved before Grid existed has no row: 1 bar, not 1/16 */
    return i < lo ? lo : i;
}

static void pos_of(void *view, Pos *b)
{
    DkPad *p = pad_packed(view_key(view));
    int total = p->unit_ticks ? (int)total_s16(p->units, p->unit_ticks) : (int)POS_MAX;
    if (total <= 0 || total > (int)POS_MAX)
        total = POS_MAX;
    b->total = total;
    b->g = grid_s16[grid_index(view)];
    b->s = bag_get(view, ID_START + 0);
    b->e = bag_get(view, ID_START + 1);
    b->ls = bag_get(view, ID_START + 2);
    b->le = bag_get(view, ID_START + 3);
    if (b->e <= 0 || b->e > total)
        b->e = total;
    if (b->s >= b->e)
        b->s = b->e - 1;
    if (b->s < 0)
        b->s = 0;
    if (b->ls < b->s)
        b->ls = b->s;
    if (b->le <= 0 || b->le > b->e)
        b->le = b->e;
    if (b->le <= b->ls) {
        b->ls = b->s;
        b->le = b->e;
    }
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* End / Loop End: at the file's end it is stored as POS_END, so it follows the file. */
static int end_value(const Pos *b, int v)
{
    return v >= b->total ? (int)POS_END : v;
}

/* Playhead in 1/16 notes: the unit the engine started last (where it stopped, once stopped); -1 if none. */
static int playhead(void *view, const Pos *b)
{
    const DkPad *p = pad_packed(view_key(view));
    unsigned u = cue_packed(view_key(view))->play;
    if (u == CUE_NONE || !p->unit_ticks)
        return -1;
    return clampi((int)(u * p->unit_ticks / TICKS_S16), 0, b->total - 1);
}

/* The white marker in the middle of the waveform: the line at wave+0x23C, which is the screen centre
 * (wave+0x240) except near the file's ends. Its sample, less the grid offset, to the nearest Sync unit,
 * in 1/16 notes. -1 without a unit grid. */
static int marker_s16(void *view, const Pos *b)
{
    const char *wave = (const char *)at(view, V_WAVE);
    const DkPad *p = pad_packed(view_key(view));
    float spp = *(const float *)(wave + 0x168), pos;
    int unit;
    if (!p->unit_ticks || !(p->spu > 0.0f))
        return -1;
    pos = (float)(int)u32(wave, 0x244) + (float)((int)u32(wave, 0x23C) - (int)u32(wave, 0x240)) * spp
          - (float)grid_packed(view_key(view))->ofs;
    unit = (int)(pos / p->spu + 0.5f);
    if (unit < 0)
        unit = 0;
    return clampi((int)((unsigned)unit * p->unit_ticks / TICKS_S16), 0, b->total - 1);
}

/* Waveform zoom: the widget applies a target samples-per-pixel at wave+0x24C on its next tick
 * (FUN_080C6A78, as stock's pinch does), around the white marker. sqrt 2 per step, 1 .. 4096. */
#define ZOOM_MAX 4096.0f

static float zoom_target(void *view)
{
    const char *wave = (const char *)at(view, V_WAVE);
    float z = *(const float *)(wave + 0x24C);
    return z > 0.0f ? z : *(const float *)(wave + 0x168);
}

static void zoom(void *view, int n)
{
    float z = zoom_target(view);
    for (; n > 0; n--)
        z *= 0.70710678f;               /* turned right: zoom in */
    for (; n < 0; n++)
        z *= 1.41421356f;
    *(float *)at(view, V_WAVE + 0x24C) = z < 1.0f ? 1.0f : z > ZOOM_MAX ? ZOOM_MAX : z;
}

/* The screen's width in 1/16 notes at the zoom being shown, for the knob title; 0 without a unit grid. */
static int zoom_s16(void *view)
{
    const DkPad *p = pad_packed(view_key(view));
    float w = (float)(int)u32(at(view, V_WAVE), 0xC), s16;
    if (!p->unit_ticks || !(p->spu > 0.0f))
        return 0;
    s16 = w * zoom_target(view) * (float)p->unit_ticks / (p->spu * (float)TICKS_S16);
    return s16 < 1.0f ? 1 : (int)(s16 + 0.5f);
}

/* ---- titles ---- */

static void fmt_pos(char *buf, unsigned size, const char *name, int s16, int is_end)
{
    if (is_end)
        SNPRINTF(buf, size, "%s end", name);
    else if (s16 < 0)
        SNPRINTF(buf, size, "%s Off", name);
    else
        SNPRINTF(buf, size, "%s %d.%d.%d", name, s16 / 16 + 1, s16 % 16 / 4 + 1, s16 % 4 + 1);
}

static void fmt_len(char *buf, unsigned size, const char *name, int s16)
{
    static const char *const frac[9] = {0, "1/16", "1/8", 0, "1/4", 0, 0, 0, "1/2"};
    if (s16 >= 16 && s16 % 16 == 0)
        SNPRINTF(buf, size, s16 == 16 ? "%s %d bar" : "%s %d bars", name, s16 / 16);
    else if (s16 <= 8 && frac[s16])
        SNPRINTF(buf, size, "%s %s", name, frac[s16]);
    else
        SNPRINTF(buf, size, "%s %d/16", name, s16);
}

static unsigned hash(const char *t)
{
    unsigned h = 2166136261u;
    while (*t)
        h = (h ^ (unsigned char)*t++) * 16777619u;
    return h | 1;
}

static const unsigned short knob_off[4] = {V_KNOB_TL, V_KNOB_TR, V_KNOB_BL, V_KNOB_BR};

static void set_title(void *view, unsigned k, const char *t, int force)
{
    unsigned h = hash(t);
    if (!force && ST->ui.title[k] == h)
        return;
    ST->ui.title[k] = h;
    KNOB_LABEL(at(view, knob_off[k]), t);
    KNOB_SHOW(at(view, knob_off[k]));   /* KNOB_LABEL alone only changes the source text: the
                                         * screen showed the previous layer's titles (device report) */
}

static void knob_titles(void *view, int force)
{
    char t[4][24];
    Pos b;
    unsigned popup = ST->ui.popup, sel = ST->ui.sel, k;
    int v;
    pos_of(view, &b);
    SNPRINTF(t[2], 24, "Grid %s", grid_names[grid_index(view)]);
    t[3][0] = 0;
    if (popup == POP_CUE) {
        SNPRINTF(t[0], 24, "Cue %s", s_letter[sel]);
        v = bag_get(view, ID_CUE1 + sel);
        fmt_pos(t[1], 24, "Pos", v > 0 ? (v - 1) * 4 : -1, 0);
        SNPRINTF(t[3], 24, "Swap");
    } else if (popup == POP_LOOP) {
        fmt_pos(t[0], 24, "LpS", b.ls, 0);
        fmt_pos(t[1], 24, "LpE", b.le, b.le >= b.total);
        fmt_len(t[2], 24, "Size", b.le - b.ls);
        SNPRINTF(t[3], 24, "Move");
    } else if (popup == POP_GRID) {
        v = grid_packed(view_key(view))->ofs;
        SNPRINTF(t[0], 24, "Nudge %s%dms", v < 0 ? "-" : "+", (v < 0 ? -v : v) / NUDGE);
        t[1][0] = 0;
    } else {
        fmt_pos(t[0], 24, "Str", b.s, 0);
        fmt_pos(t[1], 24, "End", b.e, b.e >= b.total);
    }
    if (popup == POP_NONE || popup == POP_GRID) {
        v = zoom_s16(view);   /* the screen's width: bars, else beats */
        if (v >= 16)
            SNPRINTF(t[3], 24, (v + 8) / 16 == 1 ? "Zoom %d bar" : "Zoom %d bars", (v + 8) / 16);
        else if (v)
            SNPRINTF(t[3], 24, (v + 2) / 4 <= 1 ? "Zoom 1 beat" : "Zoom %d beats", (v + 2) / 4);
        else
            SNPRINTF(t[3], 24, "Zoom");
    }
    for (k = 0; k < 4; k++)
        set_title(view, k, t[k], force);
}

static void knobs_bind(void *view)
{
    unsigned k;
    for (k = 0; k < 4; k++)
        KNOB_RESET(at(view, knob_off[k]));
    knob_titles(view, 1);
    KNOB_COUNT(at(view, V_KNOBS), 4);
}

/* Clip refresh branch (0x080A583C): stock cleared all four knobs. */
void deck_ui_bind(void *view)
{
    if (live())
        knobs_bind(view);
    VIEW_ZOOM(view);
}

static void set_popup(void *view, unsigned popup)
{
    ST->ui.popup = (unsigned char)popup;
    knobs_bind(view);
    row_apply(view, 1);
}

/* ---- edits ---- */

static void cue_mode_on(void *view)
{
    if (!bag_get(view, ID_CUEMODE))
        post_param(view, ID_CUEMODE, 1);
}

/* A cue at the white marker, to the nearest beat (device report: the playhead-based store looked
 * random). Cues are whole beats so the Pos list can set them too. */
static void cue_store(void *view, unsigned k)
{
    Pos b;
    int at_marker;
    pos_of(view, &b);
    at_marker = marker_s16(view, &b);
    if (at_marker < 0)
        return;
    post_param(view, ID_CUE1 + k, clampi((at_marker + 2) / 4, 0, CUE_BEATS - 1) + 1);   /* nearest beat */
    cue_mode_on(view);
    ST->ui.sel = (unsigned char)k;
}

static void cue_note(void *view, unsigned k, unsigned on)
{
    unsigned key = view_key(view);
    unsigned ekey = ((key >> 8) & 0x1F) << 16 | ((key >> 4) & 0xF) << 8 | (key & 0xF);
    unsigned note = cue_packed(key)->note + k;
    if (on)
        NOTE_ON_FN(ENGINE, ekey, note, 0, 0);
    else
        NOTE_OFF_FN(ENGINE, ekey, note, 0, 0);
}

static void cue_swap(void *view, int dir)
{
    int a = ST->ui.sel, b = a + dir, va, vb;
    if (b < 0 || b >= CUES)
        return;
    va = bag_get(view, ID_CUE1 + a);
    vb = bag_get(view, ID_CUE1 + b);
    post_param(view, ID_CUE1 + a, vb);
    post_param(view, ID_CUE1 + b, va);
    ST->ui.sel = (unsigned char)b;
}

static void loop_set(void *view, const Pos *b, int ls, int le)
{
    post_param(view, ID_START + 2, ls);
    post_param(view, ID_START + 3, end_value(b, le));
}

static void loop_move(void *view, int d)
{
    Pos b;
    int len, ls;
    pos_of(view, &b);
    len = b.le - b.ls;
    ls = clampi(b.ls + d, b.s, b.e - len);
    if (ls != b.ls)
        loop_set(view, &b, ls, ls + len);
}

static void loop_instant(void *view)
{
    Pos b;
    int len, ls;
    pos_of(view, &b);
    len = (b.ls == b.s && b.le == b.e) ? 16 : b.le - b.ls;
    ls = marker_s16(view, &b);
    if (ls < 0)
        return;
    ls = clampi((ls + 8) / 16 * 16, b.s, b.e - 1);   /* the nearest bar line (rekordbox / Traktor quantize) */
    loop_set(view, &b, ls, ls + len < b.e ? ls + len : b.e);
    if (!bag_get(view, ID_LOOPMODE))
        post_param(view, ID_LOOPMODE, 1);
}

/* Samples per grid step: Grid ticks x samples per tick (spu / unit ticks). */
static int grid_samples(void *view, int g)
{
    const DkPad *p = pad_packed(view_key(view));
    if (!p->unit_ticks || !(p->spu > 0.0f))
        return 0;
    return (int)((float)(g * TICKS_S16) * p->spu / (float)p->unit_ticks);
}

static void grid_ofs(void *view, int d)
{
    int o = grid_packed(view_key(view))->ofs + d;
    post_param(view, ID_GRIDOFS, clampi(o, -OFS_MAX, OFS_MAX));
}

/* ---- encoders (0x080A6E70: bl FUN_080b0d54(knob, delta), view in r4 via hook_knob) ---- */

static int steps_of(unsigned which, int delta)
{
    int *acc = &ST->ui.acc[which & 3], n;
    *acc += delta;
    n = *acc / STEP_COUNTS;
    *acc -= n * STEP_COUNTS;
    return n;
}

static void turn(void *view, unsigned k, int n)
{
    unsigned popup = ST->ui.popup;
    Pos b;
    int v, i;
    pos_of(view, &b);
    if (k == 2 && popup != POP_LOOP) {                     /* Grid size, every layer but Loop */
        i = clampi(grid_index(view) + n, grid_min(pad_packed(view_key(view))), GRID_SIZES - 1);
        post_param(view, ID_GRID, i);
        return;
    }
    if (popup == POP_CUE) {
        if (k == 0) {
            ST->ui.sel = (unsigned char)clampi((int)ST->ui.sel + n, 0, CUES - 1);
        } else if (k == 1) {
            /* move by the Grid (whole beats); turned left past the start it clears the cue ("Pos Off") */
            v = bag_get(view, ID_CUE1 + ST->ui.sel);
            if (v) {
                int step = b.g >= 4 ? b.g / 4 : 1, to = v - 1 + n * step;
                post_param(view, ID_CUE1 + ST->ui.sel,
                           to < 0 ? 0 : clampi(to, 0, clampi(b.total / 4, 1, CUE_BEATS) - 1) + 1);
            }
        } else if (k == 3) {
            cue_swap(view, n > 0 ? 1 : -1);
        }
    } else if (popup == POP_LOOP) {
        if (k == 0) {
            post_param(view, ID_START + 2, clampi(b.ls + n * b.g, b.s, b.le - 1));
        } else if (k == 1) {
            post_param(view, ID_START + 3, end_value(&b, clampi(b.le + n * b.g, b.ls + 1, b.e)));
        } else if (k == 2) {
            int ls = b.ls, ph = playhead(view, &b);
            v = b.le - b.ls;
            for (; n > 0; n--)
                v = v * 2 <= 1024 && b.ls + v * 2 <= b.e ? v * 2 : v;
            for (; n < 0; n++)
                v = v / 2 >= 1 ? v / 2 : 1;
            /* halving: keep the part the playhead is in, so it cannot fall out of the loop */
            if (ph >= b.ls && ph < b.le && v < b.le - b.ls)
                ls = b.ls + (ph - b.ls) / v * v;
            if (ls != b.ls)
                post_param(view, ID_START + 2, ls);
            post_param(view, ID_START + 3, end_value(&b, ls + v));
        } else {
            loop_move(view, n * b.g);
        }
    } else if (popup == POP_GRID) {
        if (k == 0)
            grid_ofs(view, n * NUDGE);
        else if (k == 3)
            zoom(view, n);
    } else {
        if (k == 0)
            post_param(view, ID_START + 0, clampi(b.s + n * b.g, 0, b.e - 1));
        else if (k == 1)
            post_param(view, ID_START + 1, end_value(&b, clampi(b.e + n * b.g, b.s + 1, b.total)));
        else if (k == 3)
            zoom(view, n);
    }
}

void deck_knob_turn(void *knob, int delta, void *view)
{
    unsigned off = (unsigned)knob - (unsigned)view, k;
    int n;
    if (!live() || !is_clip(view)) {
        KNOB_TURN(knob, delta);
        return;
    }
    if (ST->ui.raw)
        delta *= RAW_MUL;
    for (k = 0; k < 4 && knob_off[k] != off; k++)
        ;
    if (k == 4 || (n = steps_of(k, delta)) == 0)
        return;
    turn(view, k, n);
    knob_titles(view, 0);
}

/* ---- grid markers ---- */

#define WAVE_MARKS 128         /* the widget's marker buffer (wave+0x184): a 0x200-byte cell list */

/* Cue flags first (they get the stock slice-marker flag; the selected one is highlighted through
 * wave+0x25C, 1-based), then grid lines. */
static unsigned cue_marks(void *wave, const DkPad *p, const DkCue *c, int ofs, unsigned *out)
{
    unsigned k, n = 0, sel = 0;
    for (k = 0; k < CUES; k++) {
        int pos;
        if (c->cue[k] == CUE_NONE)
            continue;
        pos = (int)((float)to_units(c->cue[k], p->unit_ticks) * p->spu) + ofs;
        out[n++] = (unsigned)(pos < 0 ? 0 : pos);
        if (k == ST->ui.sel)
            sel = n;
    }
    *(unsigned *)at(wave, 0x25C) = sel;
    return n;
}

/* Lines every `step_units` over the part of the file on screen (and half a screen either side), thinned
 * to every 2nd / 4th / ... line so they stay >= 10 px apart. Appended after n markers, up to cap. */
static unsigned grid_marks(void *wave, const DkPad *p, unsigned step_units, int ofs, unsigned *out,
                           unsigned n, unsigned cap)
{
    float spp = *(const float *)at(wave, 0x168), spu = p->spu, lo, hi, first;
    unsigned step = step_units ? step_units : 1, k;
    int x = (int)u32(wave, 4), w = (int)u32(wave, 0xC), anchor_px = (int)u32(wave, 0x240);
    int anchor = (int)u32(wave, 0x244);
    if (!(spp > 0.0f) || !(spu > 0.0f) || p->units < 2)
        return n;
    while (spu * step / spp < 10.0f && step < p->units)
        step *= 2;
    lo = (float)anchor + (float)(x - w / 2 - anchor_px) * spp - (float)ofs;
    hi = (float)anchor + (float)(x + w + w / 2 - anchor_px) * spp - (float)ofs;
    first = lo > 0.0f ? lo / (spu * step) : 0.0f;
    for (k = (unsigned)first * step; k < p->units && n < cap; k += step) {
        float pos = (float)k * spu;
        if (pos < lo)
            continue;
        if (pos > hi)
            break;
        out[n++] = (unsigned)((int)pos + ofs < 0 ? 0 : (int)pos + ofs);
    }
    return n;
}

/* The waveform's white marker (wave+0x23C; the screen centre except near the file's ends), as a sample
 * of the file less the grid offset. While the clip plays the widget keeps the playhead under it. */
static int cursor_sample(const void *wave, int ofs)
{
    float spp = *(const float *)((const char *)wave + 0x168);
    return (int)((float)(int)u32(wave, 0x244) + (float)((int)u32(wave, 0x23C) - (int)u32(wave, 0x240)) * spp)
           - ofs;
}

typedef void (*draw_fn)(void *, void *);
#define WAVE_DRAW ((draw_fn)0x080C6339u)   /* stock waveform draw, vtable 0x080F1968 */

/* Waveform draw (vtable slot 0x080F1968, stock FUN_080C6338). The widget's tick asks for a redraw every
 * frame while it is on screen, so this runs on every scroll and zoom: the Clip pad's cue flags and the
 * grid lines for what is shown now go straight into the widget's marker buffer. (Device: lines were set
 * only on the screen update, for the part shown then.) Also notes that the editor is on screen. */
void deck_wave_draw(void *wave, void *ctx)
{
    if (live() && wave == ST->ui.wave && ST->ui.edit_pad) {
        unsigned idx = ST->ui.edit_pad - 1u, n, *buf = *(unsigned **)at(wave, 0x184);
        const DkPad *p = &ST->pad[idx];
        int ofs = ST->grid[idx].ofs;
        ST->ui.seen = UI_TICK;
        ST->ui.cursor = cursor_sample(wave, ofs);
        if (buf && p->unit_ticks && p->units >= 2 && p->spu > 0.0f) {
            n = cue_marks(wave, p, &ST->cue[idx], ofs, buf);
            if (ST->ui.line_s16)
                n = grid_marks(wave, p, (unsigned)ST->ui.line_s16 * TICKS_S16 / p->unit_ticks, ofs, buf, n,
                               WAVE_MARKS);
            *(unsigned *)at(wave, 0x188) = n;
            *((unsigned char *)wave + 0x238) = n != 0;
        }
    }
    WAVE_DRAW(wave, ctx);
}

/* Which Clip the waveform shows, for the draw hook and edit_start: set on every screen update. */
static void edit_target(void *view, unsigned mode)
{
    unsigned key;
    Pos b;
    if (mode != 1 || !live()) {
        ST->ui.wave = 0;
        ST->ui.edit_pad = 0;
        return;
    }
    key = view_key(view);
    pos_of(view, &b);
    ST->ui.line_s16 = (unsigned char)(ST->ui.grid ? (b.g < 4 ? b.g : 4) : 0);   /* a beat, or the finer Grid */
    ST->ui.edit_pad = (unsigned char)((((key >> 4) & 3) << 2 | (key & 3)) + 1);
    ST->ui.wave = at(view, V_WAVE);
}

/* Note start with the "..." editor open on this pad and the waveform on screen (drawn in the last
 * 300 ms): start at the bar the white marker is in. (Device request: play from the cursor.) */
static int edit_start(void *clip, unsigned n, unsigned ut, unsigned *unit)
{
    unsigned idx = idx_unpacked(u32(clip, 0x18)), u, bar;
    const DkPad *p = &ST->pad[idx];
    char *wave = (char *)ST->ui.wave;
    if (!wave || ST->ui.edit_pad != idx + 1 || UI_TICK - ST->ui.seen > 300u || !(p->spu > 0.0f))
        return 0;
    if (u8(wave - V_WAVE + V_ROW, 0x30))
        return 0;                                    /* the "..." row is closed */
    u = ST->ui.cursor > 0 ? (unsigned)((float)ST->ui.cursor / p->spu) : 0;
    bar = 3840u / ut ? 3840u / ut : 1;
    u = u / bar * bar;
    *unit = u < n ? u : n - 1;
    return 1;
}

/* Every update on a Clip pad (replaces the case-1 call to the beat-marker setter). */
void deck_ui_update(void *wave, void *beats, void *view)
{
    DkPad *p;
    Pos b;
    unsigned ut, s, e, ls, le, loop;
    int ofs;
    float spb;

    if (!live()) {
        WAVE_BEATS(wave, beats);
        return;
    }
    p = pad_packed(view_key(view));
    ofs = grid_packed(view_key(view))->ofs;
    ut = p->unit_ticks;
    pos_of(view, &b);
    if (ut && p->units >= 2 && p->spu > 0.0f) {
        unsigned n;
        edit_target(view, 1);
        n = cue_marks(wave, p, cue_packed(view_key(view)), ofs, ST->grid_pos);
        if (ST->ui.line_s16)   /* lines every beat, or every Grid step when the Grid is finer */
            n = grid_marks(wave, p, (unsigned)ST->ui.line_s16 * TICKS_S16 / ut, ofs, ST->grid_pos, n, GRID_MAX);
        ST->marks.pos = ST->grid_pos;
        ST->marks.count = n;
        WAVE_BEATS(wave, &ST->marks);
    } else {
        WAVE_BEATS(wave, beats);   /* Sync None / Slice: stock markers */
    }
    knob_titles(view, 0);
    row_lights(view);
    loop = bag_get(view, ID_LOOPMODE) != 0;

    if (ut == 0 || p->units < 2 || !(p->spu > 0.0f))
        return;   /* Sync None / Slice: one grain, no region to show */
    spb = p->spu;
    /* Draw where the engine will cut: 1/16 notes snapped down to whole units, moved by the offset. */
    s = to_units((unsigned)b.s, ut);
    e = b.e >= b.total ? p->units : to_units((unsigned)b.e, ut);
    ls = to_units((unsigned)b.ls, ut);
    le = b.le >= b.total ? p->units : to_units((unsigned)b.le, ut);
    if (e > p->units)
        e = p->units;
    if (le > e)
        le = e;
    WAVE_SPAN(wave, (int)(s * spb) + ofs, (int)((e > s ? e - s : 0) * spb));
    WAVE_LOOP(wave, (int)(ls * spb) + ofs, (int)(le * spb) + ofs, loop);
    /* Both setters clear +0x238; keep the markers WAVE_BEATS just set. */
    *((unsigned char *)wave + 0x238) = u32(wave, 0x188) != 0;
}

/* Bottom bar (replaces the call at 0x080A5568). Clip gets the Slicer bar and its "..." row. */
void deck_bar(void *view, unsigned is_slicer, unsigned mode)
{
    unsigned char *dots_hidden = (unsigned char *)at(view, V_BTN_EDIT + 0x30);
    VIEW_BAR(view, mode == 1 ? 1 : is_slicer);
    if (!live())
        return;
    /* Stock hides "..." on every update unless the pad is a Slicer (0x080A54F6: view+0x1D18 =
     * mode != 2), which left a black gap on Clip. Show it again: with a redraw when the pad mode or
     * the view changed, otherwise just undo the byte stock set a moment ago. */
    if (mode == 1) {
        if (mode != ST->bar_mode || ST->ui.row_view != view) {
            HIDE(at(view, V_BTN_EDIT), 0);
            ST->ui.row_view = view;
        } else {
            *dots_hidden = 0;
        }
    }
    edit_target(view, mode);
    if (mode == 1)
        knob_titles(view, 0);   /* stock's update reaches the Clip case (0x080A5B0E) only on a forced
                                 * update or a mode / sample change; a param change (event 0x66) does
                                 * not, so titles set there stayed stale (device: Grid "stuck") */
    if (mode != ST->bar_mode) {
        /* Another pad mode: close the row and any popup (stock pairs row and strip). */
        if (ST->bar_mode != 0xFF && (mode == 1 || ST->bar_mode == 1)) {
            HIDE(at(view, V_ROW), 1);
            HIDE(at(view, V_STRIP), 0);
            BTN_STATE(at(view, V_BTN_EDIT), 0);
        }
        ST->ui.popup = POP_NONE;
        ST->bar_mode = (unsigned char)mode;
    }
    row_apply(view, mode);
}

/* Waveform screen events (0x080A66DC). Returns 1 when handled here (stock skips it). */
int deck_ui_event(void *view, const unsigned short *ev)
{
    unsigned id = u32(ev, 0xC) & 0xFFFF, type = ev[0], popup, k;
    int i, held;
    if (!live() || (type != 1 && type != 2) || !is_clip(view))
        return 0;
    popup = ST->ui.popup;
    if (id == ID_DOTS) {
        if (type != 2)
            return 0;
        if (popup != POP_NONE) {
            set_popup(view, POP_NONE);   /* close the popup, row stays open */
            return 1;
        }
        return 0;                        /* stock opens / closes the row */
    }
    if ((i = btn_index(id)) < 0)
        return 0;
    held = u8(row_btn(view, 0), BTN_TOUCHES) != 0 && i != 0;   /* Cue tab held during this touch */
    for (k = 0; k < CUES && pop_btn[k] != i; k++)
        ;
    if (type == 1) {
        ST->ui.press_id = id;
        ST->ui.press_tick = UI_TICK;
        if (i == 0) {                    /* Cue tab down: a new hold */
            ST->ui.cue_used = 0;
        } else if (popup == POP_CUE && k < CUES) {
            int lit = bag_get(view, ID_CUE1 + k) != 0;
            if (held) {
                /* hold Cue + tap: a set cue is cleared, an unset one is set at the marker (device: the
                 * old "tap twice to clear" read as not working). Overwrite = clear, then set. */
                ST->ui.cue_used = 1;
                ST->ui.press_id = id | PRESS_HOLD;
                if (lit)
                    post_param(view, ID_CUE1 + k, 0);
                else
                    cue_store(view, k);
                ST->ui.sel = (unsigned char)k;
                knob_titles(view, 0);
            } else if (lit) {
                cue_note(view, k, 1);
                ST->ui.press_id = id | PRESS_NOTE;
            }
        }
        return 1;
    }
    if (i < 3) {                         /* tabs */
        if (i == 0 && ST->ui.cue_used) {
            ST->ui.cue_used = 0;
        } else {
            set_popup(view, popup == (unsigned)i + 1 ? POP_NONE : (unsigned)i + 1);
        }
    } else if (i == FRAME || k == CUES) {
        /* the popup's background or a hidden button: nothing */
    } else if (popup == POP_CUE) {
        if (ST->ui.press_id == (id | PRESS_NOTE)) {
            cue_note(view, k, 0);
            ST->ui.sel = (unsigned char)k;
        } else if (ST->ui.press_id == id) {
            cue_store(view, k);   /* a dark letter: the playhead goes there */
        }
        knob_titles(view, 0);
    } else if (popup == POP_LOOP) {
        Pos b;
        if (k == 0)
            post_param(view, ID_LOOPMODE, !bag_get(view, ID_LOOPMODE));
        else if (k == 1)
            loop_instant(view);
        else if (k == 2) {
            pos_of(view, &b);
            loop_move(view, b.g);
        }
    } else if (popup == POP_GRID) {
        Pos b;
        pos_of(view, &b);
        if (k == 0) {
            ST->ui.grid ^= 1;
            VIEW_UPDATE(view, 1);
        } else if (k == 1) {
            grid_ofs(view, -grid_samples(view, b.g));
        } else if (k == 2) {
            grid_ofs(view, grid_samples(view, b.g));
        } else if (k == 3) {
            ST->ui.raw ^= 1;
        }
    }
    ST->ui.press_id = 0;
    row_lights(view);
    return 1;
}

#ifdef SONG_CUES
/* ---- Song cues (3.1.T, docs/song-cues.md) ----
 * A Song scene sends Clip pads to one of their hot cues when it starts. Scenes are the layers of the
 * song's engine sequence (key 0x20303); each scene's bag holds scue1..scue16 (one per pad: 0 = none,
 * 1..8 = Cue A..H), added to the scene template so stock saves, loads and copies them by name.
 * hook_post copies them to SgState.cue as stock re-sends a scene (FUN_0809C140) and keeps them from
 * the engine. The next scene and its start tick are planned on every song step (hook_song_step): a
 * playing Clip jumps to its cue on the first grain from that tick, on the scene's bar, and the pad is
 * armed: whenever it starts during that scene, it starts at the cue. No note is sent, so Toggle / Gate and MIDI In play
 * no part. The Song screen's pad grid works like its sequence grid: pick Def (stock's pad event) or a
 * cue A..H as the brush, then tap pads; a pad with a cue starts the scene at it. */

#define SG_BASE      0x38006400u   /* SRAM4, free on 3.1.T: the Looper ends here, RbVoice starts at 0x38007000 */
#define SG_END       0x38007000u
#define SG_MAGIC     0x31474E53u   /* 'SNG1' */
#define SCENES       32
#define ID_SCUE      0x1C0         /* scue1..scue16; 3.1.T's params end at Clip Deck's 0x1BF */
#define SONG_KEY     0x20303u      /* the song's engine sequence; its layers are the scenes */
#define SG_BTN_ID    0x191         /* our Song-screen buttons, after Keep (0x190): A..H, Def */
#define SG_BTNS      (CUES + 1)
#define SG_DEF       CUES          /* button index of Def (the plain pad event) */

typedef struct {
    unsigned magic;                       /* +0x000 */
    void *player;                         /* +0x004 song player at the last song step */
    long long pre_at;                     /* +0x008 tick the planned next scene starts at */
    long long now_at;                     /* +0x010 tick the scene that just started began at */
    unsigned char cue[SCENES][DK_PADS];   /* +0x018 engine copy of each scene's scue params (hook_post) */
    unsigned char req[DK_PADS];           /* +0x218 1..8: an unplanned scene start: jump at the next Quant boundary */
    unsigned char arm[DK_PADS];           /* +0x228 1..8: where the pad starts during this scene */
    unsigned char next[DK_PADS];          /* +0x238 1..8: kept cached for the next scene */
    unsigned char now[DK_PADS];           /* +0x248 1..8: the started scene's jump, at the first grain from now_at */
    unsigned char pre[DK_PADS];           /* +0x258 1..8: the planned next scene's jump, at the first grain from pre_at */
    unsigned char jumped[DK_PADS];        /* +0x268 the planned jump was taken before the scene's own start */
    unsigned char skip[DK_PADS];          /* +0x278 skip the scene's own pad event (0x8B) once: jump done or due */
    unsigned char brush;                  /* +0x288 Song screen: 0 = stock brush, 1..8 = Cue A..H */
    unsigned char shown;                  /* +0x289 our buttons are shown (pad grid open) */
    unsigned char pre_scene;              /* +0x28A the planned next scene, 0xFF none */
    unsigned char rsv;
    unsigned btn[SG_BTNS][0x28];          /* +0x28C stock buttons (0xA0 each): A..H, then Def */
} SgState;

#define SG ((SgState *)SG_BASE)
typedef char sg_fits[(SG_BASE + sizeof(SgState) <= SG_END) ? 1 : -1];
typedef char sg_btn_at[(__builtin_offsetof(SgState, btn) == 0x28C) ? 1 : -1];      /* the bench's SG_BTN */
typedef char sg_cue_at[(__builtin_offsetof(SgState, cue) == 0x18) ? 1 : -1];
typedef char sg_ticks_aligned[(__builtin_offsetof(SgState, pre_at) & 7) == 0 ? 1 : -1];  /* LDRD */

static int sg_live(void)
{
    return SG->magic == SG_MAGIC && live();
}

/* Every boot (deck_init, from Param_RegisterAll): the scene table is filled again as the preset's
 * scenes are sent to the engine. The buttons are built by the Song screen's constructor. */
static void song_init(void)
{
    unsigned char *p = (unsigned char *)&SG->cue[0][0];
    unsigned i;
    for (i = 0; i < __builtin_offsetof(SgState, btn) - __builtin_offsetof(SgState, cue); i++)
        p[i] = 0;
    SG->player = 0;
    SG->pre_at = SG->now_at = 0;
    SG->pre_scene = 0xFF;
    SG->magic = SG_MAGIC;
}

static const char t_scue[DK_PADS][16] = {
    "Scene Cue 1:", "Scene Cue 2:", "Scene Cue 3:", "Scene Cue 4:", "Scene Cue 5:", "Scene Cue 6:",
    "Scene Cue 7:", "Scene Cue 8:", "Scene Cue 9:", "Scene Cue 10:", "Scene Cue 11:", "Scene Cue 12:",
    "Scene Cue 13:", "Scene Cue 14:", "Scene Cue 15:", "Scene Cue 16:"};
static const char x_scue[DK_PADS][7] = {"scue1", "scue2", "scue3", "scue4", "scue5", "scue6", "scue7",
                                        "scue8", "scue9", "scue10", "scue11", "scue12", "scue13",
                                        "scue14", "scue15", "scue16"};

/* deck_register: plain numbers 0..8 (only the Song screen edits them). */
void song_register(void *reg)
{
    unsigned i;
    for (i = 0; i < DK_PADS; i++)
        PARAM_NUMBER(reg, ID_SCUE + i, 1, t_scue[i], 0, CUES, x_scue[i]);
}

/* 0x0809450C, Param_BagTemplate case 0x4E (scene): stock adds Play Count (0xC3) only. */
void song_template(void *bag, unsigned id, int value)
{
    unsigned i;
    PARAM_ADD(bag, id, value);
    for (i = 0; i < DK_PADS; i++)
        PARAM_ADD(bag, ID_SCUE + i, 0);
}

/* hook_post (Engine_PostParam): our ids on the song's sequence are kept here, not posted. */
int song_post(unsigned key, unsigned id, unsigned value, unsigned layer)
{
    if (id - ID_SCUE >= DK_PADS)
        return 0;
    if (sg_live() && key == SONG_KEY && layer < SCENES)
        SG->cue[layer][id - ID_SCUE] = (unsigned char)(value <= CUES ? value : 0);
    return 1;
}

/* ---- The song player (FUN_0805D8C4, one step per beat) ----
 * Player +0x18 key, +0x30 playing, +0x58 / +0x41 not stepping, +0x5C moves on (not Loop1), +0x5D wraps
 * (LoopA), +0x60 the song, +0x64 scene count, +0xC20 the playing layer, +0xC24 a queued layer (a
 * touched scene). Layer +0x00 step length (index into 0x080ECA6C, beats), +0x04 step count, +0x38
 * its start tick (int64; a queued layer's is its switch tick, on a bar), +0x40 its index (the scene).
 * A scene's jump is planned ahead with the tick its scene starts at, so the first grain from that tick
 * takes it whatever the block order (the scene start's own message comes too late for the grain on
 * the bar about half the time, which made the jump a bar late; unit test 2026-10-23). */

#define STEP_BEATS   ((const float *)0x080ECA6Cu)   /* 15 step lengths in beats */

static long long layer_ticks(const void *layer)     /* a scene's length: step count x step length */
{
    unsigned i = u32(layer, 0), n = u32(layer, 4);
    int step;
    if (i > 14)
        i = 14;
    n = n < 1 ? 1 : n > 512 ? 512 : n;
    step = (int)(STEP_BEATS[i] * (float)TICKS_BEAT + 0.5f);   /* int: a float -> int64 call is libgcc's, in bank 2 */
    return (long long)step * (int)n;
}

static int song_player(void *player)
{
    return sg_live() && u32(player, 0x18) == SONG_KEY && u8(player, 0x60) && u8(player, 0x30) && !u8(player, 0x58);
}

/* The next scene and its start tick: a queued scene at its switch tick, else (at the scene's end) the
 * next one, the same one on Loop1, the first after the last on LoopA, none at the song's end. */
static void song_plan(void *player)
{
    const char *layer = *(const char **)at(player, 0xC20), *q = *(const char **)at(player, 0xC24);
    unsigned cur, nxt, k;
    long long t;
    if (!layer)
        return;
    cur = *(const unsigned short *)(layer + 0x40);
    if (q) {
        nxt = *(const unsigned short *)(q + 0x40);
        t = *(const long long *)(q + 0x38);
    } else {
        t = *(const long long *)(layer + 0x38) + layer_ticks(layer);
        nxt = !u8(player, 0x5C) ? cur : cur + 1 < u32(player, 0x64) ? cur + 1 : u8(player, 0x5D) ? 0 : 0xFF;
    }
    if (nxt >= SCENES) {
        SG->pre_scene = 0xFF;
        for (k = 0; k < DK_PADS; k++)
            SG->pre[k] = 0;
        return;
    }
    if (SG->pre_scene == nxt && SG->pre_at == t)
        return;                            /* planned already (its jumps may be under way) */
    SG->pre_scene = (unsigned char)nxt;
    SG->pre_at = t;
    for (k = 0; k < DK_PADS; k++) {
        SG->pre[k] = SG->cue[nxt][k];
        SG->jumped[k] = 0;
    }
}

/* Step 0 of the song's playing layer: the scene starts. Planned (same scene, same tick): its jumps
 * are taken or due at that tick. Not planned (PLAY, a locate): the old way, at the next Quant boundary. */
static void song_scene(void *player)
{
    const char *layer = *(const char **)at(player, 0xC20);
    unsigned scene, k, planned;
    long long t;
    if (!layer || (scene = *(const unsigned short *)(layer + 0x40)) >= SCENES)
        return;
    t = *(const long long *)(layer + 0x38);
    planned = SG->pre_scene == scene && SG->pre_at == t;
    SG->now_at = t;
    for (k = 0; k < DK_PADS; k++) {
        unsigned v = SG->cue[scene][k];
        SG->arm[k] = (unsigned char)v;
        SG->now[k] = planned ? SG->pre[k] : 0;
        SG->req[k] = planned ? 0 : (unsigned char)v;
        SG->skip[k] = planned && (SG->jumped[k] || SG->pre[k]);
        SG->pre[k] = 0;
        SG->jumped[k] = 0;
    }
    SG->pre_scene = 0xFF;
}

/* FUN_0805CDA0 entry (hook_song_step): every step of every sequence player; only the song's count. */
void song_step(void *player, void *transport, int step)
{
    unsigned k;
    (void)transport;
    if (!song_player(player))
        return;
    SG->player = player;
    if (step == 0 && !u8(player, 0x41))
        song_scene(player);
    song_plan(player);
    for (k = 0; k < DK_PADS; k++)
        SG->next[k] = SG->pre_scene < SCENES ? SG->cue[SG->pre_scene][k] : 0;
}

static int song_playing(void)
{
    return SG->player && u8(SG->player, 0x30);
}

/* Unit of cue letter v (1..8) on this clip, or CUE_NONE: not a unit grid, cues not in use, cue Off. */
static unsigned song_unit(void *clip, const DkCue *c, unsigned v)
{
    unsigned ut, n, s;
    if (v == 0 || v > CUES || !(n = grid_units(clip, &ut)) || !cue_active(c) || (s = c->cue[v - 1]) == CUE_NONE)
        return CUE_NONE;
    s = to_units(s, ut);
    return s < n ? s : n - 1;
}

/* deck_note_unit, no cue note pending: during a scene with a cue on this pad, the pad starts there. */
static unsigned song_start_unit(void *clip, DkCue *c)
{
    unsigned k = idx_unpacked(u32(clip, 0x18)), v;
    if (!sg_live())
        return CUE_NONE;
    v = SG->arm[k];
    SG->req[k] = 0;                    /* this start is already at the cue */
    SG->now[k] = 0;
    if (!song_playing()) {
        SG->arm[k] = 0;
        return CUE_NONE;
    }
    return song_unit(clip, c, v);
}

/* hook_pad_start: the Clip pad's event switch (FUN_08067FA4) at msg 0x8B, the scene's own pad event
 * (also a scene note). Stock releases both lanes there and starts a new voice: a retrigger. On a
 * clip that is playing while its scene start's jump waits, the event becomes the pending jump
 * instead (returns 1: the stub skips it), as a cue note on a playing clip does, so the clip keeps
 * playing and jumps at the next Quant Size boundary. Swallowing only the voice start, after stock had
 * released the lanes, stopped the clip (found on the unit, 3.1.T test 2026-10-23). */
int song_pad_start(void *clip)
{
    DkCue *c;
    unsigned k, unit;
    if (!sg_live() || !live() || !u8(clip, 0x28) || !(u8(clip, 0x260) || u8(clip, 0x670)))
        return 0;
    k = idx_unpacked(u32(clip, 0x18));
    c = clip_cue(clip);
    if (SG->skip[k]) {                    /* planned: the jump is taken (or due) at the scene's tick */
        SG->skip[k] = 0;
        return 1;
    }
    if (!SG->req[k] || (unit = song_unit(clip, c, SG->req[k])) == CUE_NONE)
        return 0;
    SG->req[k] = 0;
    c->pending = (unsigned short)unit;
    cue_prefetch(clip, unit, 0);
    return 1;
}

/* deck_cue_note set a pending jump: a scene start's request for this pad is dropped. */
static void song_drop(void *clip)
{
    unsigned k;
    if (!sg_live())
        return;
    k = idx_unpacked(u32(clip, 0x18));
    SG->req[k] = SG->now[k] = SG->pre[k] = 0;
}

/* deck_wrap_unit, every grain of a playing clip, with the grain's start tick. The first grain from
 * a scene's start tick takes that scene's jump at once (planned, so the Quant Size plays no part: the
 * scene starts on its bar). An unplanned scene start's jump becomes the pending one (next Quant
 * boundary). Otherwise the next jump's audio is kept cached. Returns the unit to play, or CUE_NONE. */
static unsigned song_grain(void *clip, DkCue *c, const long long *tick)
{
    unsigned k, v, unit;
    long long t = *tick;
    if (!sg_live())
        return CUE_NONE;
    k = idx_unpacked(u32(clip, 0x18));
    if (SG->now[k] && t >= SG->now_at) {
        v = SG->now[k];
        SG->now[k] = 0;
        if ((unit = song_unit(clip, c, v)) != CUE_NONE) {
            c->pending = CUE_NONE;
            return unit;
        }
    }
    if (SG->pre[k] && t >= SG->pre_at && song_playing()) {
        v = SG->pre[k];
        SG->pre[k] = 0;
        SG->jumped[k] = 1;
        if ((unit = song_unit(clip, c, v)) != CUE_NONE) {
            c->pending = CUE_NONE;
            return unit;
        }
    }
    if ((v = SG->req[k]) != 0) {
        SG->req[k] = 0;
        if ((unit = song_unit(clip, c, v)) != CUE_NONE) {
            c->pending = (unsigned short)unit;
            cue_prefetch(clip, unit, 0);
        }
        return CUE_NONE;
    }
    v = SG->now[k] ? SG->now[k] : SG->pre[k] ? SG->pre[k] : SG->next[k];
    if (c->pending == CUE_NONE && v && song_playing() && (unit = song_unit(clip, c, v)) != CUE_NONE)
        cue_prefetch(clip, unit, 1);
    return CUE_NONE;
}

/* ---- Song screen (FUN_080C2980, vtable 0x080F1758) ---- */

typedef int (*evt_fn)(void *, void *);
typedef void (*evt2_fn)(void *, void *);
typedef void (*setp_fn)(void *, unsigned, int);
typedef void (*cell_fn)(void *, unsigned, unsigned);
#define APP          ((void *)0x24020088u)
#define APP_SCENES   0x29C0      /* scene k's bag: APP + 0x29C0 + k * 0x30 */
#define APP_DIRTY    0x8CC4      /* unsaved changes */
#define SONG_SCENE   0x12180     /* selected scene */
#define SONG_BRUSH   0x1218D     /* stock brush: A..D = 0..3, Keep = 4 */
#define SONG_MODE    0x12194     /* 0/1 scene list, 2 sequence grid, 3 pad grid */
#define SONG_GRID    0x1072C     /* the 4 x 4 grid: a letter and a Keep mark per cell */
#define SONG_PANEL   0x11E28     /* Keep D C B A (FUN_080B8548), buttons from +0x34 */
#define ID_PATTERN_A 0x18C       /* A..D, Keep = 0x190 */
#define SG_COLW      62          /* stock's column width: A-D | E-H (and stock's own) */
#define SG_ROW       44          /* stock's button pitch (43 px buttons); y runs upwards */
#define MODE_PADS    3           /* the pad grid: stock hides its A-D there and keeps Keep */
#define SONG_EVENT   ((evt_fn)0x080C3541u)     /* vt+0x30 */
#define SONG_EVENT2  ((evt2_fn)0x080C3861u)    /* vt+0x34 */
#define PANEL_LAYOUT ((rect_fn)0x080B82A3u)    /* stock buttons in the rect, stacked */
#define PANEL_SELECT ((state_fn)0x080B85DDu)   /* highlight button i */
#define SCENE_SEND   ((state_fn)0x0809C141u)   /* (app, scene): items and params to the engine */
#define PARAM_SET    ((setp_fn)0x08093ECDu)
#define CELL_LETTER  ((cell_fn)0x080A367Du)
#define CELL_KEEP    ((cell_fn)0x080A36A9u)
#define BTN_STYLE    ((state_fn)0x080A3EA7u)


static void btn_hide(void *b, unsigned hidden)
{
    ((state_fn)(*(unsigned **)b)[11])(b, hidden);   /* vt+0x2C, as FUN_080B861E hides A..D */
}

static void *song_bag(unsigned scene)
{
    return at(APP, APP_SCENES + scene * 0x30);
}

/* 0x080C323A in the Song screen's constructor: the panel's layout. Stock's column stays as it is
 * (x 256: A at the top, Keep at the bottom; y runs upwards). Our A-D get a column left of it, with
 * Def at its bottom next to Keep; E-H take the places of stock's A-D, which the pad grid hides. The
 * panel's rect then spans both columns. The grid is sized from what is left of it (196 px wide) and
 * stays stock's 172 px, as the screen's height decides it. */
static const char s_def[] = "Def";
/* A table, not `i == SG_DEF ? ... : ...`: gcc made that an IT block with two loads, and Unicorn
 * (the bench) runs the code after a hooked load inside an IT block as still conditional. */
static const char *const sg_label[SG_BTNS] = {s_letter[0], s_letter[1], s_letter[2], s_letter[3], s_letter[4],
                                              s_letter[5], s_letter[6], s_letter[7], s_def};

void song_panel(void *panel, const int *rect)
{
    int x = rect[0] - SG_COLW;
    unsigned i;
    PANEL_LAYOUT(panel, rect);
    for (i = 0; i < SG_BTNS; i++) {
        void *b = SG->btn[i];
        BTN_CTOR(b);
        ((set_id_fn)(*(unsigned **)b)[9])(b, SG_BTN_ID + i);
        BTN_TEXT(b, sg_label[i]);
        BTN_STYLE(b, 5);
        if (i == SG_DEF)
            set_rect(b, x, rect[1], rect[2], SG_ROW - 1);
        else
            set_rect(b, x + (int)(i / 4) * SG_COLW, rect[1] + (4 - (int)(i % 4)) * SG_ROW, rect[2], SG_ROW - 1);
        ADD_CHILD(panel, b);
        btn_hide(b, 1);
    }
    SG->shown = 0;
    SG->brush = 0;
    *(int *)at(panel, 4) = x;
    *(int *)at(panel, 0xC) = rect[2] + SG_COLW;
}

#define GRID_CHAR    0x31        /* grid: a char per cell (lit when set), Keep marks at +0x41 */
#define GRID_KEEP    0x41
#define GRID_LETTERS 0x3B0       /* draw the chars (stock: sequence grid only) */
#define GRID_DIRTY   0x3B2
#define BRUSH_KEEP   4

static int scene_cue(unsigned scene, unsigned pad)
{
    void *bag = song_bag(scene);
    int v = bag_has(bag, ID_SCUE + pad) ? PARAM_GET(bag, ID_SCUE + pad) : 0;
    return v > 0 && v <= CUES ? v : 0;
}

/* Pad grid: stock lights a cell for each pad event that is on (its char 'A', not drawn there) and
 * marks Keep. Here the chars are drawn: a pad with a cue shows its letter, a plain one a blank. */
static void song_show(void *song)
{
    unsigned char *grid = at(song, SONG_GRID);
    unsigned scene = u32(song, SONG_SCENE), k;
    if (scene >= SCENES)
        return;
    for (k = 0; k < DK_PADS; k++) {
        unsigned ch = grid[GRID_CHAR + k], v;
        if (!ch || grid[GRID_KEEP + k])
            continue;
        v = (unsigned)scene_cue(scene, k);
        CELL_LETTER(grid, k, v ? 'A' + v - 1 : ' ');
    }
    if (!grid[GRID_LETTERS]) {
        grid[GRID_LETTERS] = 1;
        grid[GRID_DIRTY] = 1;
    }
}

static void song_lights(void *song)
{
    unsigned i;
    for (i = 0; i < CUES; i++)
        BTN_STATE(SG->btn[i], SG->brush == i + 1);
    BTN_STATE(SG->btn[SG_DEF], !SG->brush && u8(song, SONG_BRUSH) != BRUSH_KEEP);
}

/* After every Song-screen event: our buttons only in the pad grid; leaving it drops the cue brush. */
static void song_sync(void *song)
{
    unsigned on = u8(song, SONG_MODE) == MODE_PADS, i;
    if (!on)
        SG->brush = 0;
    if (SG->shown != on) {
        for (i = 0; i < SG_BTNS; i++)
            btn_hide(SG->btn[i], !on);
        SG->shown = (unsigned char)on;
    }
    if (on) {
        song_lights(song);
        song_show(song);
    }
}

/* Def or a cue letter: stock's brush is set to a plain pad event (not Keep), so Keep goes dark. */
static void song_brush(void *song, unsigned brush)
{
    SG->brush = (unsigned char)brush;
    *(unsigned char *)at(song, SONG_BRUSH) = 0;
    PANEL_SELECT(at(song, SONG_PANEL), 0);    /* stock's A, hidden in the pad grid */
    song_lights(song);
}

/* The pad's cue in this scene: the bag, then stock re-sends the scene (it reaches song_post). */
static void song_set_cue(void *song, unsigned pad, int v)
{
    unsigned scene = u32(song, SONG_SCENE), id = ID_SCUE + pad;
    void *bag;
    if (scene >= SCENES)
        return;
    bag = song_bag(scene);
    if (scene_cue(scene, pad) == v)
        return;
    if (bag_has(bag, id))
        PARAM_SET(bag, id, v);
    else
        PARAM_ADD(bag, id, v);
    SCENE_SEND(APP, scene);
    *(unsigned char *)at(APP, APP_DIRTY) = 1;
}

/* Song screen vt+0x30 (stock FUN_080C3540). In the pad grid: our buttons pick the brush, and a cell
 * tap is stock's (an off pad goes on with stock's brush, an on pad goes off), with the cue set to the
 * brush when the pad goes on and cleared otherwise. Keep stays stock's. Everything else is stock's. */
int song_event(void *song, void *ev)
{
    unsigned type = *(const unsigned short *)ev, id = u32(ev, 0xC);
    int r;
    if (sg_live() && u8(song, SONG_MODE) == MODE_PADS && (type == 1 || type == 2)) {
        if (id - SG_BTN_ID < SG_BTNS) {
            if (type == 1)
                song_brush(song, id - SG_BTN_ID == SG_DEF ? 0 : id - SG_BTN_ID + 1);
            return 0;
        }
        if (type == 1 && id < DK_PADS) {
            const unsigned char *grid = at(song, SONG_GRID);
            int was_on = grid[GRID_CHAR + id] || grid[GRID_KEEP + id];
            r = SONG_EVENT(song, ev);
            song_set_cue(song, id, !was_on && SG->brush ? SG->brush : 0);
            song_sync(song);
            return r;
        }
        if (type == 1 && id == ID_PATTERN_A + BRUSH_KEEP)
            SG->brush = 0;
    }
    r = SONG_EVENT(song, ev);
    if (sg_live())
        song_sync(song);
    return r;
}

/* Song screen vt+0x34 (stock FUN_080C3860): refreshes (0x66 / 0x67) redraw the grid. */
void song_event2(void *song, void *ev)
{
    SONG_EVENT2(song, ev);
    if (sg_live())
        song_sync(song);
}
#endif
