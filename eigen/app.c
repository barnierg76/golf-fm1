/* Golf voor de FM-1. Copyright (C) 2026 BARNIR. Alle rechten voorbehouden.
 *
 * Een wavetable-synthesizer: twee oscillatoren (A en B) die door een golftabel morphen, unisono, warps, een sub en
 * ruis, een ladderfilter, twee envelopes, twee LFO's, zes modulatieroutes (ook de druk op een toets), een arpeggiator
 * en effecten. De klankmotor staat in dsp/gf_engine.c; dit bestand is het instrument eromheen: toetsen en MIDI, de
 * bediening, de schermen, presets.
 *
 * Bediening:
 *   toetsen         spelen (aanslag en druk); OCT- / OCT+ het octaaf, samen ingedrukt: noten vasthouden
 *   PLAY            de stepsequencer starten en stoppen; de toetsen verplaatsen dan de reeks (C4 = zoals opgenomen)
 *   REC             stappen opnemen: toets(en) indrukken zet de stap, loslaten gaat naar de volgende; SEL is een rust;
 *                   lang indrukken wist de reeks
 *   SEQ             de pagina's STEP en PATTERN; SELECT kiest daar de stap
 *   PRESET          kiest een preset; SAVE bewaart de klank van nu als eigen preset
 *   SELECT / SEL    kiest oscillator A of B
 *   ALGORITHM       bladert door de pagina's
 *   HOME            PLAY                 EDIT   WAVE, SHAPE, TUNE (de oscillator)
 *   GLO             MIX, VOICE           ENV    FILTER, FILTER ENV, AMP ENV, MOD ENV
 *   LFO             LFO, MATRIX
 *   FX              EFFECTS, SPACE       ARP    de arpeggiator aan of uit (en zijn pagina)
 *   K1..K4          de vier regelaars van de pagina
 * Onderdeel van de unity-build van fm1-base (device/unity.c, host/plat_host.c), na het platform. */
#include "dsp/gf_engine.h"
#include "dsp/gf_tables.h"
#include "dsp/gf_presets.h"
#include "dsp/gf_levels.h"
#include "fastmath.h"

/* ------------------------------------------------------------- kleuren --- */
/* de kleuren van de plugin: een kleur per familie */
#define K_BG RGB(0x0e, 0x10, 0x14)
#define K_PANEL RGB(0x1c, 0x20, 0x28)
#define K_LINE RGB(0x2e, 0x34, 0x40)
#define K_DIM RGB(0x7a, 0x82, 0x90)
#define K_PAPER RGB(0xee, 0xf1, 0xf5)
#define K_MINT RGB(0x5f, 0xe3, 0xb0)     /* oscillatoren */
#define K_MINT2 RGB(0x24, 0x5a, 0x48)
#define K_MINT3 RGB(0x16, 0x30, 0x2a)
#define K_CORAL RGB(0xff, 0x7a, 0x6b)    /* filter */
#define K_AMBER RGB(0xf5, 0xb8, 0x3d)    /* envelopes */
#define K_CYAN RGB(0x4c, 0xc9, 0xf0)     /* LFO's */
#define K_VIOLET RGB(0xa7, 0x8b, 0xfa)   /* routes */
#define K_BLUE RGB(0x5b, 0x8d, 0xef)     /* effecten */

/* --------------------------------------------------------------- motor --- */
static gf_t T;
static int16_t mip0[GF_MIP_LEN] PLAT_POOL, mip1[GF_MIP_LEN] PLAT_POOL;
static int16_t dly_mem[2 * GF_DLY_LEN] PLAT_POOL;
static float rv_mem[GF_RV_MEM] PLAT_POOL, ch_mem[2 * GF_CH_LEN] PLAT_POOL;

/* De golftabellen: de ingebouwde (gf_tables.h), en daarachter die uit de bank die de computer stuurde
 * (tools/golf_tabellen.py): "GFB1", lengte, CRC-32 over de rest, aantal, per tabel 28 bytes (naam, plek, harmonischen,
 * 1 / piek), dan de tabellen zoals de ingebouwde. */
#define BANK_BASE 0x97000u
#define BANK_MAX (0xDC000u - BANK_BASE)
#define BANK_NTAB 32u
static gf_table_t TAB[GF_NTABLES + BANK_NTAB];
static char bank_name[BANK_NTAB][16];
static uint32_t ntab = GF_NTABLES;

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t crc32_of(const uint8_t *p, uint32_t n)
{
    static const uint32_t t[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u,
                                   0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u,
                                   0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        c = t[c & 15u] ^ c >> 4;
        c = t[c & 15u] ^ c >> 4;
    }
    return ~c;
}
static void bank_scan(void)              /* welke tabellen er nu zijn */
{
    const uint8_t *b = plat_data(BANK_BASE);
    uint32_t i, k, f, n = GF_NTABLES;
    for (i = 0; i < GF_NTABLES; i++)
        TAB[i] = GF_TABLES[i];
    if (b && rd32(b) == 0x31424647u) {               /* "GFB1" */
        uint32_t total = rd32(b + 4), cnt = b[12];
        if (total >= 16u && total <= BANK_MAX && cnt <= BANK_NTAB && 16u + 28u * cnt <= total &&
            crc32_of(b + 12, total - 12u) == rd32(b + 8))
            for (i = 0; i < cnt; i++) {
                const uint8_t *e = b + 16u + 28u * i;
                uint32_t off = rd32(e + 16), h = rd32(e + 20), at = off;
                union { uint32_t u; float f; } nm;
                nm.u = rd32(e + 24);
                for (f = 0; f < GF_NFR && at < total; f++)          /* zestien frames, elk binnen de bank */
                    at += 1u + 2u * b[at];
                if (off < 16u + 28u * cnt || f < GF_NFR || at > total || !h || h > 255u || !(nm.f > 0.0f) || nm.f > 1000.0f)
                    continue;
                for (k = 0; k < 14u && e[k] >= 32u && e[k] < 127u; k++)
                    bank_name[n - GF_NTABLES][k] = (char)e[k];
                bank_name[n - GF_NTABLES][k] = 0;
                TAB[n].d = b + off;
                TAB[n].h = (uint8_t)h;
                TAB[n].norm = nm.f;
                TAB[n].name = bank_name[n - GF_NTABLES];
                n++;
            }
    }
    ntab = n;
    T.tab = TAB;
    T.ntab = n;
}

/* noten van de hoofdlus naar de audio-interrupt: noot | aanslag << 8 (aanslag 0 = los; noot 255 = alles los) */
#define EVQ 64u
static volatile uint16_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
static void ev_push(uint32_t note, uint32_t vel)
{
    evq[ev_w % EVQ] = (uint16_t)(note | vel << 8);
    ev_w++;
}

static volatile uint8_t midi_pc;         /* een programmawissel over MIDI: preset + 1 (de hoofdlus laadt hem) */
static volatile uint32_t overloads;      /* zo vaak kwam een blok bijna te laat */
static volatile uint8_t want_status;
static volatile uint8_t poked;           /* een regelaar is over MIDI verzet: het scherm opnieuw */
static uint8_t poke_at, cap_lock;        /* CC 102: welke regelaar; CC 104: het unisono vastzetten (om te meten) */
static uint8_t sus_on;
static uint32_t sus_held[4];

static void midi_in(void)                /* audio-interrupt */
{
    uint32_t pkt, i;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st == 0x90u && d2) {
            sus_held[d1 >> 5] &= ~(1u << (d1 & 31u));
            gf_note_on(&T, d1, d2);
        } else if (st == 0x80u || st == 0x90u) {
            if (sus_on)
                sus_held[d1 >> 5] |= 1u << (d1 & 31u);
            else
                gf_note_off(&T, d1);
        } else if (st == 0xA0u) {                    /* de druk per toets */
            gf_pressure(&T, d1, d2);
        } else if (st == 0xD0u) {                    /* de druk van het hele klavier */
            T.chpress = (uint8_t)d1;
        } else if (st == 0xE0u) {                    /* het toonhoogtewiel: twee halve tonen */
            T.bend = ((float)(int32_t)(d1 | d2 << 7) - 8192.0f) * (2.0f / 8192.0f);
        } else if (st == 0xB0u) {
            if (d1 == 64u) {                         /* het pedaal */
                sus_on = d2 >= 64u;
                if (!sus_on)
                    for (i = 0; i < 128u; i++)
                        if (sus_held[i >> 5] >> (i & 31u) & 1u) {
                            sus_held[i >> 5] &= ~(1u << (i & 31u));
                            gf_note_off(&T, i);
                        }
            } else if (d1 == 1u) {                   /* het modulatiewiel */
                T.wheel = (uint8_t)d2;
            } else if (d1 == 102u) {                 /* elke regelaar van de klank over MIDI: CC 102 kiest, CC 103 zet */
                poke_at = (uint8_t)d2;
            } else if (d1 == 103u) {
                if (poke_at < sizeof T.p) {
                    ((uint8_t *)&T.p)[poke_at] = (uint8_t)d2;
                    poked = 1;
                }
            } else if (d1 == 105u) {                 /* de toestand van het apparaat op het scherm */
                want_status = 1;
            } else if (d1 == 104u) {                 /* het unisono op een vast aantal (0: weer vanzelf) */
                cap_lock = d2 != 0u;
                T.uni_cap = (uint8_t)(d2 ? (d2 > GF_NU ? GF_NU : d2) : GF_NU);
            } else if (d1 == 120u || d1 == 123u) {
                gf_all_off(&T);
                sus_held[0] = sus_held[1] = sus_held[2] = sus_held[3] = 0;
            }
        } else if (st == 0xC0u) {
            midi_pc = (uint8_t)(d1 + 1u);
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static int32_t gq;                   /* MASTER, Q16, glijdend: draaien geeft geen stapjes */
    uint32_t i;
    int32_t target = (int32_t)master_q12 << 4;
    midi_in();
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % EVQ], note = e & 0xFFu, vel = e >> 8;
        ev_r++;
        if (note == 0xFFu)
            gf_all_off(&T);
        else if (note == 0xFEu)                      /* de sequencer aan of uit */
            gf_seq_run(&T, vel != 0u);
        else if (vel)
            gf_note_on(&T, note, vel);
        else
            gf_note_off(&T, note);
    }
    gf_render(&T, lr, n);
    for (i = 0; i < n; i++) {
        int32_t g;
        gq += (target - gq) >> 6;
        g = gq >> 4;
        lr[2u * i] = (lr[2u * i] >> 8) * g >> 4;     /* 16 bits x Q12 -> 24 bits */
        lr[2u * i + 1u] = (lr[2u * i + 1u] >> 8) * g >> 4;
    }
}

