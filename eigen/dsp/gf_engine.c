/* Golf voor de FM-1: de klankmotor. Copyright (C) 2026 BARNIR. Alle rechten voorbehouden. Zie gf_engine.h. */
#include "gf_engine.h"
#include "fastmath.h"

#define DT ((float)GF_BLK / (float)GF_FS)
/* De code van een stem draait op het apparaat vanuit het werkgeheugen (sectie .app_ram van fm1-base), alles in een
 * functie: buiten bereik van de wachttijden van het flashgeheugen. Op de Mac gewone code. */
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#define GF_HOT
#else
#define GF_HOT __attribute__((section(".app_ram"), flatten, noinline))
#endif
typedef struct {
    int32_t aL[GF_BLK], aR[GF_BLK], mono[GF_BLK];
} scr_t;                               /* het kladblok van een stem: een per rekenkern */
static const uint16_t LEV_N[GF_NLEV] = {512, 512, 512, 256, 256, 128, 128, 64};        /* samples per frame */
static const uint8_t LEV_SH[GF_NLEV] = {23, 23, 23, 24, 24, 25, 25, 26};               /* fase (32 bits) -> plek */
static const uint16_t LEV_OFF[GF_NLEV] = {0, 8192, 16384, 24576, 28672, 32768, 34816, 36864};   /* 16 x de som ervoor */

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float par(uint32_t v) { return (float)v * (1.0f / 127.0f); }
static float secs(uint32_t v) { return 0.001f * fm_exp2f((float)v * (13.3f / 127.0f)); }   /* 1 ms .. 10 s */
static float limit(float x)            /* de uitgang: recht tot 0,7, daarboven zacht naar 1 */
{
    float a = fm_fabsf(x), u;
    if (a <= 0.7f)
        return x;
    u = (a - 0.7f) * (1.0f / 0.3f);
    a = 0.7f + 0.3f * u / (1.0f + u);
    return x < 0.0f ? -a : a;
}
static float soft(float x)             /* verzadiging: recht bij kleine waarden, vlak op 1 vanaf 1,5 */
{
    x = clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static uint32_t rnd(gf_t *t)
{
    t->rng ^= t->rng << 13;
    t->rng ^= t->rng >> 17;
    t->rng ^= t->rng << 5;
    return t->rng;
}
static float rndf(gf_t *t) { return (float)(int32_t)rnd(t) * (1.0f / 2147483648.0f); }   /* -1 .. 1 */

/* ------------------------------------------------------------ de tabellen --- */
static void ifft(float *re, float *im, uint32_t n, const float *tw)   /* x[i] = som X[k] e^(+2 pi i k / n), n <= 512 */
{
    uint32_t i, j = 0, k, len;
    for (i = 1; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float a = re[i], b = im[i];
            re[i] = re[j]; im[i] = im[j];
            re[j] = a; im[j] = b;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        uint32_t half = len >> 1, step = 512u / len;
        for (i = 0; i < n; i += len)
            for (k = 0; k < half; k++) {
                float c = tw[2u * k * step], s = tw[2u * k * step + 1u];
                float xr = re[i + k + half] * c - im[i + k + half] * s, xi = re[i + k + half] * s + im[i + k + half] * c;
                re[i + k + half] = re[i + k] - xr; im[i + k + half] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}

static void table_build(gf_t *t, uint32_t o, const gf_table_t *tab)
{
    static float re[512], im[512], ar[256], ai[256];
    const uint8_t *d = tab->d;
    uint32_t fr, L, k, i;
    for (fr = 0; fr < GF_NFR; fr++) {
        uint32_t h = *d++;
        for (k = 0; k < h; k++) {                    /* x = som a sin(k w + fase): X[k] = a / 2 e^(i (fase - pi / 2)) */
            float a = d[2u * k] ? 0.5f * fm_exp2f(((float)d[2u * k] - 255.0f) * (12.0f / 255.0f)) : 0.0f;
            float turns = (float)d[2u * k + 1u] * (1.0f / 256.0f) - 0.25f;
            ar[k] = a * fm_sin_turns(turns + 0.25f);
            ai[k] = a * fm_sin_turns(turns);
        }
        d += 2u * h;
        for (L = 0; L < GF_NLEV; L++) {
            uint32_t n = LEV_N[L], hc = (256u >> L) - 1u, kmax = h < hc ? h : hc;
            int16_t *out = t->mip[o] + LEV_OFF[L] + fr * n;
            if (!kmax && h)
                kmax = 1;
            for (i = 0; i < n; i++)
                re[i] = im[i] = 0.0f;
            for (k = 1; k <= kmax; k++) {
                re[k] = re[n - k] = ar[k - 1u];
                im[k] = ai[k - 1u];
                im[n - k] = -ai[k - 1u];
            }
            ifft(re, im, n, t->tw);
            for (i = 0; i < n; i++)
                out[i] = (int16_t)clampf(re[i] * tab->norm * 30000.0f, -32767.0f, 32767.0f);
        }
    }
}

void gf_tables_sync(gf_t *t)
{
    uint32_t o;
    for (o = 0; o < 2u; o++) {
        uint32_t want = t->p.o[o].table % t->ntab;
        if (t->loaded[o] != want) {
            t->busy[o] = 1;
            table_build(t, o, &t->tab[want]);
            t->loaded[o] = (uint8_t)want;
            t->busy[o] = 0;
        }
    }
}

int16_t gf_wave_at(const gf_t *t, uint32_t osc, uint32_t frame, uint32_t x)
{
    return t->mip[osc & 1u][(frame % GF_NFR) * 512u + (x & 511u)];   /* de volle golf (255 harmonischen) */
}

void gf_init(gf_t *t, const gf_table_t *tab, uint32_t ntab, int16_t *mip0, int16_t *mip1, int16_t *dly, float *rv, float *ch)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)t;
    gf_patch_t keep = t->p;
    gf_seq_t keepseq = t->seq;
    for (i = 0; i < sizeof *t; i++)
        z[i] = 0;
    t->p = keep;
    t->seq = keepseq;
    t->tab = tab;
    t->ntab = ntab;
    t->mip[0] = mip0;
    t->mip[1] = mip1;
    t->dly = dly;
    t->rv = rv;
    t->ch = ch;
    t->loaded[0] = t->loaded[1] = 255;
    t->rng = 0x2545F491u;
    t->uni_cap = GF_NU;
    t->last = 60.0f;
    t->dly_len = 4000.0f;
    for (i = 0; i < 512u; i++)
        t->sine[i] = (int16_t)(fm_sin_turns((float)i * (1.0f / 512.0f)) * 32767.0f);
    for (i = 0; i < 256u; i++) {
        t->tw[2u * i] = fm_sin_turns((float)i * (1.0f / 512.0f) + 0.25f);
        t->tw[2u * i + 1u] = fm_sin_turns((float)i * (1.0f / 512.0f));
    }
    for (i = 0; i < 2u * GF_DLY_LEN; i++)
        dly[i] = 0;
    for (i = 0; i < GF_RV_MEM; i++)
        rv[i] = 0.0f;
    for (i = 0; i < 2u * GF_CH_LEN; i++)
        ch[i] = 0.0f;
    for (i = 0; i < 3u; i++)
        t->ott_g[i] = 1.0f;
}

/* ---------------------------------------------------------------- noten --- */
static void voice_start(gf_t *t, gf_voice_t *v, uint32_t note, uint32_t vel, int retrig)
{
    uint32_t o, u;
    if (!v->on) {                                    /* een verse stem; een die nog klinkt houdt zijn fases en filter */
        v->env[0] = v->env[1] = 0.0f;
        v->amp = 0.0f;
        for (o = 0; o < 2u; o++)
            for (u = 0; u < GF_NU; u++)
                v->ph[o][u] = rnd(t);
        v->f[0] = v->f[1] = v->f[2] = v->f[3] = v->sd[0] = v->sd[1] = 0.0f;
        v->G = -1.0f;
        v->lph[0] = v->lph[1] = 0.0f;
        v->lrnd[0][0] = v->lrnd[1][0] = 0.0f;
        v->lrnd[0][1] = rndf(t);
        v->lrnd[1][1] = rndf(t);
        v->lval[0] = v->lval[1] = 0.0f;
        v->rnd = rndf(t);
        v->nz2 = rnd(t);
        v->fb = 0;
        v->press = 0;
        v->posq[0] = v->posq[1] = -1.0f;
        v->pitch = t->p.glide ? t->last : (float)note;
        v->on = 1;
        retrig = 1;
    }
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->target = (float)note;
    v->gate = 1;
    v->age = ++t->age;
    if (retrig)
        v->stage[0] = v->stage[1] = 1;
    t->last = (float)note;
}

static void play_on(gf_t *t, uint32_t note, uint32_t vel)
{
    gf_voice_t *v = 0;
    uint32_t i;
    if (t->p.mode != GF_M_POLY) {                    /* een stem; de ingedrukte toetsen op een stapel */
        int others = t->mono_n > 0;
        for (i = 0; i < t->mono_n; i++)
            if (t->stack[i] == note)
                break;
        if (i == t->mono_n && t->mono_n < GF_NARP)
            t->stack[t->mono_n++] = (uint8_t)note;
        voice_start(t, &t->v[0], note, vel, !(t->p.mode == GF_M_LEGATO && others && t->v[0].on && t->v[0].gate));
        return;
    }
    for (i = 0; i < GF_NV && !v; i++)                /* dezelfde noot nog eens: dezelfde stem */
        if (t->v[i].on && t->v[i].note == note)
            v = &t->v[i];
    for (i = 0; i < GF_NV && !v; i++)
        if (!t->v[i].on)
            v = &t->v[i];
    if (!v) {                                        /* de oudste die al losgelaten is, anders de oudste */
        uint32_t best = 0xFFFFFFFFu;
        int rel = 0;
        for (i = 0; i < GF_NV; i++) {
            int r = !t->v[i].gate;
            if (r > rel || (r == rel && t->v[i].age < best)) {
                rel = r;
                best = t->v[i].age;
                v = &t->v[i];
            }
        }
    }
    voice_start(t, v, note, vel, 1);
}

static void play_off(gf_t *t, uint32_t note)
{
    uint32_t i, k;
    if (t->p.mode != GF_M_POLY) {
        for (i = 0; i < t->mono_n; i++)
            if (t->stack[i] == note) {
                for (k = i; k + 1u < t->mono_n; k++)
                    t->stack[k] = t->stack[k + 1u];
                t->mono_n--;
                break;
            }
        if (t->mono_n) {                             /* terug naar de toets eronder */
            if (t->v[0].on && t->v[0].note == note) {
                t->v[0].note = t->stack[t->mono_n - 1u];
                t->v[0].target = (float)t->v[0].note;
                t->last = t->v[0].target;
                if (t->p.mode == GF_M_MONO)
                    t->v[0].stage[0] = t->v[0].stage[1] = 1;
            }
        } else if (t->v[0].on && t->v[0].gate) {
            t->v[0].gate = 0;
            t->v[0].stage[0] = t->v[0].stage[1] = 4;
        }
        return;
    }
    for (i = 0; i < GF_NV; i++)
        if (t->v[i].on && t->v[i].gate && t->v[i].note == note) {
            t->v[i].gate = 0;
            t->v[i].stage[0] = t->v[i].stage[1] = 4;
        }
}

void gf_note_on(gf_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i;
    note &= 127u;
    if (!t->p.arp) {
        play_on(t, note, vel);
        return;
    }
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;
    if (t->nheld < GF_NARP) {
        if (!t->nheld) {                             /* de eerste toets: de arpeggiator begint meteen */
            t->arp_t = 0.0f;
            t->arp_step = -1;
            t->arp_dir = 1;
        }
        t->held[t->nheld] = (uint8_t)note;
        t->heldv[t->nheld++] = (uint8_t)vel;
    }
}

void gf_note_off(gf_t *t, uint32_t note)
{
    uint32_t i, k;
    note &= 127u;
    if (!t->p.arp && !t->nheld) {
        play_off(t, note);
        return;
    }
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note) {
            for (k = i; k + 1u < t->nheld; k++) {
                t->held[k] = t->held[k + 1u];
                t->heldv[k] = t->heldv[k + 1u];
            }
            t->nheld--;
            break;
        }
    if (!t->nheld && t->arp_on) {
        play_off(t, t->arp_note);
        t->arp_on = 0;
    }
    if (!t->p.arp)
        play_off(t, note);
}

