/* Golf-motor op de Mac: metingen en luistervoorbeelden.
 *   cc -O2 -ffp-contract=off -I $FM1_BASE/include -o build/gf_host test/gf_host.c dsp/gf_engine.c -lm
 *   build/gf_host            controles (toon, stilte, aliasing, klikken, rekentijd)
 *   build/gf_host levels     het volume van elke preset -> dsp/gf_levels.h
 *   build/gf_host demo DIR   luistervoorbeelden (WAV, stereo) in DIR */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../dsp/gf_engine.h"
#include "../dsp/gf_presets.h"
#if __has_include("../dsp/gf_levels.h")
#include "../dsp/gf_levels.h"
#else
static const uint8_t GF_LEVELS[GF_NPRESETS];
#endif

static gf_t T;
static int16_t mip0[GF_MIP_LEN], mip1[GF_MIP_LEN], dly[2 * GF_DLY_LEN];
static float rv[GF_RV_MEM], ch[2 * GF_CH_LEN];
static int fails;

static void fresh(void) { gf_init(&T, GF_TABLES, GF_NTABLES, mip0, mip1, dly, rv, ch); }
static void preset(int k)
{
    T.p = GF_PRESETS[k % GF_NPRESETS].p;
    fresh();
    if (GF_LEVELS[k % GF_NPRESETS])
        T.p.master = GF_LEVELS[k % GF_NPRESETS];
    gf_tables_sync(&T);
}
static void basic(void)                /* een kale klank: een oscillator, filter open, geen effecten */
{
    memset(&T.p, 0, sizeof T.p);
    fresh();
    T.p.o[0].level = 127; T.p.o[0].fine = 64; T.p.o[0].uni = 1; T.p.o[1].fine = 64; T.p.o[1].uni = 1;
    T.p.cut = 127; T.p.fenv = 64; T.p.e[0].s = 127; T.p.e[0].r = 20; T.p.master = 64;
    for (int i = 0; i < GF_NMOD; i++) T.p.m[i].amt = 64;
    gf_tables_sync(&T);
}
static void check(const char *what, int ok, double a, double b)
{
    printf("%-64s %s  (%.3f, %.3f)\n", what, ok ? "ok  " : "FOUT", a, b);
    fails += !ok;
}
static void wav(const char *path, const int16_t *x, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = frames * 4, v;
    uint16_t s;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = GF_FS; fwrite(&v, 4, 1, f); v = GF_FS * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(x, 4, frames, f); fclose(f);
}
/* speel: noten aan op t = 0, los op hold, tot secs; out (stereo int16) mag 0 zijn */
static void play(const int *notes, int nn, double secs, double hold, int16_t *out, double *peak, double *rms, double *jump)
{
    static int32_t b[2 * GF_BLK];
    uint32_t blocks = (uint32_t)(secs * GF_FS / GF_BLK), off = (uint32_t)(hold * GF_FS / GF_BLK), k, i;
    double pk = 0, ss = 0, jm = 0, last = 0;
    for (i = 0; i < (uint32_t)nn; i++) gf_note_on(&T, notes[i], 100);
    for (k = 0; k < blocks; k++) {
        if (k == off) for (i = 0; i < (uint32_t)nn; i++) gf_note_off(&T, notes[i]);
        gf_render(&T, b, GF_BLK);
        for (i = 0; i < 2 * GF_BLK; i++) {
            double v = b[i] / 8388608.0;
            if (out) out[k * 2 * GF_BLK + i] = (int16_t)(b[i] >> 8);
            if (fabs(v) > pk) pk = fabs(v);
            ss += v * v;
            if (!(i & 1)) { if (fabs(v - last) > jm) jm = fabs(v - last); last = v; }
        }
    }
    if (peak) *peak = 20 * log10(pk + 1e-9);
    if (rms) *rms = 10 * log10(ss / (blocks * 2.0 * GF_BLK) + 1e-12);
    if (jump) *jump = jm;
}
/* de sterkte op een frequentie (Goertzel) in x[0..n), mono */
static double tone(const double *x, int n, double hz)
{
    double w = 2 * M_PI * hz / GF_FS, c = 2 * cos(w), s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        s0 = x[i] * win + c * s1 - s2; s2 = s1; s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 4 / n;
}

