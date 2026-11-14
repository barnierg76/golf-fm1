/* Golf voor de FM-1: de klankmotor. Copyright (C) 2026 BARNIR. Alle rechten voorbehouden.
 *
 * Een wavetable-synthesizer met zes noten tegelijk. Per noot: twee oscillatoren die elk door een golftabel van 16
 * frames morphen, tot zeven stemmen unisono met spreiding, een warp (sync, bend, asym, PWM, fold, FM), een sub en ruis;
 * een ladderfilter met drive; twee envelopes, twee LFO's en zes modulatieroutes. Daarna drive, een compressor in drie
 * banden (OTT), chorus, delay en galm, en een arpeggiator ervoor.
 *
 * Alles rekent met float (fm1-base include/fastmath.h, geen libm); de oscillatoren zelf met hele getallen.
 * De tabellen staan als harmonischen in het flashgeheugen (tools/pack_tables.py) en worden bij het kiezen omgezet in
 * golven voor elk octaaf (gf_tables_sync, vanuit de hoofdlus), zodat er boven de halve samplefrequentie niets klinkt. */
#pragma once
#include <stdint.h>

#define GF_FS 44100
#define GF_BLK 64                      /* samples per blok: zo vaak worden envelopes, LFO's en routes berekend */
#define GF_NV 8                        /* noten tegelijk */
#define GF_NU 7                        /* stemmen unisono per oscillator */
#define GF_NFR 16                      /* frames per tabel */
#define GF_NLEV 8                      /* per frame acht golven met 255, 127 .. 1 harmonischen; de hoge ruim bemeten,
                                        * zodat het lezen tussen twee punten geen valse tonen geeft */
#define GF_MIP_LEN (GF_NFR * 2368)     /* samples (int16) per oscillator: 512, 512, 512, 256, 256, 128, 128, 64 */
#define GF_DLY_LEN 16384               /* delay: 0,74 s per kanaal, op de halve samplefrequentie */
#define GF_RV_LEN 2048                 /* galm: vier lijnen */
#define GF_RV_MEM (4 * GF_RV_LEN + 1024)
#define GF_CH_LEN 1024                 /* chorus: 23 ms per kanaal */
#define GF_NMOD 6
#define GF_NARP 12

typedef struct {
    const uint8_t *d;                  /* per frame: aantal harmonischen, dan (sterkte, fase) per harmonische */
    uint8_t h;                         /* de meeste harmonischen van een frame */
    float norm;                        /* maakt de hoogste piek van de tabel 1 */
    const char *name;
} gf_table_t;

enum { GF_W_OFF, GF_W_SYNC, GF_W_BEND, GF_W_ASYM, GF_W_PWM, GF_W_FOLD, GF_W_FM, GF_NWARP };
enum { GF_F_LP24, GF_F_LP12, GF_F_BP, GF_F_HP, GF_F_ACID, GF_NFILT };
enum { GF_L_SINE, GF_L_TRI, GF_L_SAW, GF_L_SQUARE, GF_L_SH, GF_L_DRIFT, GF_NLFO };
enum { GF_S_NONE, GF_S_ENV2, GF_S_LFO1, GF_S_LFO2, GF_S_VELO, GF_S_PRESS, GF_S_WHEEL, GF_S_KEY, GF_S_RAND, GF_NSRC };
enum { GF_D_NONE, GF_D_PITCH, GF_D_POSA, GF_D_POSB, GF_D_POSAB, GF_D_CUT, GF_D_RES, GF_D_WARPA, GF_D_WARPB, GF_D_LEVA,
       GF_D_LEVB, GF_D_DETUNE, GF_D_PAN, GF_D_LFO1, GF_D_LFO2, GF_D_NOISE, GF_D_SUB, GF_D_AMP, GF_NDST };
enum { GF_M_POLY, GF_M_MONO, GF_M_LEGATO, GF_NMODE };
enum { GF_A_UP, GF_A_DOWN, GF_A_UPDOWN, GF_A_RANDOM, GF_A_PLAYED, GF_NARPMODE };
#define GF_NARPRATE 6                  /* 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 */

typedef struct {
    uint8_t table, pos, level;
    int8_t oct, semi;                  /* -3 .. 3, -12 .. 12 */
    uint8_t fine;                      /* 64 = zuiver, +-50 cent */
    uint8_t uni, detune, width;        /* 1 .. 7 stemmen, hoe ver uit elkaar, hoe breed */
    uint8_t warp, wamt;
} gf_osc_t;
typedef struct { uint8_t a, d, s, r; } gf_env_t;
typedef struct { uint8_t shape, rate; } gf_lfo_t;
typedef struct { uint8_t src, dst, amt; } gf_mod_t;   /* amt 64 = niets, 0 en 127 de uitersten */