void gf_pressure(gf_t *t, uint32_t note, uint32_t value)
{
    uint32_t i;
    for (i = 0; i < GF_NV; i++)
        if (t->v[i].on && t->v[i].note == note)
            t->v[i].press = (uint8_t)(value > 127u ? 127u : value);
}

void gf_all_off(gf_t *t)
{
    uint32_t i;
    t->nheld = 0;
    t->mono_n = 0;
    t->arp_on = 0;
    for (i = 0; i < GF_NV; i++)
        if (t->v[i].on && t->v[i].gate) {
            t->v[i].gate = 0;
            t->v[i].stage[0] = t->v[i].stage[1] = 4;
        }
}

uint32_t gf_voices(const gf_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < GF_NV; i++)
        n += t->v[i].on;
    return n;
}

static void arp_tick(gf_t *t)          /* een keer per blok */
{
    static const float MULT[GF_NARPRATE] = {1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
    float step = 60.0f / (40.0f + (float)t->p.bpm) * MULT[t->p.arp_rate % GF_NARPRATE] * (float)GF_FS;
    uint8_t list[GF_NARP];
    uint32_t n = t->nheld, i, k, oct = 1u + t->p.arp_oct % 4u, total, idx;
    if (t->arp_on) {
        t->arp_len -= (float)GF_BLK;
        if (t->arp_len <= 0.0f) {
            play_off(t, t->arp_note);
            t->arp_on = 0;
        }
    }
    if (!t->p.arp || !n)
        return;
    t->arp_t -= (float)GF_BLK;
    if (t->arp_t > 0.0f)
        return;
    t->arp_t += step;
    if (t->arp_t < 0.0f)
        t->arp_t = step;
    for (i = 0; i < n; i++)
        list[i] = t->held[i];
    if (t->p.arp_mode != GF_A_PLAYED)                /* van laag naar hoog */
        for (i = 1; i < n; i++)
            for (k = i; k > 0 && list[k - 1u] > list[k]; k--) {
                uint8_t s = list[k];
                list[k] = list[k - 1u];
                list[k - 1u] = s;
            }
    total = n * oct;
    switch (t->p.arp_mode % GF_NARPMODE) {
    case GF_A_DOWN: t->arp_step = t->arp_step <= 0 ? (int32_t)total - 1 : t->arp_step - 1; break;
    case GF_A_UPDOWN:
        if (total < 2u) {
            t->arp_step = 0;
        } else {
            if (t->arp_step < 0)
                t->arp_step = -1, t->arp_dir = 1;
            t->arp_step += t->arp_dir;
            if (t->arp_step >= (int32_t)total)
                t->arp_step = (int32_t)total - 2, t->arp_dir = -1;
            else if (t->arp_step < 0)
                t->arp_step = 1, t->arp_dir = 1;
        }
        break;
    case GF_A_RANDOM: t->arp_step = (int32_t)(rnd(t) % total); break;
    default: t->arp_step = (t->arp_step + 1) % (int32_t)total; break;
    }
    idx = (uint32_t)t->arp_step % total;
    if (t->arp_on)
        play_off(t, t->arp_note);
    i = list[idx % n] + 12u * (idx / n);
    t->arp_note = (uint8_t)(i > 127u ? 127u : i);
    for (k = 0; k < n && t->held[k] != list[idx % n]; k++)
        ;
    play_on(t, t->arp_note, t->heldv[k < n ? k : 0]);
    t->arp_on = 1;
    t->arp_len = step * (0.1f + 0.85f * par(t->p.arp_gate));
}

/* ---------------------------------------------------------- stepsequencer --- */
static void seq_release(gf_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_nh; i++)
        gf_note_off(t, t->seq_held[i]);
    t->seq_nh = 0;
}
void gf_seq_run(gf_t *t, int on)
{
    seq_release(t);
    t->seq_on = (uint8_t)(on != 0);
    t->seq_step = -1;
    t->seq_t = 0.0f;
    t->seq_gate = 0.0f;
    t->seq_tie = 0;
    t->seq_morph = t->seq_cut = 0.0f;
}
static void seq_tick(gf_t *t)          /* een keer per blok */
{
    static const float MULT[GF_NARPRATE] = {1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
    static const float FRAC[GF_NGATE] = {0.0f, 0.25f, 0.5f, 0.75f, 0.94f, 1.0f};
    float step, next, sw;
    const gf_step_t *s;
    uint8_t now[4];
    uint32_t i, k, n = 0, len;
    if (!t->seq_on)
        return;
    if (t->seq_gate > 0.0f) {
        t->seq_gate -= (float)GF_BLK;
        if (t->seq_gate <= 0.0f)
            seq_release(t);
    }
    t->seq_t -= (float)GF_BLK;
    if (t->seq_t > 0.0f)
        return;
    step = 60.0f / (40.0f + (float)t->p.bpm) * MULT[t->seq.rate % GF_NARPRATE] * (float)GF_FS;
    len = t->seq.len < 1u ? 1u : t->seq.len > GF_SEQ_STEPS ? GF_SEQ_STEPS : t->seq.len;
    t->seq_step = (t->seq_step + 1) % (int32_t)len;
    sw = par(t->seq.swing) * 0.5f * step;            /* swing: de oneven stappen later */
    next = t->seq_step & 1 ? step - sw : step + sw;
    t->seq_t += next;
    if (t->seq_t <= 0.0f)
        t->seq_t = next;
    s = &t->seq.s[t->seq_step];
    for (i = 0; i < 4u; i++)
        if (s->note[i]) {
            int32_t v = (int32_t)s->note[i] + t->seq_tr;
            now[n++] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        }
    if (s->gate % GF_NGATE == GF_G_REST || !n) {     /* rust */
        seq_release(t);
        t->seq_gate = 0.0f;
        t->seq_tie = 0;
        return;
    }
    t->seq_morph = (float)s->morph * (1.0f / 127.0f);
    t->seq_cut = (float)s->cut;
    if (t->seq_tie) {                                /* gebonden: eerst de nieuwe noten, dan de oude los (legato glijdt) */
        for (i = 0; i < n; i++) {
            for (k = 0; k < t->seq_nh && t->seq_held[k] != now[i]; k++)
                ;
            if (k == t->seq_nh)
                gf_note_on(t, now[i], s->vel ? s->vel : 100u);
        }
        for (k = 0; k < t->seq_nh; k++) {
            for (i = 0; i < n && now[i] != t->seq_held[k]; i++)
                ;
            if (i == n)
                gf_note_off(t, t->seq_held[k]);
        }
    } else {
        seq_release(t);
        for (i = 0; i < n; i++)
            gf_note_on(t, now[i], s->vel ? s->vel : 100u);
    }
    for (i = 0; i < n; i++)
        t->seq_held[i] = now[i];
    t->seq_nh = (uint8_t)n;
    t->seq_tie = s->gate % GF_NGATE == GF_G_TIE;
    t->seq_gate = t->seq_tie ? 0.0f : next * FRAC[s->gate % GF_NGATE];
}

/* ------------------------------------------------------------ oscillator --- */
/* een sample uit de tabel op fase pp: tussen twee punten, en tussen twee frames (wq van 16384) */
#define RD(pp)                                                                                                         \
    do {                                                                                                               \
        uint32_t q_ = (pp), ix_ = q_ >> sh;                                                                            \
        int32_t fr_ = (int32_t)((q_ >> (sh - 14u)) & 0x3FFFu);                                                         \
        int32_t a0_ = f0[ix_], b0_ = f1[ix_];                                                                          \
        a0_ += (f0[(ix_ + 1u) & mask] - a0_) * fr_ >> 14;                                                              \
        b0_ += (f1[(ix_ + 1u) & mask] - b0_) * fr_ >> 14;                                                              \
        s = a0_ + ((b0_ - a0_) * wq >> 14);                                                                            \
    } while (0)
#define OUT()                                                                                                          \
    do {                                                                                                               \
        aL[i] += s * gl >> 12;                                                                                         \
        aR[i] += s * gr >> 12;                                                                                         \
    } while (0)

static uint32_t osc_run(const gf_t *t, gf_voice_t *v, uint32_t o, float note, float pos, float wamt, float lev, float det, float panm,
                        int32_t *aL, int32_t *aR, int32_t *mono)
{
    const gf_osc_t *O = &t->p.o[o];
    uint32_t n = O->uni < 1u ? 1u : O->uni > GF_NU ? GF_NU : O->uni, u, i, L = 0, sh, mask, mode = O->warp % GF_NWARP;
    float pitch = note + 12.0f * (float)O->oct + (float)O->semi + ((float)O->fine - 64.0f) * (0.5f / 63.0f);
    float f = 440.0f * fm_exp2f((pitch - 69.0f) * (1.0f / 12.0f)), slope = 1.0f, need, fpos, g;
    const int16_t *f0, *f1;
    int32_t wq, fi, k1 = 0, k2 = 0;
    uint32_t pivot = 0;
    if (n > t->uni_cap)
        n = t->uni_cap;
    if (f > 16000.0f)
        f = 16000.0f;
    wamt = clampf(wamt, 0.0f, 1.0f);
    switch (mode) {
    case GF_W_SYNC: case GF_W_PWM: slope = 1.0f + 7.0f * wamt; k1 = (int32_t)(slope * 256.0f); break;
    case GF_W_BEND: k1 = (int32_t)((wamt * 2.0f - 1.0f) * 32767.0f); slope = 1.0f + fm_fabsf(wamt * 2.0f - 1.0f); break;
    case GF_W_ASYM: {
        float m = 0.5f - 0.4375f * wamt;
        pivot = (uint32_t)(m * 4294967296.0f);
        slope = 0.5f / m;
        k1 = (int32_t)(slope * 256.0f);
        k2 = (int32_t)(0.5f / (1.0f - m) * 256.0f);
        break;
    }
    case GF_W_FOLD: slope = 1.0f + 7.0f * wamt; k1 = (int32_t)(slope * 256.0f); slope = 1.0f + 3.0f * wamt; break;
    case GF_W_FM: k1 = (int32_t)(wamt * (o ? 90000.0f : 200000.0f)); slope = 1.0f + 4.0f * wamt; break;
    default: break;
    }
    need = f * slope * (256.0f / 21000.0f);          /* de golf met de meeste harmonischen die nog onder 21 kHz blijven */
    while (need > 1.0f && L < GF_NLEV - 1u) {
        need *= 0.5f;
        L++;
    }
    sh = LEV_SH[L];
    mask = LEV_N[L] - 1u;
    fpos = clampf(pos, 0.0f, 1.0f) * (float)(GF_NFR - 1);
    fi = (int32_t)fpos;
    if (fi > GF_NFR - 2)
        fi = GF_NFR - 2;
    wq = (int32_t)((fpos - (float)fi) * 16384.0f);
    f0 = t->mip[o] + LEV_OFF[L] + (uint32_t)fi * LEV_N[L];
    f1 = f0 + LEV_N[L];
    g = lev * fm_rsqrtf((float)n) * 4096.0f;
    for (u = 0; u < n; u++) {
        float offs = n > 1u ? -1.0f + 2.0f * (float)u / (float)(n - 1u) : 0.0f;
        float pan = clampf((u & 1u ? -offs : offs) * par(O->width) + panm, -1.0f, 1.0f);
        /* de ontstemming is klein (hooguit 60 cent): 2^x als 1 + x ln 2 + ...; de plek links-rechts als kromme door
         * 1, 0,707 en 0 (samen bijna even hard): geen dure functies per stem per blok */
        float dx = offs * det * (60.0f / 1200.0f) * FM_LN2;
        uint32_t inc = (uint32_t)(f * (1.0f + dx * (1.0f + dx * 0.5f)) * (4294967296.0f / (float)GF_FS)), ph = v->ph[o][u];
        int32_t gl = (int32_t)(g * (1.0f - pan * (0.7071f + 0.2929f * pan))),
                gr = (int32_t)(g * (1.0f + pan * (0.7071f - 0.2929f * pan))), s, fb = v->fb;
        int center = u == n / 2u;
        switch (mode) {
        case GF_W_SYNC:                              /* de golf loopt sneller en begint opnieuw met de noot; de naad gedempt */
            for (i = 0; i < GF_BLK; i++) {
                uint32_t e = ph >> 12, e2 = ~ph >> 12;
                ph += inc;
                RD(((ph >> 12) * (uint32_t)k1) << 4);
                e = e < e2 ? e : e2;
                if (e < 32768u)
                    s = s * (int32_t)e >> 15;
                OUT();
                if (center && mono) mono[i] = s;
            }
            break;
        case GF_W_BEND:                              /* de fase gebogen: de golf naar voren of naar achteren gedrukt */
            for (i = 0; i < GF_BLK; i++) {
                int32_t x;
                ph += inc;
                x = (int32_t)(ph >> 16);
                x = x * (65535 - x) >> 16;
                RD(ph + (uint32_t)(x * k1 << 1));
                OUT();
                if (center && mono) mono[i] = s;
            }
            break;
        case GF_W_ASYM:                              /* het midden van de golf verschoven */
            for (i = 0; i < GF_BLK; i++) {
                ph += inc;
                if (ph < pivot)
                    RD(((ph >> 12) * (uint32_t)k1) << 4);
                else
                    RD(0x80000000u + ((((ph - pivot) >> 12) * (uint32_t)k2) << 4));
                OUT();
                if (center && mono) mono[i] = s;
            }
            break;
        case GF_W_PWM:                               /* de golf in het begin van de periode geperst, daarna stil */
            for (i = 0; i < GF_BLK; i++) {
                uint32_t q;
                ph += inc;
                q = (ph >> 12) * (uint32_t)k1;
                RD(q >= 0x10000000u ? 0xFFFFFFFFu : q << 4);
                OUT();
                if (center && mono) mono[i] = s;
            }
            break;
        case GF_W_FOLD:                              /* harder gemaakt en teruggevouwen */
            for (i = 0; i < GF_BLK; i++) {
                ph += inc;
                RD(ph);
                s = s * k1 >> 8;
                s = (s + 32768) & 0x1FFFF;
                s = (s > 65535 ? 131071 - s : s) - 32768;
                OUT();
                if (center && mono) mono[i] = s;
            }
            break;
        case GF_W_FM:                                /* A: de fase bewogen door oscillator B; B: door zichzelf */
            for (i = 0; i < GF_BLK; i++) {
                ph += inc;
                RD(ph + (uint32_t)(o || !mono ? fb : mono[i]) * (uint32_t)k1);
                if (o)
                    fb = (fb + s) >> 1;
                OUT();
            }
            break;
        default:
            for (i = 0; i < GF_BLK; i++) {
                ph += inc;
                RD(ph);
                OUT();
                if (center && mono) mono[i] = s;
            }
            break;
        }
        if (o && mode == GF_W_FM && center) {        /* B met FM: zijn eigen klank ook naar A */
            v->fb = fb;
            if (mono)
                for (i = 0; i < GF_BLK; i++)
                    mono[i] = fb;
        }
        v->ph[o][u] = ph;
    }
    return n;
}

/* ------------------------------------------------------------- een stem --- */
static void env_run(gf_voice_t *v, uint32_t e, const gf_env_t *E)
{
    float x = v->env[e], s = par(E->s);
    switch (v->stage[e]) {
    case 1:
        x += DT / secs(E->a);
        if (x >= 1.0f) {
            x = 1.0f;
            v->stage[e] = 2;
        }
        break;
    case 2: x = s + (x - s) * fm_exp2f(-7.0f * DT / secs(E->d)); break;
    case 4:
        x *= fm_exp2f(-7.0f * DT / secs(E->r));
        if (x < 0.0004f) {
            x = 0.0f;
            v->stage[e] = 0;
        }
        break;
    default: break;
    }
    v->env[e] = x;
}

static void lfo_run(const gf_t *t, gf_voice_t *v, uint32_t l, float ratemod)
{
    const gf_lfo_t *Lf = &t->p.l[l];
    float ph = v->lph[l] + 0.05f * fm_exp2f(par(Lf->rate) * 10.0f + clampf(ratemod, -1.0f, 1.0f) * 4.0f) * DT, x;
    if (ph >= 1.0f) {
        ph -= fm_floorf(ph);
        v->lrnd[l][0] = v->lrnd[l][1];
        v->nz2 = v->nz2 * 1664525u + 1013904223u;       /* per stem: twee rekenkernen delen geen toestand */
        v->lrnd[l][1] = (float)(int32_t)v->nz2 * (1.0f / 2147483648.0f);
    }
    switch (Lf->shape % GF_NLFO) {
    case GF_L_TRI: x = ph < 0.25f ? 4.0f * ph : ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f; break;
    case GF_L_SAW: x = 1.0f - 2.0f * ph; break;
    case GF_L_SQUARE: x = ph < 0.5f ? 1.0f : -1.0f; break;
    case GF_L_SH: x = v->lrnd[l][1]; break;
    case GF_L_DRIFT: x = v->lrnd[l][0] + (v->lrnd[l][1] - v->lrnd[l][0]) * ph * ph * (3.0f - 2.0f * ph); break;
    default: x = fm_sin_turns(ph); break;
    }
    v->lph[l] = ph;
    v->lval[l] = x;
}

GF_HOT static uint32_t voice_run(const gf_t *t, gf_voice_t *v, float *mixL, float *mixR, scr_t *scr)
{
    int32_t *aL = scr->aL, *aR = scr->aR, *mono = scr->mono;
    const gf_patch_t *P = &t->p;
    float ms[GF_NDST], src[GF_NSRC], note, cu, fc, g, G, k, dG, dk, amp, da, dgain, comp, pr, count = 0.0f;
    uint32_t i, used = 0, ftype = P->ftype % GF_NFILT;
    env_run(v, 0, &P->e[0]);
    env_run(v, 1, &P->e[1]);
    if (!v->stage[0] && v->amp < 0.0005f) {
        v->on = 0;
        return 0;
    }
    pr = par(v->press > t->chpress ? v->press : t->chpress);
    src[GF_S_NONE] = 0.0f;
    src[GF_S_ENV2] = v->env[1];
    src[GF_S_LFO1] = v->lval[0];
    src[GF_S_LFO2] = v->lval[1];
    src[GF_S_VELO] = par(v->vel);
    src[GF_S_PRESS] = pr;
    src[GF_S_WHEEL] = par(t->wheel);
    src[GF_S_KEY] = clampf(((float)v->note - 60.0f) * (1.0f / 36.0f), -1.0f, 1.0f);
    src[GF_S_RAND] = v->rnd;
    for (i = 0; i < GF_NDST; i++)
        ms[i] = 0.0f;
    for (i = 0; i < GF_NMOD; i++) {
        float a = ((float)P->m[i].amt - 64.0f) * (1.0f / 63.0f), x = src[P->m[i].src % GF_NSRC];
        uint32_t d = P->m[i].dst % GF_NDST;
        ms[d] += d == GF_D_PITCH ? x * a * a * a * 24.0f : x * a;   /* de toonhoogte fijn bij kleine waarden */
    }
    lfo_run(t, v, 0, ms[GF_D_LFO1]);
    lfo_run(t, v, 1, ms[GF_D_LFO2]);
    /* glijden naar de noot */
    if (P->glide) {
        float c = 1.0f - fm_exp2f(-DT * 5.0f / secs(P->glide));
        v->pitch += (v->target - v->pitch) * c;
    } else {
        v->pitch = v->target;
    }
    note = v->pitch + ms[GF_D_PITCH] + t->bend;
    for (i = 0; i < GF_BLK; i++)
        aL[i] = aR[i] = mono[i] = 0;
    for (i = 2; i-- > 0;) {                          /* B eerst: zijn klank kan A bewegen (FM) */
        const gf_osc_t *O = &P->o[i];
        float lev = par(O->level), pos;
        lev = lev * lev * clampf(1.0f + ms[i ? GF_D_LEVB : GF_D_LEVA], 0.0f, 2.0f);
        pos = par(O->pos) + ms[GF_D_POSAB] + ms[i ? GF_D_POSB : GF_D_POSA] + t->seq_morph;
        if (v->posq[i] < 0.0f)
            v->posq[i] = pos;
        v->posq[i] += (pos - v->posq[i]) * 0.5f;     /* geen stapjes bij draaien */
        if ((lev > 0.0005f || (i && P->o[0].warp % GF_NWARP == GF_W_FM)) && !t->busy[i] && t->loaded[i] != 255u)
            used += osc_run(t, v, i, note, v->posq[i], par(O->wamt) + ms[i ? GF_D_WARPB : GF_D_WARPA], lev,
                            par(O->detune) * clampf(1.0f + ms[GF_D_DETUNE], 0.0f, 2.0f), ms[GF_D_PAN], aL, aR, mono);
    }
    {   /* sub en ruis */
        float sl = clampf(par(P->sub) + ms[GF_D_SUB], 0.0f, 1.0f), nl = clampf(par(P->noise) + ms[GF_D_NOISE], 0.0f, 1.0f);
        int32_t gs = (int32_t)(sl * sl * 4096.0f), gn = (int32_t)(nl * nl * 2600.0f);
        if (gs) {
            uint32_t inc = (uint32_t)(440.0f * fm_exp2f((note - 69.0f - (P->sub_oct ? 24.0f : 12.0f)) * (1.0f / 12.0f)) *
                                      (4294967296.0f / (float)GF_FS)), ph = v->sph;
            for (i = 0; i < GF_BLK; i++) {
                int32_t s;
                ph += inc;
                s = t->sine[ph >> 23] * gs >> 12;
                aL[i] += s;
                aR[i] += s;
            }
            v->sph = ph;
        }
        if (gn) {
            uint32_t z = v->nz ? v->nz : 0x9E3779B9u;
            for (i = 0; i < GF_BLK; i++) {
                int32_t s;
                z = z * 1664525u + 1013904223u;
                s = (int16_t)(z >> 16) * gn >> 12;
                aL[i] += s;
                aR[i] += s;
            }
            v->nz = z;
        }
    }
    /* het filter: een ladder van vier trappen, de terugkoppeling zonder vertraging opgelost */
    cu = (float)P->cut + ((float)P->fenv - 64.0f) * (90.0f / 63.0f) * v->env[1] + par(P->ftrk) * ((float)v->note - 60.0f) * 1.058f +
         ms[GF_D_CUT] * 64.0f + t->seq_cut;
    fc = 20.0f * fm_exp2f(clampf(cu, 0.0f, 127.0f) * (10.0f / 127.0f));
    if (fc > 18000.0f)
        fc = 18000.0f;
    g = fm_tanf(FM_PI * fc / (float)GF_FS);
    G = g / (1.0f + g);
    k = clampf(par(P->res) + ms[GF_D_RES], 0.0f, 1.0f) * (ftype == GF_F_ACID ? 4.3f : 3.9f);
    if (v->G < 0.0f) {
        v->G = G;
        v->kq = k;
    }
    dG = (G - v->G) * (1.0f / (float)GF_BLK);
    dk = (k - v->kq) * (1.0f / (float)GF_BLK);
    dgain = 1.0f + par(P->drive) * 7.0f;
    comp = 2.2f / 0.3f / (1.0f + 0.55f * (dgain - 1.0f));
    amp = v->env[0] * (0.25f + 0.75f * par(v->vel) * par(v->vel)) * clampf(1.0f + ms[GF_D_AMP], 0.0f, 2.0f);
    da = (amp - v->amp) * (1.0f / (float)GF_BLK);
    {
        float s1 = v->f[0], s2 = v->f[1], s3 = v->f[2], s4 = v->f[3], d1 = v->sd[0], d2 = v->sd[1], Gc = v->G, kc = v->kq, a = v->amp;
        /* 1 / (1 + k G^4) aan het begin en het eind van het blok, daartussen recht: geen deling per sample */
        float q0 = 1.0f / (1.0f + v->kq * v->G * v->G * v->G * v->G), q1 = 1.0f / (1.0f + k * G * G * G * G),
              dq = (q1 - q0) * (1.0f / (float)GF_BLK), kin = 0.3f * (1.0f + 0.35f * v->kq), dkin = 0.3f * 0.35f * dk;
        const float sc = 0.5f / 32768.0f * 0.45f, scd = sc * dgain;
        for (i = 0; i < GF_BLK; i++) {
            float mid = (float)(aL[i] + aR[i]) * scd, side = (float)(aL[i] - aR[i]) * sc, x, S, u, y1, y2, y3, y4, vv, y;
            Gc += dG;
            kc += dk;
            a += da;
            q0 += dq;
            kin += dkin;
            x = mid * kin;                           /* zacht het filter in: zonder drive blijft het schoon */
            S = (((s1 * Gc + s2) * Gc + s3) * Gc + s4) * (1.0f - Gc);
            u = soft((x - kc * S) * q0);
            vv = (u - s1) * Gc; y1 = vv + s1; s1 = y1 + vv;
            vv = (y1 - s2) * Gc; y2 = vv + s2; s2 = y2 + vv;
            vv = (y2 - s3) * Gc; y3 = vv + s3; s3 = y3 + vv;
            vv = (y3 - s4) * Gc; y4 = vv + s4; s4 = y4 + vv;
            switch (ftype) {
            case GF_F_LP12: y = y2; break;
            case GF_F_BP: y = 2.0f * (y2 - y4); break;
            case GF_F_HP: y = u - 2.0f * y1 + y2; break;
            case GF_F_ACID: y = soft(y3 * 1.3f); break;
            default: y = y4; break;
            }
            d1 += (side - d1) * Gc;
            d2 += (d1 - d2) * Gc;
            y *= comp * a;
            vv = (ftype == GF_F_HP ? side - d2 : d2) * 2.2f * a;
            mixL[i] += y + vv;
            mixR[i] += y - vv;
        }
        v->f[0] = fm_flush(s1); v->f[1] = fm_flush(s2); v->f[2] = fm_flush(s3); v->f[3] = fm_flush(s4);
        v->sd[0] = fm_flush(d1); v->sd[1] = fm_flush(d2);
        v->G = G;
        v->kq = k;
        v->amp = amp;
    }
    (void)count;
    return used ? used : 1u;
}

/* ---------------------------------------------------------- de effecten --- */
static void fx_run(gf_t *t, float *L, float *R)
{
    const gf_patch_t *P = &t->p;
    float drv = par(P->fx_drive), ott = par(P->ott), cho = par(P->cho), dmix = par(P->dly_mix), rmix = par(P->rev_mix), size = par(P->rev_size);
    float dg = 1.0f + drv * drv * 11.0f, dcomp = 1.0f / (1.0f + 0.45f * (dg - 1.0f)), fb = par(P->dly_fb) * 0.92f;
    float want = (0.03f + 0.71f * par(P->dly_time) * par(P->dly_time)) * 22050.0f, t60 = 0.4f * fm_exp2f(size * 5.0f), rg[4];
    float c1 = 0.0253f, c2 = 0.2690f;                /* 180 Hz en 2,2 kHz */
    float bandpk[3] = {0.0f, 0.0f, 0.0f}, og[3], ogd[3], chd, chph = t->ch_ph;
    uint32_t i, b, rl[4];
    static const float RLEN[4] = {1031.0f, 1327.0f, 1523.0f, 1871.0f};
    for (b = 0; b < 4u; b++) {
        float len = RLEN[b] * (0.45f + 0.55f * size);
        rl[b] = (uint32_t)len;
        rg[b] = fm_exp2f(-9.966f * len / (t60 * 22050.0f));
    }
    for (b = 0; b < 3u; b++) {                       /* OTT: elke band naar zijn eigen niveau getrokken, omhoog en omlaag */
        static const float TGT[3] = {0.16f, 0.10f, 0.07f};
        float e = t->ott_env[b] > 0.0005f ? t->ott_env[b] : 0.0005f;
        float gg = clampf(fm_powf(TGT[b] / e, 0.6f), 0.3f, 6.0f), target = 1.0f + ott * (gg - 1.0f);
        og[b] = t->ott_g[b];
        t->ott_g[b] += (target - t->ott_g[b]) * (target < t->ott_g[b] ? 0.5f : 0.06f);
        ogd[b] = (t->ott_g[b] - og[b]) * (1.0f / (float)GF_BLK);
    }
    chd = 0.6f * DT;
    for (i = 0; i < GF_BLK; i++) {
        float l = L[i], r = R[i], lo, lm, m, hi, x;
        if (drv > 0.0f) {
            l = soft(l * dg) * dcomp * 1.6f;
            r = soft(r * dg) * dcomp * 1.6f;
        }
        if (ott > 0.0f) {
            for (b = 0; b < 3u; b++)
                og[b] += ogd[b];
            t->ott_lp[0][0] += (l - t->ott_lp[0][0]) * c1;
            t->ott_lp[0][1] += (l - t->ott_lp[0][1]) * c2;
            t->ott_lp[1][0] += (r - t->ott_lp[1][0]) * c1;
            t->ott_lp[1][1] += (r - t->ott_lp[1][1]) * c2;
            lo = t->ott_lp[0][0]; lm = t->ott_lp[0][1]; m = lm - lo; hi = l - lm;
            x = fm_fabsf(lo); if (x > bandpk[0]) bandpk[0] = x;
            x = fm_fabsf(m); if (x > bandpk[1]) bandpk[1] = x;
            x = fm_fabsf(hi); if (x > bandpk[2]) bandpk[2] = x;
            l = lo * og[0] + m * og[1] + hi * og[2];
            lo = t->ott_lp[1][0]; lm = t->ott_lp[1][1];
            r = lo * og[0] + (lm - lo) * og[1] + (r - lm) * og[2];
        }
        if (cho > 0.0f) {                            /* chorus: twee bewegende vertragingen, links en rechts uit fase */
            float dl = (12.0f + 5.0f * fm_sin_turns(chph)) * 44.1f, dr = (12.0f + 5.0f * fm_sin_turns(chph + 0.25f)) * 44.1f;
            uint32_t w = t->ch_w, il = (uint32_t)dl, ir = (uint32_t)dr;
            float *cl = t->ch, *cr = t->ch + GF_CH_LEN, a, c;
            cl[w] = l;
            cr[w] = r;
            a = cl[(w - il) & (GF_CH_LEN - 1u)];
            a += (cl[(w - il - 1u) & (GF_CH_LEN - 1u)] - a) * (dl - (float)il);
            c = cr[(w - ir) & (GF_CH_LEN - 1u)];
            c += (cr[(w - ir - 1u) & (GF_CH_LEN - 1u)] - c) * (dr - (float)ir);
            l += (c - l * 0.3f) * cho * 0.7f;
            r += (a - r * 0.3f) * cho * 0.7f;
            t->ch_w = (w + 1u) & (GF_CH_LEN - 1u);
            chph += chd * (1.0f / (float)GF_BLK);
        }
        /* delay en galm op de halve samplefrequentie */
        t->half[0] += l;
        t->half[1] += r;
        if (t->odd) {
            float inl = t->half[0] * 0.5f, inr = t->half[1] * 0.5f, dlw, drw, a, c, d, e, in, wl, wr;
            uint32_t w = t->dly_w, id;
            int16_t *bl = t->dly, *br = t->dly + GF_DLY_LEN;
            float *rv = t->rv, *ap = t->rv + 4u * GF_RV_LEN;
            t->half[0] = t->half[1] = 0.0f;
            t->dly_len += (want - t->dly_len) * 0.0006f;
            id = (uint32_t)t->dly_len;
            dlw = (float)bl[(w - id) & (GF_DLY_LEN - 1u)];
            dlw += ((float)bl[(w - id - 1u) & (GF_DLY_LEN - 1u)] - dlw) * (t->dly_len - (float)id);
            drw = (float)br[(w - id) & (GF_DLY_LEN - 1u)];
            drw += ((float)br[(w - id - 1u) & (GF_DLY_LEN - 1u)] - drw) * (t->dly_len - (float)id);
            dlw *= 1.0f / 20000.0f;
            drw *= 1.0f / 20000.0f;
            t->dly_lp[0] += (dlw - t->dly_lp[0]) * 0.55f;
            t->dly_lp[1] += (drw - t->dly_lp[1]) * 0.55f;
            bl[w] = (int16_t)clampf(((inl + inr) * 0.5f + t->dly_lp[1] * fb) * 20000.0f, -32767.0f, 32767.0f);   /* heen en weer */
            br[w] = (int16_t)clampf(t->dly_lp[0] * fb * 20000.0f, -32767.0f, 32767.0f);
            t->dly_w = (w + 1u) & (GF_DLY_LEN - 1u);
            wl = dlw * dmix;
            wr = drw * dmix;
            if (rmix > 0.0f || t->rv_lp[0] != 0.0f) {
                uint32_t rw = t->rv_w;
                float y;
                in = (inl + inr) * 0.5f + (wl + wr) * 0.5f;
                y = ap[(rw - 223u) & 255u];              /* twee allpassen maken de inzet diffuus */
                in -= 0.6f * y;
                ap[rw & 255u] = in;
                in = y + 0.6f * in;
                y = ap[256u + ((rw - 331u) & 511u)];
                in -= 0.6f * y;
                ap[256u + (rw & 511u)] = in;
                in = y + 0.6f * in;
                a = rv[(rw - rl[0]) & (GF_RV_LEN - 1u)];
                c = rv[GF_RV_LEN + ((rw - rl[1]) & (GF_RV_LEN - 1u))];
                d = rv[2u * GF_RV_LEN + ((rw - rl[2]) & (GF_RV_LEN - 1u))];
                e = rv[3u * GF_RV_LEN + ((rw - rl[3]) & (GF_RV_LEN - 1u))];
                t->rv_lp[0] += (a - t->rv_lp[0]) * 0.6f; a = t->rv_lp[0] * rg[0];
                t->rv_lp[1] += (c - t->rv_lp[1]) * 0.6f; c = t->rv_lp[1] * rg[1];
                t->rv_lp[2] += (d - t->rv_lp[2]) * 0.6f; d = t->rv_lp[2] * rg[2];
                t->rv_lp[3] += (e - t->rv_lp[3]) * 0.6f; e = t->rv_lp[3] * rg[3];
                rv[rw & (GF_RV_LEN - 1u)] = fm_flush(in + 0.5f * (a + c + d + e));
                rv[GF_RV_LEN + (rw & (GF_RV_LEN - 1u))] = fm_flush(in + 0.5f * (a - c + d - e));
                rv[2u * GF_RV_LEN + (rw & (GF_RV_LEN - 1u))] = fm_flush(in + 0.5f * (a + c - d - e));
                rv[3u * GF_RV_LEN + (rw & (GF_RV_LEN - 1u))] = fm_flush(in + 0.5f * (a - c - d + e));
                t->rv_w = rw + 1u;
                wl += (a - d) * rmix * 0.7f;
                wr += (c - e) * rmix * 0.7f;
            }
            t->wet[0][0] = t->wet[0][1]; t->wet[1][0] = t->wet[1][1];
            t->wet[0][1] = wl; t->wet[1][1] = wr;
            l += 0.5f * (t->wet[0][0] + t->wet[0][1]);
            r += 0.5f * (t->wet[1][0] + t->wet[1][1]);
        } else {
            l += t->wet[0][1];
            r += t->wet[1][1];
        }
        t->odd ^= 1u;
        L[i] = l;
        R[i] = r;
    }
    t->ch_ph = chph - fm_floorf(chph);
    for (b = 0; b < 3u; b++) {
        t->ott_env[b] = bandpk[b] > t->ott_env[b] ? bandpk[b] : t->ott_env[b] * 0.985f + bandpk[b] * 0.015f;
        t->ott_env[b] = fm_flush(t->ott_env[b]);
    }
    for (b = 0; b < 2u; b++) {
        t->ott_lp[b][0] = fm_flush(t->ott_lp[b][0]);
        t->ott_lp[b][1] = fm_flush(t->ott_lp[b][1]);
        t->dly_lp[b] = fm_flush(t->dly_lp[b]);
    }
    for (b = 0; b < 4u; b++)
        t->rv_lp[b] = fm_flush(t->rv_lp[b]);
}

/* De stemmen van een blok, verdeeld over twee rekenkernen als de app die geeft (t->fork en t->join: plat_cpu2_run en
 * plat_cpu2_wait van fm1-base): de even stemmen hier, de oneven op de tweede kern, elk in een eigen buffer. */
static uint32_t (*volatile VOICE)(const gf_t *, gf_voice_t *, float *, float *, scr_t *) = voice_run;   /* via een wijzer: de
                                                                                 code staat ver weg in het werkgeheugen */
static gf_t *job_t;
static float mixL[2][GF_BLK], mixR[2][GF_BLK];
static scr_t scr[2];
static volatile uint32_t job_used[2];
static void voices_half(uint32_t half)
{
    gf_t *t = job_t;
    uint32_t i, used = 0;
    float *L = mixL[half], *R = mixR[half];
    for (i = 0; i < GF_BLK; i++)
        L[i] = R[i] = 0.0f;
    for (i = half; i < GF_NV; i += 2u)
        if (t->v[i].on)
            used += VOICE(t, &t->v[i], L, R, &scr[half]);
    job_used[half] = used;
}

void gf_render(gf_t *t, int32_t *lr, uint32_t n)
{
    float *L = mixL[0], *R = mixR[0];
    uint32_t off, i, used;
    for (off = 0; off + GF_BLK <= n; off += GF_BLK) {
        float m = par(t->p.master) * 2.0f;
        int forked;
        seq_tick(t);
        arp_tick(t);
        job_t = t;
        forked = t->fork && t->fork(voices_half, 1u);
        voices_half(0);
        if (!forked || t->join() != 0)               /* de tweede kern nam het niet, of gaf het op: hier */
            voices_half(1u);
        for (i = 0; i < GF_BLK; i++) {
            L[i] += mixL[1][i];
            R[i] += mixR[1][i];
        }
        used = job_used[0] + job_used[1];
        if (used > t->peak_voices)
            t->peak_voices = used;
        fx_run(t, L, R);
        m *= m;
        for (i = 0; i < GF_BLK; i++) {
            lr[2u * (off + i)] = (int32_t)(limit(L[i] * m) * 8388607.0f);
            lr[2u * (off + i) + 1u] = (int32_t)(limit(R[i] * m) * 8388607.0f);
        }
    }
}