int main(int argc, char **argv)
{
    static const int CH[4][3] = {{50, 57, 62}, {60, 67, 72}, {41, 48, 53}, {48, 55, 64}};
    static int16_t buf[2 * GF_FS * 21];
    static double mono[GF_FS * 2];
    int k, i;
    double pk, rms;
    if (argc > 3 && !strcmp(argv[1], "batch")) {      /* batch IN UIT: per klank 1 byte grondtoon + de klank; de vaste frase
                                                         van de plugin (GolfSmoke --render-files), 7,5 s, 16 bits stereo */
        FILE *in = fopen(argv[2], "rb"), *out = fopen(argv[3], "wb");
        uint8_t rec[1 + sizeof(gf_patch_t)];
        static int32_t b[2 * GF_BLK];
        static int16_t o[2 * GF_BLK];
        if (!in || !out) return 2;
        while (fread(rec, 1, sizeof rec, in) == sizeof rec) {
            int root = rec[0];
            uint32_t blocks = (uint32_t)(7.5 * GF_FS / GF_BLK), blk;
            double prev = -1;
            memcpy(&T.p, rec + 1, sizeof T.p);
            fresh();
            gf_tables_sync(&T);
            for (blk = 0; blk < blocks; blk++) {
                double sec = (double)blk * GF_BLK / GF_FS;
#define AT(t) (prev < (t) && sec >= (t))
                if (AT(0.0)) gf_note_on(&T, root, 102);
                if (AT(1.4)) gf_note_on(&T, root + 7, 102);
                if (AT(1.5)) gf_note_off(&T, root);
                if (AT(2.6)) gf_note_off(&T, root + 7);
                if (AT(3.0)) { gf_note_on(&T, root, 102); gf_note_on(&T, root + 4, 102); gf_note_on(&T, root + 7, 102); }
                if (AT(5.0)) { gf_note_off(&T, root); gf_note_off(&T, root + 4); gf_note_off(&T, root + 7); }
                prev = sec;
                gf_render(&T, b, GF_BLK);
                for (i = 0; i < 2 * GF_BLK; i++) o[i] = (int16_t)(b[i] >> 8);
                fwrite(o, 2, 2 * GF_BLK, out);
            }
        }
        fclose(in); fclose(out);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "levels")) {     /* het volume van elke preset: RMS -18 dBFS bij een akkoord */
        printf("/* gemaakt door build/gf_host levels, niet bewerken: het volume (master) van elke ingebouwde preset */\n");
        printf("static const uint8_t GF_LEVELS[] = {");
        for (k = 0; k < (int)GF_NPRESETS; k++) {
            int it, m = 64, mono_p = GF_PRESETS[k].p.mode != GF_M_POLY;
            for (it = 0; it < 5; it++) {
                T.p = GF_PRESETS[k].p; fresh(); T.p.master = (uint8_t)m; gf_tables_sync(&T);
                play(CH[k % 4], mono_p ? 1 : 3, 6, 4, 0, &pk, &rms, 0);
                {   /* RMS naar -18 dBFS, maar de piek niet boven -3: een klank die uitsterft blijft anders te hard */
                    double up = -18.0 - rms, cap = -3.0 - pk;
                    m = (int)(m * pow(10, (up < cap ? up : cap) / 40) + 0.5);   /* het volume gaat met het kwadraat */
                }
                if (m > 127) m = 127;
                if (m < 10) m = 10;
            }
            fprintf(stderr, "%-14s master %3d  RMS %6.1f  piek %6.1f%s\n", GF_PRESETS[k].name, m, rms, pk, m == 127 ? "  (zachter dan de rest)" : "");
            printf("%s%d,", k % 16 ? " " : "\n    ", m);
        }
        printf("\n};\n");
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "seq")) {        /* seq UIT.wav NAAM ...: de beginreeks van de app (seq_default) op elke
                                                         genoemde preset, twee keer rond en de tweede keer een kwart hoger */
        static const uint8_t N[16] = {48, 0, 48, 60, 0, 48, 51, 0, 48, 0, 55, 58, 0, 53, 51, 46};
        static const int8_t MO[16] = {0, 0, 10, 30, 0, 0, 20, 40, 0, 0, 15, 35, 0, 25, 45, 60};
        static int32_t b[2 * GF_BLK];
        uint32_t per = (uint32_t)(10.0 * GF_FS / GF_BLK), blk, n = 0;
        int16_t *all = calloc((size_t)(argc - 3) * per * 2 * GF_BLK, 2);
        int a;
        for (a = 3; a < argc; a++) {
            for (k = 0; k < (int)GF_NPRESETS && strcmp(GF_PRESETS[k].name, argv[a]); k++) ;
            if (k == (int)GF_NPRESETS) { fprintf(stderr, "geen preset %s\n", argv[a]); return 2; }
            preset(k);
            memset(&T.seq, 0, sizeof T.seq);
            T.seq.len = 16; T.seq.rate = 3; T.p.bpm = 80; T.p.arp = 0;
            for (i = 0; i < 16; i++) {
                T.seq.s[i].note[0] = N[i];
                T.seq.s[i].vel = i % 4 == 0 ? 120 : 90;
                T.seq.s[i].gate = N[i] ? (i == 10 ? GF_G_TIE : GF_G_50) : GF_G_REST;
                T.seq.s[i].morph = MO[i];
                T.seq.s[i].cut = i % 4 == 3 ? 20 : 0;
            }
            if (GF_PRESETS[k].cat != GF_C_BASS) T.seq_tr = 12;
            gf_seq_run(&T, 1);
            if (GF_PRESETS[k].cat != GF_C_BASS) T.seq_tr = 12;
            for (blk = 0; blk < per; blk++) {
                double sec = (double)blk * GF_BLK / GF_FS;
                if (sec >= 4.0 && sec < 4.0 + (double)GF_BLK / GF_FS) T.seq_tr += 5;
                if (sec >= 8.0 && sec < 8.0 + (double)GF_BLK / GF_FS) gf_seq_run(&T, 0);
                gf_render(&T, b, GF_BLK);
                for (i = 0; i < 2 * GF_BLK; i++) all[n++] = (int16_t)(b[i] >> 8);
            }
        }
        wav(argv[2], all, n / 2);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "demo")) {
        for (k = 0; k < (int)GF_NPRESETS; k++) {
            char path[512], nm[32];
            int mono_p = GF_PRESETS[k].p.mode != GF_M_POLY, arp = GF_PRESETS[k].p.arp;
            static int32_t b[2 * GF_BLK];
            uint32_t blocks = 18 * GF_FS / GF_BLK, blk;
            preset(k);
            /* een frase: mono een loopje, anders drie akkoorden; de druk loopt op in het tweede deel */
            for (blk = 0; blk < blocks; blk++) {
                double sec = (double)blk * GF_BLK / GF_FS;
                static const int LINE[8] = {38, 38, 50, 41, 36, 48, 43, 45};
                static const int CHORD[3][3] = {{50, 57, 65}, {48, 55, 64}, {53, 60, 69}};
                int step = (int)(sec / (mono_p ? 0.5 : 5.0)), prev = (int)((sec - (double)GF_BLK / GF_FS) / (mono_p ? 0.5 : 5.0));
                if (sec < 15.0 && (blk == 0 || step != prev)) {
                    if (mono_p) {
                        if (blk) gf_note_off(&T, LINE[(step + 7) % 8] + (arp ? 12 : 0));
                        gf_note_on(&T, LINE[step % 8] + (arp ? 12 : 0), step % 3 ? 90 : 120);
                    } else {
                        if (blk) for (i = 0; i < 3; i++) gf_note_off(&T, CHORD[(step + 2) % 3][i]);
                        for (i = 0; i < 3; i++) gf_note_on(&T, CHORD[step % 3][i], 100);
                    }
                }
                if (sec >= 15.0 && prev < step && sec < 15.0 + (double)GF_BLK / GF_FS) gf_all_off(&T);
                if (blk == (uint32_t)(15.0 * GF_FS / GF_BLK)) gf_all_off(&T);
                T.chpress = sec > 5.0 && sec < 15.0 ? (uint8_t)(127 * (0.5 - 0.5 * cos((sec - 5.0) * 0.628))) : 0;
                gf_render(&T, b, GF_BLK);
                for (i = 0; i < 2 * GF_BLK; i++) buf[blk * 2 * GF_BLK + i] = (int16_t)(b[i] >> 8);
            }
            for (i = 0; GF_PRESETS[k].name[i] && i < 31; i++)
                nm[i] = GF_PRESETS[k].name[i] == ' ' ? '_' : (char)(GF_PRESETS[k].name[i] + 32 * (GF_PRESETS[k].name[i] >= 'A' && GF_PRESETS[k].name[i] <= 'Z'));
            nm[i] = 0;
            snprintf(path, sizeof path, "%s/%02d_%s.wav", argv[2], k + 1, nm);
            wav(path, buf, blocks * GF_BLK);
            printf("%-14s stemmen max %u\n", GF_PRESETS[k].name, T.peak_voices);
        }
        return 0;
    }
    /* stilte */
    basic();
    play(CH[0], 0, 1, 0, 0, &pk, 0, 0);
    check("geen noten: stil (piek dBFS)", pk < -150, pk, 0);
    /* toonhoogte: sinus op A4 */
    basic();
    { int n = 69; play(&n, 1, 2, 2, buf, &pk, &rms, 0); }
    for (i = 0; i < GF_FS; i++) mono[i] = buf[2 * (i + GF_FS / 2)] / 32768.0;
    { double best = 0, bf = 0, f;
      for (f = 436; f <= 444; f += 0.05) { double a = tone(mono, GF_FS, f); if (a > best) best = a, bf = f; }
      check("sinus op A4: 440 Hz (binnen 0,1 Hz)", fabs(bf - 440) <= 0.1, bf, 20 * log10(best + 1e-9)); }
    /* aliasing: een zaag hoog op het klavier; alles wat geen harmonische is */
    for (k = 0; k < 2; k++) {
        static const int NOTE[2] = {96, 84};
        double f0 = 440 * pow(2, (NOTE[k] - 69) / 12.0), harm = 0, worst = 0, f;
        basic();
        T.p.o[0].pos = 85;
        if (k) { T.p.o[0].warp = GF_W_SYNC; T.p.o[0].wamt = 50; }
        play(&NOTE[k], 1, 2, 2, buf, 0, 0, 0);
        for (i = 0; i < GF_FS; i++) mono[i] = buf[2 * (i + GF_FS / 2)] / 32768.0;
        for (f = f0; f < 20000; f += f0) { double a = tone(mono, GF_FS, f); if (a > harm) harm = a; }
        for (f = 300; f < 20000; f += 37.0) {
            double r = fmod(f, f0), a;
            if (r < 60 || f0 - r < 60) continue;
            a = tone(mono, GF_FS, f); if (a > worst) worst = a;
        }
        check(k ? "sync op C6: sterkste valse toon onder de grondtoon (dB, < -40)" : "zaag op C7: sterkste valse toon (dB, < -60)",
              20 * log10(worst / harm + 1e-12) < (k ? -40 : -60), 20 * log10(worst / harm + 1e-12), 20 * log10(harm));
    }
    /* stelen: twaalf noten achter elkaar op zes stemmen (sinus, lange uitklank), geen klik */
    basic();
    T.p.e[0].r = 95;
    { static int32_t b[2 * GF_BLK]; double last = 0, jmax = 0, pkk = 0;
      for (k = 0; k < 12 * 150; k++) {
          if (k % 150 == 0) gf_note_on(&T, 48 + 2 * (k / 150), 110);
          if (k % 150 == 100) gf_note_off(&T, 48 + 2 * (k / 150));
          gf_render(&T, b, GF_BLK);
          for (i = 0; i < GF_BLK; i++) { double v = b[2 * i] / 8388608.0; if (fabs(v - last) > jmax) jmax = fabs(v - last); last = v; if (fabs(v) > pkk) pkk = fabs(v); }
      }
      check("stemmen stelen: grootste sprong / piek onder 0,12", jmax / pkk < 0.12, jmax / pkk, pkk);
      gf_all_off(&T); }
    /* de sequencer: het tempo klopt, de noten staan op hun stap, na stoppen stil */
    basic();
    { static int32_t b[2 * GF_BLK]; static double m[GF_FS]; int steps = 0, lastst = -1, n = 0; double tail = 0, a4, a5;
      memset(&T.seq, 0, sizeof T.seq);
      T.seq.len = 4; T.seq.rate = 3;
      T.seq.s[0].note[0] = 69; T.seq.s[0].vel = 100; T.seq.s[0].gate = GF_G_100;
      T.seq.s[2].note[0] = 81; T.seq.s[2].vel = 100; T.seq.s[2].gate = GF_G_50;
      T.p.bpm = 80; T.p.e[0].r = 10;
      gf_seq_run(&T, 1);
      for (k = 0; k < 8 * GF_FS / GF_BLK; k++) {
          gf_render(&T, b, GF_BLK);
          if (T.seq_step != lastst) { lastst = T.seq_step; steps++; }
          if (T.seq_step == 0 && n + GF_BLK <= 4096) for (i = 0; i < GF_BLK; i++) m[n++] = b[2 * i] / 8388608.0;
      }
      a4 = tone(m, n, 440); a5 = tone(m, n, 880);
      check("sequencer: 8 s op 120 BPM in zestienden = 64 stappen", steps >= 64 && steps <= 65, steps, 0);
      check("sequencer: stap 1 klinkt A4, niet A5 (dB verschil > 30)", 20 * log10(a4 / (a5 + 1e-12)) > 30, 20 * log10(a4 + 1e-12), 20 * log10(a5 + 1e-12));
      gf_seq_run(&T, 0);
      for (k = 0; k < 2 * GF_FS / GF_BLK; k++) gf_render(&T, b, GF_BLK);
      for (i = 0; i < 2 * GF_BLK; i++) if (fabs(b[i] / 8388608.0) > tail) tail = fabs(b[i] / 8388608.0);
      check("sequencer: na stoppen stil", tail < 1e-4, tail, 0); }
    /* MPE: a member channel's bend moves its own note only; its slide (CC 74) opens its note's filter */
    basic();
    gf_mpe(&T, 48);
    { static int32_t b[2 * GF_BLK]; double c4, a440, a880, lo, hi;
      T.note_ch = 1; gf_note_on(&T, 69, 100);        /* A4 on channel 2 */
      T.note_ch = 2; gf_note_on(&T, 60, 100);        /* C4 on channel 3 */
      T.note_ch = 0;
      T.cbend[1] = 8192 * 12 / 48;                   /* channel 2 an octave up */
      for (k = 0; k < GF_FS / GF_BLK; k++) {
          gf_render(&T, b, GF_BLK);
          for (i = 0; i < GF_BLK; i++) mono[k * GF_BLK + i] = b[2 * i] / 8388608.0;
      }
      c4 = tone(mono + GF_FS / 2, GF_FS / 2, 261.63); a440 = tone(mono + GF_FS / 2, GF_FS / 2, 440); a880 = tone(mono + GF_FS / 2, GF_FS / 2, 880);
      check("MPE: channel 2's bend +12 st moves A4 to A5, C4 stays (dB over 440 Hz > 30)",
            20 * log10(a880 / (a440 + 1e-12)) > 30 && 20 * log10(c4 / (a440 + 1e-12)) > 30, 20 * log10(a880 + 1e-12), 20 * log10(a440 + 1e-12));
      T.note_ch = 2; gf_note_off(&T, 69); T.note_ch = 0;   /* a note off on another channel leaves it alone */
      check("MPE: a note off on channel 3 does not release channel 2's note", T.v[0].gate && T.v[1].gate, T.v[0].gate, T.v[1].gate);
      basic(); gf_mpe(&T, 48);
      T.slide.dst = GF_D_CUT; T.slide.amt = 101;     /* the device's route: SLIDE to the cutoff */
      T.p.m[GF_NMOD - 1].src = GF_S_SLIDE; T.p.m[GF_NMOD - 1].dst = GF_D_RES; T.p.m[GF_NMOD - 1].amt = 64;   /* the sound's
                                                        * own SLIDE route (amount 0) must not switch the device's off */
      T.p.o[0].pos = 85; T.p.cut = 50;               /* a saw, the filter half closed */
      T.note_ch = 1; gf_note_on(&T, 45, 100); T.note_ch = 0;
      for (lo = hi = 0, k = 0; k < 2 * GF_FS / GF_BLK; k++) {
          if (k == GF_FS / GF_BLK) T.cslide[1] = 127;
          gf_render(&T, b, GF_BLK);
          if (k % (GF_FS / GF_BLK) >= GF_FS / GF_BLK / 2)
              for (i = 0; i < GF_BLK; i++) *(k < GF_FS / GF_BLK ? &lo : &hi) += (double)b[2 * i] * b[2 * i];
      }
      check("MPE: slide up opens the note's filter (dB louder > 3)", 10 * log10(hi / (lo + 1e-12)) > 3, 10 * log10(hi / (lo + 1e-12)), 0);
      T.mpe = 0;
      for (lo = hi, hi = 0, k = 0; k < GF_FS / GF_BLK; k++) {
          gf_render(&T, b, GF_BLK);
          if (k >= GF_FS / GF_BLK / 2) for (i = 0; i < GF_BLK; i++) hi += (double)b[2 * i] * b[2 * i];
      }
      check("MPE off: the slide does nothing (dB quieter > 3)", 10 * log10(lo / (hi + 1e-12)) > 3, 10 * log10(lo / (hi + 1e-12)), 0); }
    /* na loslaten stil, voor elke preset; en niet tegen de begrenzer */
    for (k = 0; k < (int)GF_NPRESETS; k++) {
        static int32_t b[2 * GF_BLK]; double tail = 0;
        preset(k);
        play(CH[k % 4], GF_PRESETS[k].p.mode != GF_M_POLY ? 1 : 3, 4, 3, 0, &pk, &rms, 0);
        gf_all_off(&T);
        for (i = 0; i < 40 * GF_FS / GF_BLK; i++) gf_render(&T, b, GF_BLK);
        for (i = 0; i < 2 * GF_BLK; i++) if (fabs(b[i] / 8388608.0) > tail) tail = fabs(b[i] / 8388608.0);
        if (pk > -0.5 || rms < -40 || tail > 1e-4 || isnan(rms)) {
            char w[80]; snprintf(w, sizeof w, "preset %s: piek, RMS, staart", GF_PRESETS[k].name);
            check(w, 0, pk, rms); printf("   staart %g\n", tail);
        }
    }
    check("alle presets: klinken, niet tegen de begrenzer, daarna stil", 1, (double)GF_NPRESETS, 0);
    /* rekentijd */
    { static int32_t b[2 * GF_BLK]; clock_t c0; static const int N6[6] = {40, 47, 52, 55, 59, 64};
      preset(0);
      for (i = 0; i < 6; i++) gf_note_on(&T, N6[i], 100);
      c0 = clock();
      for (i = 0; i < 20 * GF_FS / GF_BLK; i++) gf_render(&T, b, GF_BLK);
      printf("rekentijd: 20 s, zes noten SUPERZAAG (%u stemmen) in %.0f ms op deze Mac\n", T.peak_voices, (clock() - c0) * 1000.0 / CLOCKS_PER_SEC); }
    printf(fails ? "%d FOUT\n" : "alles ok\n", fails);
    return fails != 0;
}