void app_overload(void)                  /* audio-interrupt: het blok nam meer dan 90 % van zijn tijd */
{
    if (T.uni_cap > 1u && !cap_lock)
        T.uni_cap--;
    overloads++;
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < GF_NV; i++)
        T.v[i].on = 0;
    T.nheld = 0;
    T.arp_on = 0;
    T.mono_n = 0;
    T.seq_on = 0;
    T.seq_nh = 0;
}

/* ----------------------------------------------------------- bewaren --- */
#define NFACT ((uint32_t)GF_NPRESETS)
#define NUSER 16u
#define GFS_MAGIC 0x47465331u             /* "GFS1" */
#define GFU_MAGIC 0x47465532u             /* "GFU2" */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -3 .. +3 */
    uint8_t preset;                      /* 0 .. NFACT - 1 ingebouwd, daarna de eigen presets */
    uint8_t rsv[2];
} state_t;
typedef struct {
    uint32_t magic;
    uint8_t used[NUSER];
    gf_patch_t p[NUSER];
    gf_seq_t seq;                        /* de reeks van de stepsequencer */
} user_t;
static state_t st;
static user_t user;

static void bytes_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
}

/* --------------------------------------------------------- de pagina's --- */
enum { PG_PLAY, PG_WAVE, PG_SHAPE, PG_TUNE, PG_MIX, PG_VOICE, PG_FILT, PG_FENV, PG_AENV, PG_MENV, PG_LFO, PG_MTX, PG_FX,
       PG_SPACE, PG_ARP, PG_SEQ, PG_SEQ2, NPG };
enum { K_NONE, K_VAL, K_BI, K_TABLE, K_WARP, K_FILT, K_LFO, K_SRC, K_DST, K_MODE, K_ARPM, K_ARPR, K_OCT, K_SEMI, K_UNI,
       K_SUBO, K_SLOT, K_BPM, K_AOCT, K_SNOTE, K_GATE, K_LOCK, K_SLEN, K_CUR };
enum { W_GLOBAL, W_OSC, W_A, W_B, W_SLOT, W_UI, W_STEP, W_SEQ };      /* de klank, de gekozen oscillator, A, B, de gekozen route */
enum { C_OSC, C_FILT, C_ENV, C_LFO, C_MTX, C_FX, C_VOICE };
typedef struct {
    const char *name;
    uint8_t where, off, kind, fam;
} par_t;
#define GP(f) (uint8_t)__builtin_offsetof(gf_patch_t, f)
#define OP(f) (uint8_t)__builtin_offsetof(gf_osc_t, f)
#define MP(f) (uint8_t)__builtin_offsetof(gf_mod_t, f)
#define SP(f) (uint8_t)__builtin_offsetof(gf_step_t, f)
#define QP(f) (uint8_t)__builtin_offsetof(gf_seq_t, f)
#define NO_ {"", 0, 0, K_NONE, 0}
static const char *const PAGE_NAME[NPG] = {"PLAY", "WAVE", "SHAPE", "TUNE", "MIX", "VOICE", "FILTER", "FILTER ENV", "AMP ENV",
                                           "MOD ENV", "LFO", "MATRIX", "EFFECTS", "SPACE", "ARP", "STEP", "PATTERN"};
static const par_t PAGE[NPG][4] = {
    {{"MORPH A", W_A, OP(pos), K_VAL, C_OSC}, {"MORPH B", W_B, OP(pos), K_VAL, C_OSC}, {"CUTOFF", W_GLOBAL, GP(cut), K_VAL, C_FILT},
     {"RESO", W_GLOBAL, GP(res), K_VAL, C_FILT}},
    {{"TABLE", W_OSC, OP(table), K_TABLE, C_OSC}, {"MORPH", W_OSC, OP(pos), K_VAL, C_OSC}, {"LEVEL", W_OSC, OP(level), K_VAL, C_OSC},
     {"OCTAVE", W_OSC, OP(oct), K_OCT, C_OSC}},
    {{"WARP", W_OSC, OP(warp), K_WARP, C_OSC}, {"AMOUNT", W_OSC, OP(wamt), K_VAL, C_OSC}, {"UNISON", W_OSC, OP(uni), K_UNI, C_OSC},
     {"DETUNE", W_OSC, OP(detune), K_VAL, C_OSC}},
    {{"SEMI", W_OSC, OP(semi), K_SEMI, C_OSC}, {"FINE", W_OSC, OP(fine), K_BI, C_OSC}, {"WIDTH", W_OSC, OP(width), K_VAL, C_OSC}, NO_},
    {{"SUB", W_GLOBAL, GP(sub), K_VAL, C_OSC}, {"SUB OCT", W_GLOBAL, GP(sub_oct), K_SUBO, C_OSC}, {"NOISE", W_GLOBAL, GP(noise), K_VAL, C_OSC},
     NO_},
    {{"MODE", W_GLOBAL, GP(mode), K_MODE, C_VOICE}, {"GLIDE", W_GLOBAL, GP(glide), K_VAL, C_VOICE},
     {"VOLUME", W_GLOBAL, GP(master), K_VAL, C_VOICE}, {"TEMPO", W_GLOBAL, GP(bpm), K_BPM, C_VOICE}},
    {{"TYPE", W_GLOBAL, GP(ftype), K_FILT, C_FILT}, {"CUTOFF", W_GLOBAL, GP(cut), K_VAL, C_FILT}, {"RESO", W_GLOBAL, GP(res), K_VAL, C_FILT},
     {"DRIVE", W_GLOBAL, GP(drive), K_VAL, C_FILT}},
    {{"ENV AMT", W_GLOBAL, GP(fenv), K_BI, C_FILT}, {"KEY TRACK", W_GLOBAL, GP(ftrk), K_VAL, C_FILT}, NO_, NO_},
    {{"ATTACK", W_GLOBAL, GP(e[0].a), K_VAL, C_ENV}, {"DECAY", W_GLOBAL, GP(e[0].d), K_VAL, C_ENV}, {"SUSTAIN", W_GLOBAL, GP(e[0].s), K_VAL, C_ENV},
     {"RELEASE", W_GLOBAL, GP(e[0].r), K_VAL, C_ENV}},
    {{"ATTACK", W_GLOBAL, GP(e[1].a), K_VAL, C_ENV}, {"DECAY", W_GLOBAL, GP(e[1].d), K_VAL, C_ENV}, {"SUSTAIN", W_GLOBAL, GP(e[1].s), K_VAL, C_ENV},
     {"RELEASE", W_GLOBAL, GP(e[1].r), K_VAL, C_ENV}},
    {{"SHAPE 1", W_GLOBAL, GP(l[0].shape), K_LFO, C_LFO}, {"RATE 1", W_GLOBAL, GP(l[0].rate), K_VAL, C_LFO},
     {"SHAPE 2", W_GLOBAL, GP(l[1].shape), K_LFO, C_LFO}, {"RATE 2", W_GLOBAL, GP(l[1].rate), K_VAL, C_LFO}},
    {{"ROUTE", W_UI, 0, K_SLOT, C_MTX}, {"SOURCE", W_SLOT, MP(src), K_SRC, C_MTX}, {"TARGET", W_SLOT, MP(dst), K_DST, C_MTX},
     {"AMOUNT", W_SLOT, MP(amt), K_BI, C_MTX}},
    {{"DRIVE", W_GLOBAL, GP(fx_drive), K_VAL, C_FX}, {"OTT", W_GLOBAL, GP(ott), K_VAL, C_FX}, {"CHORUS", W_GLOBAL, GP(cho), K_VAL, C_FX},
     {"REV SIZE", W_GLOBAL, GP(rev_size), K_VAL, C_FX}},
    {{"DLY TIME", W_GLOBAL, GP(dly_time), K_VAL, C_FX}, {"FEEDBACK", W_GLOBAL, GP(dly_fb), K_VAL, C_FX}, {"DELAY", W_GLOBAL, GP(dly_mix), K_VAL, C_FX},
     {"REVERB", W_GLOBAL, GP(rev_mix), K_VAL, C_FX}},
    {{"PATTERN", W_GLOBAL, GP(arp_mode), K_ARPM, C_VOICE}, {"RATE", W_GLOBAL, GP(arp_rate), K_ARPR, C_VOICE},
     {"OCTAVES", W_GLOBAL, GP(arp_oct), K_AOCT, C_VOICE}, {"GATE", W_GLOBAL, GP(arp_gate), K_VAL, C_VOICE}},
    {{"NOTE", W_STEP, SP(note), K_SNOTE, C_VOICE}, {"VELOCITY", W_STEP, SP(vel), K_VAL, C_VOICE}, {"GATE", W_STEP, SP(gate), K_GATE, C_VOICE},
     {"MORPH", W_STEP, SP(morph), K_LOCK, C_OSC}},
    {{"LENGTH", W_SEQ, QP(len), K_SLEN, C_VOICE}, {"RATE", W_SEQ, QP(rate), K_ARPR, C_VOICE}, {"SWING", W_SEQ, QP(swing), K_VAL, C_VOICE},
     {"FILTER", W_STEP, SP(cut), K_LOCK, C_FILT}},
};
static const char *const GATE_NAME[GF_NGATE] = {"REST", "25", "50", "75", "100", "TIE"};
static const char *const WARP_NAME[GF_NWARP] = {"OFF", "SYNC", "BEND", "ASYM", "PWM", "FOLD", "FM"};
static const char *const FILT_NAME[GF_NFILT] = {"LADDER", "LOW 12", "BAND", "HIGH", "ACID"};
static const char *const LFO_NAME[GF_NLFO] = {"SINE", "TRI", "SAW", "SQUARE", "S&H", "DRIFT"};
static const char *const SRC_NAME[GF_NSRC] = {"-", "MOD ENV", "LFO 1", "LFO 2", "VELOCITY", "PRESSURE", "WHEEL", "KEY", "RANDOM"};
static const char *const DST_NAME[GF_NDST] = {"-", "PITCH", "MORPH A", "MORPH B", "MORPH", "CUTOFF", "RESO", "WARP A", "WARP B", "LEVEL A",
                                              "LEVEL B", "DETUNE", "PAN", "LFO1 RATE", "LFO2 RATE", "NOISE", "SUB", "VOLUME"};