typedef struct {
    gf_osc_t o[2];
    uint8_t sub, sub_oct, noise;       /* niveau, 0 = een octaaf lager / 1 = twee, niveau */
    uint8_t ftype, cut, res, drive, fenv, ftrk;   /* fenv 64 = niets */
    gf_env_t e[2];                     /* 0 het volume, 1 het filter en de routes */
    gf_lfo_t l[2];
    gf_mod_t m[GF_NMOD];
    uint8_t mode, glide;
    uint8_t fx_drive, ott, cho, dly_time, dly_fb, dly_mix, rev_size, rev_mix;
    uint8_t arp, arp_mode, arp_rate, arp_oct, arp_gate, bpm;   /* bpm: 40 + waarde */
    uint8_t master;
} gf_patch_t;

/* De stepsequencer: hooguit 32 stappen, elk met tot vier noten, een aanslag, een lengte en een eigen stand van de
 * golftabel en het filter (bovenop de klank). */
#define GF_SEQ_STEPS 32
enum { GF_G_REST, GF_G_25, GF_G_50, GF_G_75, GF_G_100, GF_G_TIE, GF_NGATE };
typedef struct {
    uint8_t note[4];                   /* 0 = geen */
    uint8_t vel, gate;                 /* GF_G_...: rust, deel van de stap, of door tot de volgende stap */
    int8_t morph, cut;                 /* -64 .. 63: erbij op MORPH (van 127) en op CUTOFF */
} gf_step_t;
typedef struct {
    uint8_t len, rate, swing, rsv;     /* 1 .. 32 stappen; het tempo-deel als de arpeggiator; swing 0 .. 127 */
    gf_step_t s[GF_SEQ_STEPS];
} gf_seq_t;

typedef struct {
    uint8_t on, gate, note, vel, press, stage[2], steal;
    uint32_t age;
    float env[2], pitch, target, lph[2], lval[2], lrnd[2][2], rnd;
    uint32_t ph[2][GF_NU], sph, nz, nz2;
    int32_t fb;                        /* FM van oscillator B op zichzelf: zijn vorige sample */
    float amp, f[4], sd[2], G, kq, posq[2];
} gf_voice_t;

typedef struct {
    gf_patch_t p;
    gf_voice_t v[GF_NV];
    const gf_table_t *tab;
    uint32_t ntab, age, rng;
    int16_t *mip[2];
    volatile uint8_t loaded[2], busy[2];   /* de tabel die in mip[] staat (255: geen); bezig met omzetten */
    int16_t sine[512];
    float tw[512];                     /* cos en sin voor de omzetting */
    /* spelen */
    uint8_t wheel, chpress, uni_cap;   /* uni_cap: hooguit zoveel stemmen unisono (de app verlaagt dit bij overbelasting) */
    float bend;                        /* halve tonen */
    float last;                        /* de toonhoogte van de vorige noot: daar glijdt de volgende vandaan */
    uint8_t held[GF_NARP], heldv[GF_NARP], nheld, arp_note, arp_on, mono_n;
    uint8_t stack[GF_NARP];            /* mono: de ingedrukte toetsen, de laatste bovenop */
    int32_t arp_step, arp_dir;
    float arp_t, arp_len;
    /* effecten */
    float ott_lp[2][2], ott_env[3], ott_g[3];
    float *ch, *rv;
    int16_t *dly;
    uint32_t ch_w, dly_w, rv_w;
    float ch_ph, dly_lp[2], rv_lp[4], half[2], wet[2][2], dly_len;
    uint32_t odd;
    /* de stepsequencer */
    gf_seq_t seq;
    uint8_t seq_on, seq_tie, seq_nh, seq_held[4];
    int8_t seq_tr;                     /* alle noten zoveel halve tonen hoger of lager */
    int32_t seq_step;                  /* de stap die nu klinkt (-1: nog geen) */
    float seq_t, seq_gate, seq_morph, seq_cut;
    int (*fork)(void (*job)(uint32_t), uint32_t arg);   /* een tweede rekenkern, of 0: de app zet ze na gf_init */
    int (*join)(void);
    uint32_t peak_voices;              /* het grootste aantal stemmen in een blok, voor de test */
} gf_t;

void gf_init(gf_t *t, const gf_table_t *tab, uint32_t ntab, int16_t *mip0, int16_t *mip1 /* elk GF_MIP_LEN */,
             int16_t *dly /* 2 * GF_DLY_LEN */, float *rv /* GF_RV_MEM */, float *ch /* 2 * GF_CH_LEN */);
void gf_seq_run(gf_t *t, int on);      /* de sequencer starten (vanaf stap 1) of stoppen */
void gf_tables_sync(gf_t *t);          /* hoofdlus: zet de tabellen van de klank om als ze veranderd zijn */
void gf_note_on(gf_t *t, uint32_t note, uint32_t vel);
void gf_note_off(gf_t *t, uint32_t note);
void gf_pressure(gf_t *t, uint32_t note, uint32_t value);   /* de druk op een toets (0 .. 127) */
void gf_all_off(gf_t *t);
void gf_render(gf_t *t, int32_t *lr, uint32_t n);   /* n veelvoud van GF_BLK; stereo, 24 bits, 44,1 kHz */
uint32_t gf_voices(const gf_t *t);
int16_t gf_wave_at(const gf_t *t, uint32_t osc, uint32_t frame, uint32_t x);   /* voor het scherm: x 0 .. 511 */