static const char *const MODE_NAME[GF_NMODE] = {"POLY", "MONO", "LEGATO"};
static const char *const ARPM_NAME[GF_NARPMODE] = {"UP", "DOWN", "UP DOWN", "RANDOM", "PLAYED"};
static const char *const ARPR_NAME[GF_NARPRATE] = {"1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32"};
static const char OSC_LETTER[2][2] = {"A", "B"};

/* --------------------------------------------------------------------- UI --- */
#define WV_W 48u                         /* de golf van een frame op het scherm: zoveel punten */
static int8_t wave_pt[2][GF_NFR][WV_W];
static uint8_t wave_of[2] = {255, 255};  /* de tabel waar wave_pt van is */

static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;                  /* ingedrukt bij de vorige invoer */
    uint32_t latch[4];                   /* PLAY: de vastgehouden noten */
    uint32_t mid_at, recover_at;
    uint8_t page, osc, preset, last_k, slot;
    uint8_t hold, dirty;
    uint8_t oct_combo;                   /* OCT- en OCT+ waren samen ingedrukt */
    uint8_t cur, rec, seq_dirty;         /* de sequencer: de gekozen stap, opnemen, veranderd sinds het bewaren */
    uint32_t rec_keys, rec_at;           /* de toetsen van de stap die nu opgenomen wordt; sinds wanneer REC ingedrukt is */
    int32_t seen_step;
    uint8_t rd_head, rd_mid, rd_bot;     /* dit deel van het scherm opnieuw */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void two_digits(uint32_t v, char *b)
{
    b[0] = (char)('0' + v / 10u % 10u);
    b[1] = (char)('0' + v % 10u);
    b[2] = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.rd_head = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* De bank op soort: bas, lead, pad, toetsen, pluk, arpeggio, beweging. order[plek] = het nummer van de preset in
 * GF_PRESETS (dat nummer wordt bewaard: de volgorde mag veranderen). */
static uint8_t order[GF_NPRESETS], place[GF_NPRESETS];
static const char *const CAT_NAME[GF_NCAT] = {"BASS", "LEAD", "PAD", "KEYS", "PLUCK", "ARP", "MOTION"};
static void order_make(void)
{
    uint32_t c, i, n = 0;
    for (c = 0; c < GF_NCAT; c++)
        for (i = 0; i < NFACT; i++)
            if (GF_PRESETS[i].cat % GF_NCAT == c) {
                place[i] = (uint8_t)n;
                order[n++] = (uint8_t)i;
            }
}

static void preset_name(uint32_t i, char *t)     /* "003 GLASPAD", "U02 USER" */
{
    if (i < NFACT) {
        uint32_t n = place[i] + 1u;
        t[0] = (char)('0' + n / 100u % 10u);
        two_digits(n, t + 1);
        text_cat(t, " ");
        text_cat(t, GF_PRESETS[i].name);
    } else {
        t[0] = 'U';
        two_digits(i - NFACT + 1u, t + 1);
        text_cat(t, " USER");
    }
}

static void latch_clear(void);
static void preset_load(uint32_t i)
{
    if (i >= NFACT + NUSER || (i >= NFACT && !user.used[i - NFACT]))
        i = 0;
    if (T.p.arp || T.nheld)                          /* de arpeggiator speelt niet door in de volgende klank */
        ev_push(0xFFu, 0);
    bytes_copy(&T.p, i < NFACT ? &GF_PRESETS[i].p : &user.p[i - NFACT], sizeof T.p);
    if (i < NFACT)
        T.p.master = GF_LEVELS[i];                   /* elke ingebouwde preset even hard (gf_levels.h) */
    ui.preset = (uint8_t)i;
    ui.dirty = 0;
    ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
}

static void preset_step(int32_t d)
{
    uint32_t i = ui.preset < NFACT ? place[ui.preset] : ui.preset, n = NFACT + NUSER, k;   /* de plek in de bank */
    char t[28];
    for (k = 0; k < n; k++) {
        i = (i + (d > 0 ? 1u : n - 1u)) % n;
        if (i < NFACT || user.used[i - NFACT])
            break;
    }
    preset_load(i < NFACT ? order[i] : i);
    preset_name(ui.preset, t);
    if (ann_on())
        ann_say(t, 0, ANN_NOW);
}

static void category_step(void)         /* naar de eerste preset van de volgende soort */
{
    uint32_t c = ui.preset < NFACT ? GF_PRESETS[ui.preset].cat % GF_NCAT : GF_NCAT - 1u, k, i;
    for (k = 1; k <= GF_NCAT; k++) {
        uint32_t want = (c + k) % GF_NCAT;
        for (i = 0; i < NFACT; i++)
            if (GF_PRESETS[order[i]].cat % GF_NCAT == want) {
                preset_load(order[i]);
                app_say("", CAT_NAME[want]);
                return;
            }
    }
}

static int on_seq_page(void) { return ui.page == PG_SEQ || ui.page == PG_SEQ2; }
static void seq_default(void)            /* een reeks om mee te beginnen: zestien stappen, de golftabel beweegt mee */
{
    static const uint8_t N[16] = {48, 0, 48, 60, 0, 48, 51, 0, 48, 0, 55, 58, 0, 53, 51, 46};
    static const int8_t MO[16] = {0, 0, 10, 30, 0, 0, 20, 40, 0, 0, 15, 35, 0, 25, 45, 60};
    uint32_t i;
    for (i = 0; i < sizeof T.seq; i++)
        ((uint8_t *)&T.seq)[i] = 0;
    T.seq.len = 16;
    T.seq.rate = 3;
    for (i = 0; i < 16u; i++) {
        T.seq.s[i].note[0] = N[i];
        T.seq.s[i].vel = i % 4u == 0u ? 120 : 90;
        T.seq.s[i].gate = N[i] ? (i == 10u ? GF_G_TIE : GF_G_50) : GF_G_REST;
        T.seq.s[i].morph = MO[i];
        T.seq.s[i].cut = i % 4u == 3u ? 20 : 0;
    }
}
static void seq_save(void)
{
    bytes_copy(&user.seq, &T.seq, sizeof user.seq);
    user.magic = GFU_MAGIC;
    if (plat_store_save(OBJ_USER, &user, sizeof user) != 0) {
        app_say("SAVE", "FAILED");
        return;
    }
    ui.seq_dirty = 0;
    app_say("PATTERN", "SAVED");
}

static void preset_save(void)
{
    uint32_t slot = NUSER, i;
    char t[8];
    if (on_seq_page()) {                 /* op de pagina's van de sequencer: alleen de reeks */
        seq_save();
        return;
    }
    if (ui.preset >= NFACT) {
        slot = ui.preset - NFACT;
    } else {
        for (i = 0; i < NUSER; i++)
            if (!user.used[i]) {
                slot = i;
                break;
            }
    }
    if (slot >= NUSER) {
        app_say("USER PRESETS", "FULL");
        return;
    }
    bytes_copy(&user.p[slot], &T.p, sizeof T.p);
    bytes_copy(&user.seq, &T.seq, sizeof user.seq);
    ui.seq_dirty = 0;
    user.used[slot] = 1;
    user.magic = GFU_MAGIC;
    st.magic = GFS_MAGIC;
    st.preset = (uint8_t)(NFACT + slot);
    if (plat_store_save(OBJ_USER, &user, sizeof user) != 0 || plat_store_save(OBJ_STATE, &st, sizeof st) != 0) {
        app_say("SAVE", "FAILED");
        return;
    }
    ui.preset = st.preset;
    ui.dirty = 0;
    t[0] = 'U';
    two_digits(slot + 1u, t + 1);
    app_say("SAVED", t);
}

static void tables_sync(void)            /* de tabellen van de klank omzetten, en hun golven voor het scherm */
{
    uint32_t o, f, x;
    gf_tables_sync(&T);
    for (o = 0; o < 2u; o++)
        if (wave_of[o] != T.loaded[o]) {
            wave_of[o] = T.loaded[o];
            for (f = 0; f < GF_NFR; f++)
                for (x = 0; x < WV_W; x++)
                    wave_pt[o][f][x] = (int8_t)(gf_wave_at(&T, o, f, x * 512u / WV_W) * 11 / 32768);
            ui.rd_mid = 1;
        }
}

void app_init(void)
{
    uint32_t s;
    order_make();
    gf_init(&T, TAB, GF_NTABLES, mip0, mip1, dly_mem, rv_mem, ch_mem);
    bank_scan();
    if (plat_cpu2_set(1) == 0) {         /* de tweede rekenkern neemt de helft van de stemmen */
        T.fork = plat_cpu2_run;
        T.join = plat_cpu2_wait;
    }
    plat_usb_audio_full(1);              /* over USB altijd op vol niveau; MASTER regelt alleen speaker en koptelefoon */
    if (plat_store_load(OBJ_USER, &user, sizeof user) != (int)sizeof user || user.magic != GFU_MAGIC) {
        for (s = 0; s < sizeof user; s++)
            ((uint8_t *)&user)[s] = 0;
        user.magic = GFU_MAGIC;
        seq_default();
        bytes_copy(&user.seq, &T.seq, sizeof user.seq);
    }
    bytes_copy(&T.seq, &user.seq, sizeof T.seq);
    if (T.seq.len < 1u || T.seq.len > GF_SEQ_STEPS)
        T.seq.len = 16;
    if (plat_store_load(OBJ_STATE, &st, sizeof st) != (int)sizeof st || st.magic != GFS_MAGIC || st.octave < -3 || st.octave > 3) {
        st.magic = GFS_MAGIC;
        st.octave = 0;
        st.preset = 0;
    }
    preset_load(st.preset);
    tables_sync();
}

/* ------------------------------------------------------------- regelaars --- */
static uint8_t *par_ptr(const par_t *q)
{
    switch (q->where) {
    case W_OSC: return (uint8_t *)&T.p.o[ui.osc] + q->off;
    case W_A: return (uint8_t *)&T.p.o[0] + q->off;
    case W_B: return (uint8_t *)&T.p.o[1] + q->off;
    case W_SLOT: return (uint8_t *)&T.p.m[ui.slot] + q->off;
    case W_UI: return &ui.slot;
    case W_STEP: return (uint8_t *)&T.seq.s[ui.cur % GF_SEQ_STEPS] + q->off;
    case W_SEQ: return (uint8_t *)&T.seq + q->off;
    default: return (uint8_t *)&T.p + q->off;
    }
}
static void par_text(const par_t *q, char *t)
{
    uint32_t v = *par_ptr(q);
    t[0] = 0;
    switch (q->kind) {
    case K_TABLE: text_cat(t, TAB[v % ntab].name); break;
    case K_WARP: text_cat(t, WARP_NAME[v % GF_NWARP]); break;
    case K_FILT: text_cat(t, FILT_NAME[v % GF_NFILT]); break;
    case K_LFO: text_cat(t, LFO_NAME[v % GF_NLFO]); break;
    case K_SRC: text_cat(t, SRC_NAME[v % GF_NSRC]); break;
    case K_DST: text_cat(t, DST_NAME[v % GF_NDST]); break;
    case K_MODE: text_cat(t, MODE_NAME[v % GF_NMODE]); break;
    case K_ARPM: text_cat(t, ARPM_NAME[v % GF_NARPMODE]); break;
    case K_ARPR: text_cat(t, ARPR_NAME[v % GF_NARPRATE]); break;
    case K_SUBO: text_cat(t, v ? "-2" : "-1"); break;
    case K_OCT: case K_SEMI: case K_BI: {
        int32_t s = q->kind == K_BI ? (int32_t)v - 64 : (int8_t)v;
        if (s > 0)
            text_cat(t, "+");
        int_text(s, t + (t[0] ? 1 : 0));
        break;
    }
    case K_SLOT: case K_AOCT: int_text((int32_t)v + 1, t); break;
    case K_GATE: text_cat(t, GATE_NAME[v % GF_NGATE]); break;
    case K_SLEN: int_text((int32_t)v, t); break;
    case K_LOCK: {
        int32_t sv = (int8_t)v;
        if (sv > 0)
            text_cat(t, "+");
        int_text(sv, t + (t[0] ? 1 : 0));
        break;
    }
    case K_SNOTE: {                                  /* de laagste noot van de stap, en hoeveel erbij */
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const uint8_t *nt = par_ptr(q);
        uint32_t k, cnt = 0, lo = 255;
        char d[4];
        for (k = 0; k < 4u; k++)
            if (nt[k]) {
                cnt++;
                if (nt[k] < lo)
                    lo = nt[k];
            }
        if (!cnt) {
            text_cat(t, "-");
            break;
        }
        text_cat(t, NN[lo % 12u]);
        int_text((int32_t)(lo / 12u) - 1, d);
        text_cat(t, d);
        if (cnt > 1u) {
            text_cat(t, " +");
            int_text((int32_t)cnt - 1, d);
            text_cat(t, d);
        }
        break;
    }
    case K_BPM: int_text((int32_t)v + 40, t); break;
    case K_UNI: case K_VAL: int_text((int32_t)v, t); break;
    default: break;
    }
}
static void par_turn(uint32_t k, int32_t e)
{
    const par_t *q = &PAGE[ui.page][k];
    uint8_t *p;
    int32_t v, one = e > 0 ? 1 : -1, a = e < 0 ? -e : e, fast = e * (a >= 3 ? 4 : a == 2 ? 2 : 1);
    if (q->kind == K_NONE)
        return;
    p = par_ptr(q);
    if (q->kind == K_SNOTE) {                        /* alle noten van de stap een halve toon op of neer; leeg: een C */
        gf_step_t *st_ = &T.seq.s[ui.cur % GF_SEQ_STEPS];
        uint32_t j, any = 0;
        for (j = 0; j < 4u; j++)
            if (st_->note[j]) {
                st_->note[j] = (uint8_t)clamp(st_->note[j] + one, 1, 127);
                any = 1;
            }
        if (!any) {
            st_->note[0] = 60;
            if (!st_->vel)
                st_->vel = 100;
            if (st_->gate == GF_G_REST)
                st_->gate = GF_G_50;
        }
        ui.seq_dirty = 1;
        ui.last_k = (uint8_t)k;
        ui.rd_bot = ui.rd_mid = 1;
        return;
    }
    switch (q->kind) {
    case K_GATE: v = clamp(*p + one, 0, GF_NGATE - 1); break;
    case K_SLEN: v = clamp(*p + one, 1, GF_SEQ_STEPS); break;
    case K_LOCK: v = clamp((int8_t)*p + fast, -64, 63); break;
    case K_TABLE: v = clamp(*p + fast, 0, (int32_t)ntab - 1); break;
    case K_WARP: v = clamp(*p + one, 0, GF_NWARP - 1); break;
    case K_FILT: v = clamp(*p + one, 0, GF_NFILT - 1); break;
    case K_LFO: v = clamp(*p + one, 0, GF_NLFO - 1); break;
    case K_SRC: v = clamp(*p + one, 0, GF_NSRC - 1); break;
    case K_DST: v = clamp(*p + one, 0, GF_NDST - 1); break;
    case K_MODE: v = clamp(*p + one, 0, GF_NMODE - 1); break;
    case K_ARPM: v = clamp(*p + one, 0, GF_NARPMODE - 1); break;
    case K_ARPR: v = clamp(*p + one, 0, GF_NARPRATE - 1); break;
    case K_SUBO: v = clamp(*p + one, 0, 1); break;
    case K_OCT: v = clamp((int8_t)*p + one, -3, 3); break;
    case K_SEMI: v = clamp((int8_t)*p + one, -12, 12); break;
    case K_UNI: v = clamp(*p + one, 1, GF_NU); break;
    case K_SLOT: v = clamp(*p + one, 0, GF_NMOD - 1); break;
    case K_AOCT: v = clamp(*p + one, 0, 3); break;
    case K_BPM: v = clamp(*p + fast, 0, 200); break;
    default: v = clamp(*p + fast, 0, 127); break;    /* snel draaien: grotere stappen */
    }
    if ((uint8_t)v != *p) {
        *p = (uint8_t)v;
        if (q->where == W_STEP || q->where == W_SEQ) {
            ui.seq_dirty = 1;
        } else if (q->kind != K_SLOT) {
            ui.dirty = 1;
            ui.rd_head = 1;
        }
        if (ann_on()) {
            char t[16];
            par_text(q, t);
            ann_say(q->name, t, ANN_NOW);
        }
    }
    ui.last_k = (uint8_t)k;
    ui.rd_bot = ui.rd_mid = 1;
}

static void page_set(uint32_t pg)
{
    ui.page = (uint8_t)(pg % NPG);
    ui.rd_mid = 1;
    ui.last_k = 255;
    ui.rd_bot = 1;
    if (ann_on())
        ann_say(PAGE_NAME[ui.page], 0, ANN_NOW);
}
static void osc_set(int32_t o)
{
    ui.osc = (uint8_t)clamp(o, 0, 1);
    ui.rd_mid = ui.rd_bot = 1;
    if (ann_on())
        ann_say("OSCILLATOR", OSC_LETTER[ui.osc], ANN_NOW);
}

/* ---------------------------------------------------------------- noten --- */
static int32_t key_note(uint32_t k) { return clamp(53 + (int32_t)k + 12 * st.octave, 0, 127); }
static void note_send(int32_t note, uint32_t vel)   /* naar de motor en naar USB-MIDI */
{
    ev_push((uint32_t)note, vel);
    if (vel)
        plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | vel << 24, TO_USB);
    else
        plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB);
}
static void latch_clear(void)
{
    uint32_t i;
    for (i = 0; i < 128u; i++)
        if (ui.latch[i >> 5] >> (i & 31u) & 1u)
            note_send((int32_t)i, 0);
    ui.latch[0] = ui.latch[1] = ui.latch[2] = ui.latch[3] = 0;
}
static void octave_step(int32_t d)
{
    int32_t o = clamp(st.octave + d, -3, 3);
    uint32_t k;
    char t[4];
    if (o == st.octave)
        return;
    if (!ui.hold)                        /* een toets die ingedrukt blijft zou zijn noot niet meer loslaten */
        for (k = 0; k < NKEYS; k++)
            if (ui.keys >> k & 1u)
                note_send(key_note(k), 0);
    st.octave = (int8_t)o;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
    ui.rd_mid = 1;
}

static void hold_toggle(uint32_t keys)
{
    ui.hold = (uint8_t)!ui.hold;
    if (ui.hold) {                                   /* wat nu klinkt blijft klinken */
        uint32_t j;
        for (j = 0; j < NKEYS; j++)
            if (keys >> j & 1u) {
                int32_t n = key_note(j);
                ui.latch[n >> 5] |= 1u << (n & 31);
            }
    } else {
        latch_clear();
    }
    app_say("HOLD", ui.hold ? "ON" : "OFF");
    ui.rd_mid = 1;
}
static void status_say(void)             /* de toestand van het apparaat: belasting, haperingen, ruimte (MIDI CC 105) */
{
    char t[28], n[12];
    t[0] = 0;
    int_text((int32_t)plat_cpu_pct(), n);
    text_cat(t, n);
    text_cat(t, "% X");
    int_text((int32_t)plat_xruns(), n);
    text_cat(t, n);
    text_cat(t, " O");
    int_text((int32_t)overloads, n);
    text_cat(t, n);
    text_cat(t, " U");
    int_text((int32_t)T.uni_cap, n);
    text_cat(t, n);
    text_cat(t, plat_cpu2_on() ? " C2" : " C1");
    app_say("", t);
    ui.msg_until = plat_ms() + 15000u;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, lit = 0, klit;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int32_t note = key_note(k);
        if (ui.rec) {                                /* opnemen: de toetsen zetten de stap, loslaten gaat verder */
            if ((keys & bit) && !(ui.keys & bit)) {
                gf_step_t *sp = &T.seq.s[ui.cur % GF_SEQ_STEPS];
                uint32_t v = touch_vel(k), j;
                if (!v)
                    v = 100;
                if (!ui.rec_keys) {
                    sp->note[0] = sp->note[1] = sp->note[2] = sp->note[3] = 0;
                    sp->vel = (uint8_t)v;
                    if (sp->gate == GF_G_REST)
                        sp->gate = GF_G_50;
                    if (ui.cur >= T.seq.len)
                        T.seq.len = (uint8_t)(ui.cur + 1u);
                }
                for (j = 0; j < 4u && sp->note[j]; j++)
                    ;
                if (j < 4u)
                    sp->note[j] = (uint8_t)(note < 1 ? 1 : note);
                ui.rec_keys |= bit;
                ui.seq_dirty = 1;
                note_send(note, v);
                ui.rd_mid = ui.rd_bot = 1;
            } else if (!(keys & bit) && (ui.keys & bit)) {
                note_send(note, 0);
                ui.rec_keys &= ~bit;
                if (!ui.rec_keys) {
                    ui.cur = (uint8_t)((ui.cur + 1u) % GF_SEQ_STEPS);
                    ui.rd_mid = ui.rd_bot = 1;
                }
            }
        } else if (T.seq_on) {                       /* de reeks speelt: een toets verplaatst hem (C4 = zoals opgenomen) */
            if ((keys & bit) && !(ui.keys & bit)) {
                T.seq_tr = (int8_t)(note - 60);
                ui.rd_mid = 1;
            }
        } else if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            if (!v)
                v = 100;
            if (ui.hold) {                           /* vasthouden: een toets zet zijn noot aan, nog eens: uit */
                if (ui.latch[note >> 5] >> (note & 31) & 1u) {
                    ui.latch[note >> 5] &= ~(1u << (note & 31));
                    note_send(note, 0);
                } else {
                    ui.latch[note >> 5] |= 1u << (note & 31);
                    note_send(note, v);
                }
            } else {
                note_send(note, v);
            }
        } else if (!(keys & bit) && (ui.keys & bit) && !ui.hold) {
            note_send(note, 0);
        } else if (keys & bit) {                     /* de druk op een toets die ingedrukt blijft */
            gf_pressure(&T, (uint32_t)note, touch_pressure(k));
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_OCTDN: case B_OCTUP:                  /* samen ingedrukt: noten vasthouden; een alleen: het octaaf, bij loslaten */
            if (btn & 1u << (k == B_OCTUP ? B_OCTDN : B_OCTUP)) {
                ui.oct_combo = 1;
                hold_toggle(keys);
            }
            break;
        case B_HOME:                                 /* naar PLAY; daar nog eens: de volgende soort presets */
            if (ui.page == PG_PLAY)
                category_step();
            else
                page_set(PG_PLAY);
            break;
        case B_EDIT: page_set(ui.page >= PG_WAVE && ui.page < PG_TUNE ? ui.page + 1u : PG_WAVE); break;
        case B_GLO: page_set(ui.page == PG_MIX ? PG_VOICE : PG_MIX); break;
        case B_SEQ: page_set(ui.page == PG_SEQ ? PG_SEQ2 : PG_SEQ); break;
        case B_ENV: page_set(ui.page >= PG_FILT && ui.page < PG_MENV ? ui.page + 1u : PG_FILT); break;
        case B_LFO: page_set(ui.page == PG_LFO ? PG_MTX : PG_LFO); break;
        case B_FX: page_set(ui.page == PG_FX ? PG_SPACE : PG_FX); break;
        case B_ARP:                                  /* de arpeggiator aan of uit */
            T.p.arp = (uint8_t)!T.p.arp;
            if (!T.p.arp && !ui.hold)
                ev_push(0xFFu, 0);
            ui.dirty = 1;
            page_set(PG_ARP);
            app_say("ARP", T.p.arp ? "ON" : "OFF");
            ui.rd_mid = 1;
            break;
        case B_SEL:
            if (on_seq_page() || ui.rec) {           /* een rust: de stap leeg, naar de volgende */
                gf_step_t *sp = &T.seq.s[ui.cur % GF_SEQ_STEPS];
                sp->note[0] = sp->note[1] = sp->note[2] = sp->note[3] = 0;
                sp->gate = GF_G_REST;
                if (ui.rec && ui.cur >= T.seq.len)
                    T.seq.len = (uint8_t)(ui.cur + 1u);
                ui.cur = (uint8_t)((ui.cur + 1u) % GF_SEQ_STEPS);
                ui.seq_dirty = 1;
                ui.rd_mid = ui.rd_bot = 1;
            } else {
                osc_set(!ui.osc);
            }
            break;
        case B_SAVE: preset_save(); break;
        case B_REC: ui.rec_at = plat_ms(); break;    /* kort: opnemen aan of uit; lang: de reeks wissen (bij het loslaten) */
        case B_PLAY:                                 /* op de sequencerpagina's: de reeks starten; loopt hij: stoppen;
                                                        anders: noten vasthouden */
            if (T.seq_on || on_seq_page() || ui.rec) {
                ev_push(0xFEu, T.seq_on ? 0u : 1u);
                T.seq_tr = 0;
                app_say("SEQ", T.seq_on ? "STOP" : "PLAY");
                ui.rd_mid = 1;
            } else {
                hold_toggle(keys);
            }
            break;
        default: break;
        }
    }
    if (!ui.oct_combo) {
        if ((ui.btn & 1u << B_OCTDN) && !(btn & 1u << B_OCTDN))
            octave_step(-1);
        if ((ui.btn & 1u << B_OCTUP) && !(btn & 1u << B_OCTUP))
            octave_step(1);
    }
    if (!(btn & (1u << B_OCTDN | 1u << B_OCTUP)))
        ui.oct_combo = 0;
    if ((ui.btn & 1u << B_REC) && !(btn & 1u << B_REC)) {
        if (plat_ms() - ui.rec_at >= 900u) {         /* lang: de reeks leeg, klaar om opnieuw op te nemen */
            uint32_t j;
            uint8_t rate = T.seq.rate, swing = T.seq.swing;
            for (j = 0; j < sizeof T.seq; j++)
                ((uint8_t *)&T.seq)[j] = 0;
            T.seq.len = 1;
            T.seq.rate = rate;
            T.seq.swing = swing;
            ui.cur = 0;
            ui.rec = 1;
            ui.seq_dirty = 1;
            app_say("PATTERN", "CLEARED");
        } else {
            ui.rec = (uint8_t)!ui.rec;
            if (ui.rec && !on_seq_page())
                page_set(PG_SEQ);
            app_say("RECORD", ui.rec ? "ON" : "OFF");
        }
        ui.rec_keys = 0;
        ui.rd_mid = ui.rd_bot = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (on_seq_page() || ui.rec) {               /* de stap kiezen */
            ui.cur = (uint8_t)clamp(ui.cur + (e > 0 ? 1 : -1), 0, GF_SEQ_STEPS - 1);
            ui.rd_mid = ui.rd_bot = 1;
        } else {
            osc_set(ui.osc + (e > 0 ? 1 : -1));
        }
    }
    if ((e = plat_enc(EN_ALGO)) != 0)
        page_set((uint32_t)(ui.page + (e > 0 ? 1 : (int32_t)NPG - 1)));
    if ((e = plat_enc(EN_PRESET)) != 0)
        preset_step(e);
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            par_turn(k, e);
    ui.keys = keys;
    ui.btn = btn;
    /* de lampjes: de knop van de pagina, SAVE als de klank veranderd is, PLAY bij vasthouden, ARP, de toetsen die klinken */
    lit = btn;
    lit |= ui.page == PG_PLAY ? 1u << B_HOME : ui.page <= PG_TUNE ? 1u << B_EDIT : ui.page <= PG_VOICE ? 1u << B_GLO
           : ui.page <= PG_MENV ? 1u << B_ENV : ui.page <= PG_MTX ? 1u << B_LFO : ui.page <= PG_SPACE ? 1u << B_FX
           : ui.page >= PG_SEQ ? 1u << B_SEQ : 0u;
    if (ui.dirty)
        lit |= 1u << B_SAVE;
    if (T.seq_on || ui.hold)
        lit |= 1u << B_PLAY;
    if (ui.rec)
        lit |= 1u << B_REC;
    if (T.p.arp)
        lit |= 1u << B_ARP;
    klit = keys;
    for (k = 0; k < NKEYS; k++) {
        int32_t n = key_note(k);
        if (ui.latch[n >> 5] >> (n & 31) & 1u)
            klit |= 1u << k;
        if (on_seq_page() || ui.rec) {               /* de noten van de gekozen stap */
            const gf_step_t *sp = &T.seq.s[ui.cur % GF_SEQ_STEPS];
            if (n && (sp->note[0] == n || sp->note[1] == n || sp->note[2] == n || sp->note[3] == n))
                klit |= 1u << k;
        }
    }
    plat_glow(1);
    plat_leds(lit, klit);
}

void app_input_only(void) { input(); }

/* Eigen golftabellen vanaf de computer (tools/golf_tabellen.py): bericht 0x61 met de lengte en de CRC-32 van een bank.
 * De noten gaan uit, de bank wordt sector voor sector gewist en geschreven met wat de computer aanreikt, en daarna
 * nagekeken. Het antwoord is een laatste vraag aan de computer: 0xC0000000 + het aantal tabellen, of 0xCF000000 + wat
 * er misging. */
static void draw_head(void);
static void latch_clear(void);
void app_usb_cmd(const uint8_t *d, uint32_t n)
{
    static uint8_t buf[512];
    uint32_t len, crc, off, code = 0;
    const uint8_t *b;
    if (n != 15u || d[2] != 0x61u)
        return;
    len = rd32(d + 6);
    crc = rd32(d + 10);
    if (len > BANK_MAX) {
        code = 1;
    } else {
        latch_clear();
        ui.hold = 0;
        app_panic();
        ntab = T.ntab = GF_NTABLES;                  /* niets leest de bank nog: de klank terug op ingebouwde tabellen */
        if (T.p.o[0].table >= GF_NTABLES)
            T.p.o[0].table = 0;
        if (T.p.o[1].table >= GF_NTABLES)
            T.p.o[1].table = 0;
        gf_tables_sync(&T);
        app_say("TABLES", len ? "LOADING" : "CLEARING");
        draw_head();
        plat_host_patience(3000u, 0);
        if (plat_data_erase(BANK_BASE) != 0)         /* de kop weg: vanaf nu is er geen bank, tot hij er heel staat */
            code = 2;
        for (off = 0; !code && off < len; off += 512u) {
            uint32_t m = len - off < 512u ? len - off : 512u;
            if (off && !(off & 0xFFFu) && plat_data_erase(BANK_BASE + off) != 0)
                code = 2;
            else if (plat_host_read(off, buf, m) != 0)
                code = 3;
            else if (plat_data_write(BANK_BASE + off, buf, m) != 0)
                code = 4;
        }
        b = plat_data(BANK_BASE);
        if (!code && len && (!b || crc32_of(b, len) != crc))
            code = 5;
        if (code)
            (void)plat_data_erase(BANK_BASE);
        bank_scan();
        if (!code && len && ntab == GF_NTABLES)
            code = 5;
        app_say("TABLES", code ? "FAILED" : len ? "LOADED" : "CLEARED");
        ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
    }
    (void)plat_host_read(code ? 0xCF000000u | code : 0xC0000000u | (ntab - GF_NTABLES), buf, 8u);
}

/* --------------------------------------------------------------- tekenen --- */
static uint16_t fam_ink(uint32_t fam)
{
    static const uint16_t INK[7] = {K_MINT, K_CORAL, K_AMBER, K_CYAN, K_VIOLET, K_BLUE, K_PAPER};
    return INK[fam % 7u];
}
static void box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* een omtrek */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

static void draw_head(void)              /* 240 x 28: het woordmerk, de preset of een bericht */
{
    char t[32];
    int32_t w;
    cv_begin(240, 28, K_BG);
    cv_text(8, 6, &FONT_B, APP_NAME, K_MINT);
    cv_rect(8, 23, text_w(&FONT_B, APP_NAME), 2, K_MINT2);
    if (plat_ms() < ui.msg_until) {
        w = text_w(&FONT_B, ui.msg[1]);
        cv_text(234 - w, 6, &FONT_B, ui.msg[1], K_AMBER);
        cv_text(234 - w - 6 - text_w(&FONT_B, ui.msg[0]), 6, &FONT_B, ui.msg[0], K_DIM);
    } else {
        const felucca_font_t *f;
        preset_name(ui.preset, t);
        f = text_w(&FONT_B, t) > 222 - 14 - text_w(&FONT_B, APP_NAME) ? &FONT_T : &FONT_B;   /* een lange naam: klein */
        w = text_w(f, t);
        cv_text(234 - w, f == &FONT_B ? 6 : 10, f, t, K_PAPER);
        if (ui.dirty)                                /* veranderd sinds het laden of bewaren */
            cv_rect(234 - w - 9, 12, 4, 4, K_AMBER);
    }
    cv_rect(0, 27, 240, 1, K_LINE);
    cv_blit(0, 0);
}

static float live_pos(uint32_t o)        /* waar de nieuwste noot nu in de tabel is: de routes bewegen mee */
{
    uint32_t i, best = 0;
    float p = (float)T.p.o[o].pos * (1.0f / 127.0f);
    for (i = 0; i < GF_NV; i++)
        if (T.v[i].on && T.v[i].age >= best && T.v[i].posq[o] >= 0.0f) {
            best = T.v[i].age;
            p = T.v[i].posq[o];
        }
    return p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
}

static void draw_seq(void)               /* 240 x 116: de reeks, twee rijen van zestien stappen */
{
    uint32_t i, k, len = T.seq.len;
    int32_t lo = 127, hi = 0;
    char t[20], d[8];
    cv_begin(240, 116, K_BG);
    cv_rect(4, 4, 30, 16, ui.rec ? K_CORAL : K_MINT);
    cv_text(8, 4, &FONT_B, ui.rec ? "REC" : "SEQ", K_BG);
    t[0] = 0;
    text_cat(t, "STEP ");
    int_text((int32_t)ui.cur + 1, d);
    text_cat(t, d);
    text_cat(t, " / ");
    int_text((int32_t)len, d);
    text_cat(t, d);
    cv_text(42, 7, &FONT_T, t, K_PAPER);
    cv_text(118, 7, &FONT_T, ARPR_NAME[T.seq.rate % GF_NARPRATE], K_DIM);
    int_text(40 + (int32_t)T.p.bpm, t);
    text_cat(t, " BPM");
    cv_text(150, 7, &FONT_T, t, K_DIM);
    if (T.seq_on && T.seq_tr) {
        t[0] = T.seq_tr > 0 ? '+' : '-';
        int_text(T.seq_tr > 0 ? T.seq_tr : -T.seq_tr, t + 1);
        cv_text(236 - text_w(&FONT_T, t), 7, &FONT_T, t, K_AMBER);
    }
    for (i = 0; i < GF_SEQ_STEPS; i++)                /* het bereik van de noten, voor de hoogte van de balkjes */
        for (k = 0; k < 4u; k++)
            if (T.seq.s[i].note[k]) {
                if (T.seq.s[i].note[k] < lo) lo = T.seq.s[i].note[k];
                if (T.seq.s[i].note[k] > hi) hi = T.seq.s[i].note[k];
            }
    if (hi - lo < 12) {
        hi = (hi + lo) / 2 + 6;
        lo = hi - 12;
    }
    for (i = 0; i < GF_SEQ_STEPS; i++) {
        const gf_step_t *sp = &T.seq.s[i];
        int32_t x = 8 + (int32_t)(i % 16u) * 14, y = 24 + (int32_t)(i / 16u) * 46, gw;
        int in = i < len, play = T.seq_on && (int32_t)i == T.seq_step;
        uint16_t ink = !in ? K_LINE : play ? K_AMBER : K_MINT;
        cv_rect(x, y, 13, 32, i % 4u == 0u ? K_PANEL : RGB(0x14, 0x18, 0x1e));
        gw = sp->gate == GF_G_TIE ? 14 : sp->gate == GF_G_REST ? 0 : 3 + (int32_t)sp->gate * 2;
        for (k = 0; k < 4u; k++)
            if (sp->note[k] && gw)
                cv_rect(x, y + 28 - (sp->note[k] - lo) * 26 / (hi - lo), gw, 3, ink);
        if (sp->morph)                               /* de eigen stand van de golftabel en het filter: twee streepjes eronder */
            cv_rect(x + 6 - (sp->morph < 0 ? -sp->morph * 6 / 64 : 0), y + 34, (sp->morph < 0 ? -sp->morph : sp->morph) * 6 / 64 + 1, 2,
                    in ? K_VIOLET : K_LINE);
        if (sp->cut)
            cv_rect(x + 6 - (sp->cut < 0 ? -sp->cut * 6 / 64 : 0), y + 37, (sp->cut < 0 ? -sp->cut : sp->cut) * 6 / 64 + 1, 2,
                    in ? K_CORAL : K_LINE);
        if (play)
            cv_rect(x, y + 41, 13, 2, K_AMBER);
        if (i == ui.cur)
            box(x - 1, y - 1, 15, 34, K_PAPER);
    }
    cv_blit(0, 28);
}

static void draw_mid(void)               /* 240 x 116: de golftabel van de oscillator, het filter, de toestand */
{
    const gf_osc_t *O = &T.p.o[ui.osc];
    uint32_t o = ui.osc, f, x;
    int32_t now = (int32_t)(live_pos(o) * (float)(GF_NFR - 1) + 0.5f), px0 = 160, i, py = -1;
    char t[20];
    cv_begin(240, 116, K_BG);
    /* de oscillator: zijn letter, de tabel, en de zestien frames achter elkaar; het frame dat nu klinkt licht op */
    cv_rect(4, 4, 16, 16, K_MINT);
    cv_text(8, 4, &FONT_B, OSC_LETTER[o], K_BG);
    cv_text(26, 3, &FONT_T, TAB[O->table % ntab].name, K_PAPER);
    t[0] = 0;
    text_cat(t, WARP_NAME[O->warp % GF_NWARP]);
    cv_text(26, 12, &FONT_T, O->warp % GF_NWARP ? t : "", K_DIM);
    int_text((int32_t)(O->uni < 1u ? 1u : O->uni), t);
    text_cat(t, "x");
    cv_text(150 - text_w(&FONT_T, t), 3, &FONT_T, t, O->uni > 1u ? K_MINT : K_DIM);
    for (f = GF_NFR; f-- > 0;) {
        int32_t bx = 6 + (int32_t)f * 3, by = 100 - (int32_t)f * 4, lx = 0, ly = 0;
        uint16_t c = (int32_t)f == now ? K_PAPER : f & 1u ? K_MINT3 : K_MINT2;
        if (wave_of[o] == 255u)
            break;
        for (x = 0; x < WV_W; x++) {
            int32_t xx = bx + (int32_t)x * 2, yy = by - wave_pt[o][f][x];
            if (x)
                cv_line(lx, ly, xx, yy, c);
            lx = xx;
            ly = yy;
        }
    }
    if (wave_of[o] != 255u) {                        /* het frame van nu nog eens bovenop, in de kleur van de oscillator */
        int32_t bx = 6 + now * 3, by = 100 - now * 4, lx = 0, ly = 0;
        for (x = 0; x < WV_W; x++) {
            int32_t xx = bx + (int32_t)x * 2, yy = by - wave_pt[o][now][x];
            if (x)
                cv_line(lx, ly, xx, yy, K_MINT);
            lx = xx;
            ly = yy;
        }
    }
    /* het filter: wat het doorlaat, van 20 Hz tot 20 kHz */
    box(px0, 4, 76, 44, K_LINE);
    {
        float fc = 20.0f * fm_exp2f((float)T.p.cut * (10.0f / 127.0f)), k = (float)T.p.res * (3.9f / 127.0f);
        uint32_t ty = T.p.ftype % GF_NFILT;
        for (i = 0; i < 74; i++) {
            float w = 20.0f * fm_exp2f((float)i * (10.0f / 73.0f)) / fc, w2 = w * w;
            float re = (1.0f - w2) * (1.0f - w2) - 4.0f * w2 + k, im = 4.0f * w * (1.0f - w2), num, db;
            int32_t y;
            switch (ty) {
            case GF_F_LP12: num = (1.0f + w2) * (1.0f + w2); break;
            case GF_F_BP: num = 4.0f * w2 * (4.0f + w2); break;
            case GF_F_HP: num = w2 * w2 * (1.0f + w2) * (1.0f + w2); break;
            case GF_F_ACID: num = 1.0f + w2; break;
            default: num = 1.0f; break;
            }
            db = 3.0103f * fm_log2f((num + 1e-9f) * (1.0f + k) * (1.0f + k) / (re * re + im * im + 1e-9f));
            y = 20 - (int32_t)(db * 0.55f);
            y = y < 6 ? 6 : y > 46 ? 46 : y;
            if (py >= 0)
                cv_line(px0 + i, py, px0 + 1 + i, y, K_CORAL);
            py = y;
        }
    }
    cv_text(px0, 52, &FONT_T, FILT_NAME[T.p.ftype % GF_NFILT], K_DIM);
    /* de toestand: octaaf, vasthouden, arpeggiator, belasting, noten */
    cv_text(px0, 70, &FONT_T, "OCT", K_DIM);
    t[0] = st.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (st.octave < 0 ? -st.octave : st.octave));
    t[2] = 0;
    cv_text(px0 + 22, 70, &FONT_T, t, K_PAPER);
    cv_text(px0 + 38, 70, &FONT_T, T.seq_on ? "SEQ" : "HLD", T.seq_on || ui.hold ? K_AMBER : K_LINE);
    cv_text(px0 + 60, 70, &FONT_T, "ARP", T.p.arp ? K_MINT : K_LINE);
    cv_text(px0, 84, &FONT_T, MODE_NAME[T.p.mode % GF_NMODE], K_DIM);
    cv_text(px0, 101, &FONT_T, "CPU", K_DIM);
    int_text((int32_t)plat_cpu_pct(), t);
    cv_text(px0 + 22, 101, &FONT_T, t, plat_cpu_pct() > 85u ? K_AMBER : K_PAPER);
    for (f = 0; f < GF_NV; f++)                      /* een blokje per noot die klinkt */
        cv_rect(px0 + 42 + (int32_t)f * 4, 103, 3, 6, T.v[f].on ? (T.v[f].gate ? K_MINT : K_MINT2) : K_LINE);
    cv_blit(0, 28);
}

static void draw_bot(void)               /* 240 x 96: de pagina en haar vier regelaars */
{
    uint32_t k;
    int32_t x = 6;
    int osc_page = ui.page >= PG_WAVE && ui.page <= PG_TUNE;
    cv_begin(240, 96, K_BG);
    cv_rect(0, 0, 240, 1, K_LINE);
    if (osc_page) {                                  /* de oscillator waar de regelaars over gaan */
        cv_rect(6, 5, 16, 18, K_MINT);
        cv_text(10, 6, &FONT_B, OSC_LETTER[ui.osc], K_BG);
        x = 28;
    }
    cv_text(x, 6, &FONT_B, PAGE_NAME[ui.page], K_PAPER);
    for (k = 0; k < NPG; k++)
        cv_rect(234 - (int32_t)(NPG - k) * 6, 12, 4, 4, k == ui.page ? K_AMBER : K_LINE);
    for (k = 0; k < 4u; k++) {
        const par_t *q = &PAGE[ui.page][k];
        int32_t cx = 4 + (int32_t)k * 59, v, w;
        uint16_t ink;
        char t[16];
        static const char *const KN[4] = {"K1", "K2", "K3", "K4"};
        cv_text(cx, 78, &FONT_T, KN[k], q->kind == K_NONE ? K_LINE : K_DIM);
        if (q->kind == K_NONE)
            continue;
        ink = fam_ink(q->fam);
        cv_text(cx, 28, &FONT_T, q->name, k == ui.last_k ? K_PAPER : K_DIM);
        par_text(q, t);
        {
            const felucca_font_t *f = text_w(&FONT_B, t) > 57 ? &FONT_T : &FONT_B;
            uint32_t n = 0;
            while (t[n])
                n++;
            while (n > 1u && text_w(f, t) > 57)      /* een naam die niet past: afkappen */
                t[--n] = 0;
            cv_text(cx, f == &FONT_B ? 42 : 46, f, t, K_PAPER);
        }
        cv_rect(cx, 66, 56, 5, K_LINE);
        v = *par_ptr(q);
        switch (q->kind) {
        case K_TABLE: w = (v + 1) * 56 / (int32_t)ntab; break;
        case K_WARP: w = v * 56 / (GF_NWARP - 1); break;
        case K_FILT: w = (v + 1) * 56 / GF_NFILT; break;
        case K_LFO: w = (v + 1) * 56 / GF_NLFO; break;
        case K_SRC: w = v * 56 / (GF_NSRC - 1); break;
        case K_DST: w = v * 56 / (GF_NDST - 1); break;
        case K_MODE: w = (v + 1) * 56 / GF_NMODE; break;
        case K_ARPM: w = (v + 1) * 56 / GF_NARPMODE; break;
        case K_ARPR: w = (v + 1) * 56 / GF_NARPRATE; break;
        case K_SUBO: w = (v + 1) * 28; break;
        case K_UNI: w = v * 56 / GF_NU; break;
        case K_SLOT: w = (v + 1) * 56 / GF_NMOD; break;
        case K_AOCT: w = (v + 1) * 14; break;
        case K_BPM: w = v * 56 / 200; break;
        case K_SNOTE: w = v * 56 / 127; break;
        case K_GATE: w = v * 56 / (GF_NGATE - 1); break;
        case K_SLEN: w = v * 56 / GF_SEQ_STEPS; break;
        case K_OCT: case K_SEMI: case K_BI: case K_LOCK: {   /* vanuit het midden */
            int32_t s = q->kind == K_BI ? v - 64 : (int8_t)v, full = q->kind == K_BI ? 63 : q->kind == K_OCT ? 3 : q->kind == K_LOCK ? 64 : 12;
            w = (s < 0 ? -s : s) * 28 / full;
            cv_rect(s < 0 ? cx + 28 - w : cx + 28, 66, w ? w : 1, 5, ink);
            w = 0;
            break;
        }
        default: w = v * 56 / 127; break;
        }
        if (w)
            cv_rect(cx, 66, w, 5, ink);
    }
    cv_blit(0, 144);
}

void app_splash(void)                    /* het woordmerk met een golf erdoor, de maker en de versie eronder */
{
    int32_t x, ly = 0;
    lcd_fill(0, 0, 240, 240, K_BG);
    cv_begin(240, 80, K_BG);
    for (x = 0; x < 240; x++) {
        float ph = (float)x * (1.0f / 60.0f);
        int32_t y = 40 - (int32_t)(26.0f * fm_sin_turns(ph) * (0.35f + 0.65f * (float)x * (1.0f / 240.0f)));
        if (x)
            cv_line(x - 1, ly, x, y, K_MINT2);
        ly = y;
    }
    cv_text(120 - text_w(&FONT_L, APP_NAME) / 2, 24, &FONT_L, APP_NAME, K_MINT);
    cv_blit(0, 60);
    cv_begin(240, 44, K_BG);
    cv_text(120 - text_w(&FONT_B, "by BARNIR") / 2, 0, &FONT_B, "by BARNIR", K_PAPER);
    cv_text(120 - text_w(&FONT_T, "FIRMWARE " APP_VERSION) / 2, 24, &FONT_T, "FIRMWARE " APP_VERSION, K_DIM);
    cv_blit(0, 150);
}

void app_redraw(void) { ui.rd_head = ui.rd_mid = ui.rd_bot = 1; }   /* de spiegel over USB wil het hele scherm */

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, K_BG);
    ui.last_k = 255;
    ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
}

static void speak(void)                  /* een schermlezer die net meeluistert hoort waar je bent */
{
    char t[80], n[28];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". PRESET ");
        preset_name(ui.preset, n);
        text_cat(t, n);
        text_cat(t, ". PAGE ");
        text_cat(t, PAGE_NAME[ui.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    speak();
    if (midi_pc) {                                   /* een programmawissel over MIDI */
        uint32_t i = midi_pc - 1u;
        midi_pc = 0;
        if (i < NFACT)
            preset_load(order[i]);
    }
    if (want_status) {
        want_status = 0;
        status_say();
    }
    if (T.seq_step != ui.seen_step) {                /* de reeks loopt: de stap op het scherm */
        ui.seen_step = T.seq_step;
        if (on_seq_page())
            ui.rd_mid = 1;
    }
    if (poked) {
        poked = 0;
        ui.dirty = 1;
        ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
    }
    tables_sync();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.rd_head = 1;
    }
    if (now - ui.recover_at >= 10000u) {             /* na een overbelasting komen de stemmen langzaam terug */
        ui.recover_at = now;
        if (T.uni_cap < GF_NU && plat_cpu_pct() < 55u && !cap_lock)
            T.uni_cap++;
    }
    if (now - ui.mid_at >= (gf_voices(&T) ? 66u : 250u))   /* het frame beweegt mee: 15 keer per seconde */
        ui.rd_mid = 1;
    if (ui.rd_head) {
        ui.rd_head = 0;
        draw_head();
    }
    if (ui.rd_mid) {
        ui.rd_mid = 0;
        ui.mid_at = now;
        if (on_seq_page())
            draw_seq();
        else
            draw_mid();
    }
    if (ui.rd_bot) {
        ui.rd_bot = 0;
        draw_bot();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = st.octave;
    else if (!strcmp(what, "preset"))
        *got = ui.preset;
    else if (!strcmp(what, "oct"))
        *got = (uint32_t)(st.octave + 3);
    else if (!strcmp(what, "seq"))
        *got = T.seq_on;
    else if (!strcmp(what, "rec"))
        *got = ui.rec;
    else if (!strcmp(what, "cur"))
        *got = ui.cur;
    else if (!strcmp(what, "seqlen"))
        *got = T.seq.len;
    else if (!strcmp(what, "snote"))
        *got = T.seq.s[ui.cur % GF_SEQ_STEPS].note[0];
    else if (!strcmp(what, "seqstep"))
        *got = T.seq_step;
    else if (!strcmp(what, "place"))
        *got = ui.preset < NFACT ? place[ui.preset] : -1;
    else if (!strcmp(what, "cat"))
        *got = ui.preset < NFACT ? GF_PRESETS[ui.preset].cat : -1;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "osc"))
        *got = ui.osc;
    else if (!strcmp(what, "dirty"))
        *got = ui.dirty;
    else if (!strcmp(what, "hold"))
        *got = ui.hold;
    else if (!strcmp(what, "arp"))
        *got = T.p.arp;
    else if (!strcmp(what, "voices"))
        *got = (int)gf_voices(&T);
    else if (!strcmp(what, "held")) {
        uint32_t i;
        for (*got = 0, i = 0; i < GF_NV; i++)
            *got += T.v[i].on && T.v[i].gate;
    } else if (!strcmp(what, "users")) {
        uint32_t i;
        for (*got = 0, i = 0; i < NUSER; i++)
            *got += user.used[i];
    } else if (!strncmp(what, "par", 3) && what[3] >= '0' && what[3] <= '3') {   /* parN: regelaar N van de pagina */
        const par_t *q = &PAGE[ui.page][what[3] - '0'];
        *got = q->kind == K_OCT || q->kind == K_SEMI ? (int8_t)*par_ptr(q) : *par_ptr(q);
    } else if (!strcmp(what, "table"))
        *got = T.loaded[ui.osc];
    else if (!strcmp(what, "tables"))
        *got = (int)ntab;
    else if (!strcmp(what, "unicap"))
        *got = T.uni_cap;
    else
        return 0;
    return 1;
}
#endif
