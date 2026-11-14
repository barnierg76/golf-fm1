#!/usr/bin/env python3
import os
"""Presets van de plugin Golf omzetten naar Golf voor de FM-1, en ze naast een opname van de plugin leggen.

  golf_presets.py convert UIT.json                  elke .golfpreset vertalen (een eerste gok)
  golf_presets.py fit IN.json REFMAP UIT.json       per preset bijstellen tot de klank het dichtst bij de opname zit
  golf_presets.py emit IN.json GRENS                de beste presets -> dsp/gf_presets_golf.h

REFMAP: opnamen van de plugin, een per preset, met de vaste frase van `GolfSmoke --render-files` (48 kHz).
De motor van de FM-1 speelt dezelfde frase met `build/gf_host batch`. De maat voor het verschil: het spectrum in 24
banden per 50 ms, in dB, even hard gezet, gewogen naar wat in de opname hard klinkt.
Heeft numpy en scipy nodig (/usr/local/bin/python3)."""
import json, math, os, re, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GOLF = ROOT.parents[1] / "Golf"
PRESETS = GOLF / "plugin/assets/presets"
PARAMS = Path(os.environ.get("GOLF_PARAMS", "/private/tmp/golf_params_list.txt"))
HOST = ROOT / "build/gf_host"

# ---------------------------------------------------------------- de klank van de FM-1 (gf_patch_t, 78 bytes) ---
W_OFF, W_SYNC, W_BEND, W_ASYM, W_PWM, W_FOLD, W_FM = range(7)
F_LP24, F_LP12, F_BP, F_HP, F_ACID = range(5)
L_SINE, L_TRI, L_SAW, L_SQUARE, L_SH, L_DRIFT = range(6)
S_NONE, S_ENV2, S_LFO1, S_LFO2, S_VELO, S_PRESS, S_WHEEL, S_KEY, S_RAND = range(9)
(D_NONE, D_PITCH, D_POSA, D_POSB, D_POSAB, D_CUT, D_RES, D_WARPA, D_WARPB, D_LEVA, D_LEVB, D_DETUNE, D_PAN, D_LFO1, D_LFO2,
 D_NOISE, D_SUB, D_AMP) = range(18)
M_POLY, M_MONO, M_LEGATO = range(3)

def clampi(v, lo=0, hi=127):
    return int(max(lo, min(hi, round(v))))

def new_patch():
    osc = lambda: dict(table=0, pos=0, level=0, oct=0, semi=0, fine=64, uni=1, detune=0, width=0, warp=0, wamt=0)
    return dict(o=[osc(), osc()], sub=0, sub_oct=0, noise=0, ftype=F_LP24, cut=127, res=0, drive=0, fenv=64, ftrk=0,
                e=[[0, 60, 127, 40], [0, 60, 0, 40]], l=[[L_SINE, 60], [L_SINE, 60]], m=[[0, 0, 64] for _ in range(6)],
                mode=M_POLY, glide=0, fx=[0, 0, 0, 60, 40, 0, 60, 0], arp=[0, 0, 3, 0, 60, 80], master=64)

def pack(p):
    b = b""
    for o in p["o"]:
        b += struct.pack("<3B2b6B", o["table"], o["pos"], o["level"], o["oct"], o["semi"], o["fine"], o["uni"], o["detune"],
                         o["width"], o["warp"], o["wamt"])
    b += bytes([p["sub"], p["sub_oct"], p["noise"], p["ftype"], p["cut"], p["res"], p["drive"], p["fenv"], p["ftrk"]])
    b += bytes(p["e"][0] + p["e"][1] + p["l"][0] + p["l"][1])
    for m in p["m"]:
        b += bytes(m)
    b += bytes([p["mode"], p["glide"]] + p["fx"] + p["arp"] + [p["master"]])
    assert len(b) == 78
    return b

def tables():
    """naam van een tabel van de plugin -> nummer in de firmware"""
    out, n = {}, 0
    for line in (ROOT / "tools/tables.txt").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out[line.lstrip("=")] = n
        n += 1
    return out

RENAMED = {"Analog - Juno Brass": "Analog - Poly Brass", "Analog - Moog Bass": "Analog - Ladder Bass",
           "Analog - Prophet Stack": "Analog - Poly Stack", "Digital - Organ M1": "Digital - House Organ"}

# ----------------------------------------------------------------------------------- een preset van de plugin ---
def defaults():
    d, rng = {}, {}
    for line in PARAMS.read_text().splitlines():
        f = line.split("\t")
        if len(f) >= 5:
            d[f[0]] = float(f[4])
            rng[f[0]] = (float(f[2]), float(f[3]))
    return d, rng

def load(path, dflt):
    s = Path(path).read_text()
    attr = dict(re.findall(r'(\w+)="([^"]*)"', re.search(r"<Golf ([^>]*)>", s).group(1)))
    P = dict(dflt)
    seen = set()
    for i, v in re.findall(r'<PARAM id="([^"]+)" value="([^"]+)"', s):
        P[i] = float(v)
        seen.add(i)
    for k in P:
        if k.endswith("ReverbType") and k not in seen:
            P[k] = 0.0
    return attr, P

def secs_to(v_secs):                    # seconden -> de tijdregelaar van de FM-1 (1 ms * 2^(v * 13,3 / 127))
    return clampi(127 * math.log2(max(v_secs, 0.001) / 0.001) / 13.3)
def env_secs(v):
    return 32 * v ** 5
def hz_to_cut(hz):
    return 127 * math.log2(max(hz, 20) / 20) / 10
def lfo_hz(P, n):
    v = P[f"lfo{n}Rate"]
    if P[f"lfo{n}Sync"] >= 0.5:
        beats = [32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125][int(round(8 * v))]
        return 120 / (60 * beats)
    return 0.02 * 2 ** (12 * v)

def warp_map(mode, a):
    """warp van de plugin -> (warp, hoeveel 0..1) van de FM-1; None: bestaat hier niet"""
    a = max(0.0, min(1.0, a))
    if mode == 1: return W_SYNC, min(1, 15 * a ** 3 / 7)
    if mode == 2: return W_BEND, 0.5 + 0.5 * a
    if mode == 3: return W_BEND, 0.5 - 0.5 * a
    if mode == 4: return W_BEND, a
    if mode == 5: return W_PWM, min(1, (1 / max(1 - a, 0.125) - 1) / 7)
    if mode in (6, 15): return W_ASYM, min(1, a * 0.914)
    if mode == 10: return W_FOLD, min(1, 15 * a / 7)
    if mode in (11, 30, 31): return W_FM, min(1, a * a * 1.2)
    if mode == 12: return W_FM, min(1, a / 1.4)
    if mode in (26, 32, 33): return W_FM, min(1, a * a * 1.2)       # (Sub): hier oscillator B als sinus
    if mode == 27: return W_FM, min(1, a / 1.4)
    return None

F2_MAP = {0: F_LP12, 1: F_LP24, 2: F_BP, 3: F_HP, 4: F_HP, 39: F_LP12, 40: F_HP, 41: F_BP, 42: F_HP, 44: F_HP, 46: F_LP12,
          47: F_HP, 52: F_LP24, 5: None}
def filter_type(P, which):
    """(soort op de FM-1 of None, is het een ladder)"""
    if which == 1:
        t = int(P["filterType"])
        if t == 0 or t == 5 or t == 4: return F_LP24, True
        if t == 1: return F_ACID, True
        if t in (2, 3): return F_LP12, True
        return F2_MAP.get(t - 6), False
    return F2_MAP.get(int(P["filter2Type"])), False

def convert(path, dflt, rng, TAB):
    attr, P = load(path, dflt)
    notes, pen = [], 0.0
    macro = [P.get(f"macro{i}", 0) for i in range(8)]
    def src_static(s):                   # de waarde van een bron in rust (voor macro's en hulpbronnen)
        if 9 <= s <= 12: return macro[s - 9]
        if 19 <= s <= 22: return macro[s - 15]
        if s == 13: return 0.8
        return None
    # de routes: statisch (macro's) erbij optellen, de rest bewaren
    static, dyn = {}, []
    for n in range(1, 33):
        s, d, a = int(P.get(f"mod{n}Source", 0)), int(P.get(f"mod{n}Destination", 0)), P.get(f"mod{n}Amount", 0)
        aux = int(P.get(f"mod{n}Aux", 0))
        if not s or not a:
            continue
        if aux:
            av = src_static(aux)
            if av is None:               # wiel, druk, LFO als vermenigvuldiger: in rust niets
                continue
            a *= av
        key = attr.get(f"modTarget{n}", "") if d == 80 else d
        if (9 <= s <= 12) or (19 <= s <= 22):
            if d == 80:
                if key in P and key in rng:
                    lo, hi = rng[key]
                    P[key] = max(lo, min(hi, P[key] + a * src_static(s) * (hi - lo)))
            else:
                static[d] = static.get(d, 0) + a * src_static(s)
        elif d != 80:
            dyn.append([s, d, a])
    st = lambda d: static.get(d, 0)
    # LFO's die periodiek zijn lopen van 0 tot 1: de helft van de diepte is een vaste verschuiving
    def lfo_n(s):
        return s - 4 if 5 <= s <= 8 else s - 20 if 25 <= s <= 30 else 0
    for r in dyn:
        n = lfo_n(r[0])
        if n and int(P[f"lfo{n}Waveform"]) in (0, 1, 2, 3, 6, 8) and int(P[f"lfo{n}Mode"]) != 2:
            static[r[1]] = static.get(r[1], 0) + r[2] / 2
            r[2] /= 2
        elif r[0] == 17:                 # toeval: 0..1 bij de plugin, -1..1 hier
            static[r[1]] = static.get(r[1], 0) + r[2] / 2
            r[2] /= 2
    # ------------------------------------------------------------------ oscillatoren: de twee sterkste ---
    POSD, LEVD, WARPD, DETD, PITD = (0, 6, 7), (8, 9, 10), (11, 12, 13), (20, 21, 22), (14, 15, 16)
    cand, sample_pow, wt_pow = [], 0.0, 0.0
    for i in range(3):
        if P[f"osc{i}Enable"] < 0.5 or int(P[["routeA", "routeB", "routeC"][i]]) == 5:
            continue
        lev = max(0.0, min(1.0, P[f"osc{i}Level"] + st(LEVD[i])))
        if int(P.get(f"osc{i}Type", 0)) == 1:
            sample_pow += lev ** 2
            continue
        name = attr.get(["wavetable", "wavetableB", "wavetableC"][i], "builtin:Basic Shapes").replace("builtin:", "")
        name = RENAMED.get(name, name)
        t = TAB.get(name)
        if t is None:
            short = [k for k in TAB if k.split(" - ")[-1] == name.split(" - ")[-1]]
            t = TAB[short[0]] if short else None
        if t is None:
            notes.append(f"tabel ontbreekt: {name}")
            pen += lev ** 2
            t = 0
        cand.append((lev, i, t))
        wt_pow += lev ** 2
    if sample_pow > 0.25 * (wt_pow + 1e-9):
        notes.append("sample-oscillator")
        pen += 2
    if not cand:
        return None
    w1 = [int(P[f"osc{i}Warp1"]) for i in range(3)]
    FMM = (11, 12, 30, 31)
    cand.sort(key=lambda c: -c[0])
    pick = sorted(cand[:2], key=lambda c: c[1])
    if len(cand) > 2:
        pen += cand[2][0] ** 2 / max(wt_pow, 1e-9)
        notes.append(f"derde oscillator weg ({cand[2][0]:.2f})")
    # B of C met FM/PM leest A: dan is die hier oscillator A en de A van de plugin hier B
    if len(pick) == 2 and w1[pick[1][1]] in FMM and w1[pick[0][1]] not in FMM and pick[0][1] == 0:
        pick = [pick[1], pick[0]]
    p = new_patch()
    slot_of = {}
    for k, (lev, i, t) in enumerate(pick):
        o = p["o"][k]
        slot_of[i] = k
        o["table"] = t
        o["pos"] = clampi(127 * (P[f"osc{i}Position"] + st(POSD[i])))
        o["level"] = clampi(127 * lev)
        semis = 12 * P[f"osc{i}Octave"] + P[f"osc{i}Semi"] + P[f"osc{i}Fine"] / 100 + 24 * st(PITD[i]) + 24 * st(4)
        octv = max(-3, min(3, round(semis / 12)))
        rest = semis - 12 * octv
        o["oct"], o["semi"] = int(octv), int(max(-12, min(12, round(rest))))
        o["fine"] = clampi(64 + (rest - o["semi"]) * 100 * 63 / 50)
        un = int(P[f"osc{i}Unison"])
        o["uni"] = max(1, min(7, un))
        det = max(0.0, min(1.0, P[f"osc{i}Detune"] + st(DETD[i])))
        o["detune"] = clampi(127 * 200 * det ** 2.5 / 60) if un > 1 else 0
        o["width"] = clampi(127 * abs(2 * P[f"osc{i}Width"] - 1)) if un > 1 else 0
        wm = warp_map(w1[i], P[f"osc{i}Warp1Amount"] + st(WARPD[i]))
        if w1[i] and wm is None and P[f"osc{i}Warp1Amount"] + st(WARPD[i]) > 0.05:
            notes.append(f"warp {w1[i]} bestaat hier niet")
            pen += 0.3 * lev ** 2 / max(wt_pow, 1e-9)
        if wm:
            o["warp"], o["wamt"] = wm[0], clampi(127 * wm[1])
    # FM: A leest B. Een (Sub)-warp of een ontbrekende B: B wordt een stille sinus
    oa = p["o"][0]
    if oa["warp"] == W_FM:
        ia = pick[0][1]
        if w1[ia] in (26, 27, 32, 33) or len(pick) == 1:
            if len(pick) == 1:
                ob = p["o"][1]
                ob.update(table=0, pos=0, level=0, oct=int(max(-3, min(3, P["subOctave"]))) if w1[ia] in (26, 27, 32, 33) else oa["oct"])
            else:
                oa["warp"], oa["wamt"] = W_OFF, 0
                notes.append("FM van de sub weg")
    if len(pick) == 2 and p["o"][1]["warp"] == W_FM and w1[pick[1][1]] in (26, 27, 32, 33):
        p["o"][1]["warp"], p["o"][1]["wamt"] = W_OFF, 0
    # ------------------------------------------------------------------------------ sub en ruis ---
    if P["subOn"] >= 0.5 and int(P["routeSub"]) != 5:
        p["sub"] = clampi(127 * max(0, min(1, P["subLevel"] + st(26))))
        p["sub_oct"] = 1 if P["subOctave"] <= -2 else 0
    if P["noiseOn"] >= 0.5 and int(P["routeNoise"]) != 5:
        nl = max(0, min(1, P["noiseLevel"] + st(27)))
        p["noise"] = clampi(127 * nl * (1.0 if int(P["noiseType"]) <= 3 else 0.7))
    # ---------------------------------------------------------------------------------- filter ---
    ra = int(P[["routeA", "routeB", "routeC"][pick[0][1]]])
    f1, f2 = P["filterOn"] >= 0.5, P["filter2On"] >= 0.5
    which = 0
    if ra in (0, 1) and f1: which = 1
    if ra == 2 and f2: which = 2
    if ra == 0 and f1 and f2:            # allebei achter elkaar: het laagdoorlaatfilter dat het meest weghaalt
        c1, c2 = P["filterCutoff"] + st(3), P["filter2Cutoff"] + st(39)
        t2 = filter_type(P, 2)[0]
        which = 2 if (t2 in (F_LP12, F_LP24) and c2 < c1) else 1
        notes.append("twee filters, een gekozen")
        pen += 0.15
    elif ra == 0 and f2 and not f1:
        which = 2
    cutd = 3
    if which:
        pre = "filter" if which == 1 else "filter2"
        cutd, resd = (3, 5) if which == 1 else (39, 40)
        ft, ladder = filter_type(P, which)
        if ft is None:
            notes.append(f"filtersoort bestaat hier niet ({pre} {int(P[pre + 'Type'])})")
            pen += 0.5 * P[pre + "Mix"]
            which = 0
        else:
            c = max(0.0, min(1.0, P[pre + "Cutoff"] + st(cutd)))
            p["ftype"] = ft
            p["cut"] = clampi(hz_to_cut(8 * (22050 / 8) ** c))
            r = max(0.0, min(1.0, P[pre + "Resonance"] + st(resd)))
            p["res"] = clampi(127 * r * (1.0 if ladder else 0.85))
            p["drive"] = clampi(127 * ((1 + P[pre + "Drive"]) ** 4 - 1) / 7 * 0.5) if ladder else clampi(127 * P[pre + "Drive"] * 0.5)
            p["ftrk"] = clampi(127 * P["filterKeytrack"])
    # ------------------------------------------------------------------------------- envelopes ---
    def env(n, curve=1.39):
        a, d, r = env_secs(P[f"env{n}Attack"]), env_secs(P[f"env{n}Decay"]) + env_secs(P[f"env{n}Hold"]), env_secs(P[f"env{n}Release"])
        return [secs_to(a) if a > 0.0015 else 0, secs_to(d * curve), clampi(127 * P[f"env{n}Sustain"] ** 2), secs_to(r * curve)]
    p["e"][0] = env(0)
    # de mod-envelope: de envelope (2..4, of 1) die het meest doet
    wsum = {}
    for s, d, a in dyn:
        if 1 <= s <= 4:
            wsum[s] = wsum.get(s, 0) + abs(a) * (2 if d in (3, 39) else 1)
    menv = max((k for k in wsum if k > 1), key=lambda k: wsum[k], default=None) or (1 if 1 in wsum else None)
    p["e"][1] = env(menv - 1) if menv else list(p["e"][0])
    # LFO's: de twee die het meest doen
    lw = {}
    for s, d, a in dyn:
        n = lfo_n(s)
        if n and int(P[f"lfo{n}Mode"]) != 2:
            lw[n] = lw.get(n, 0) + abs(a) * (3 if d in (4, 14, 15, 16) else 1)
    lfos = sorted(lw, key=lambda n: -lw[n])[:2]
    SH = {0: L_SINE, 1: L_TRI, 2: L_SAW, 3: L_SQUARE, 4: L_SH, 5: L_DRIFT, 6: L_SAW, 7: L_DRIFT, 8: L_TRI}
    lsign = {}
    for k, n in enumerate(lfos):
        wf = int(P[f"lfo{n}Waveform"])
        p["l"][k] = [SH[wf], clampi(127 * math.log2(max(lfo_hz(P, n), 0.05) / 0.05) / 10)]
        lsign[n] = -1 if wf == 2 else 1          # zaag omhoog: hier loopt de zaag omlaag
    # ---------------------------------------------------------------------------------- routes ---
    routes = []
    def dst_of(d):
        if d in POSD and POSD.index(d) in slot_of: return (D_POSA, D_POSB)[slot_of[POSD.index(d)]], 1.0
        if d in LEVD and LEVD.index(d) in slot_of: return (D_LEVA, D_LEVB)[slot_of[LEVD.index(d)]], 0.7
        if d in WARPD and WARPD.index(d) in slot_of: return (D_WARPA, D_WARPB)[slot_of[WARPD.index(d)]], 1.0
        if d in DETD and DETD.index(d) in slot_of: return D_DETUNE, 0.5
        if d == 4 or (d in PITD and PITD.index(d) in slot_of): return D_PITCH, 12.0
        if d == cutd and which: return D_CUT, 2.2
        if d == (5 if which == 1 else 40) and which: return D_RES, 0.7
        if d == 1: return D_AMP, 1.0
        if d == 26: return D_SUB, 0.5
        if d == 27: return D_NOISE, 0.5
        if d == 30 and 1 in lfos: return (D_LFO1, D_LFO2)[lfos.index(1)], 0.4
        if d == 31 and 2 in lfos: return (D_LFO1, D_LFO2)[lfos.index(2)], 0.4
        return None, 0
    for s, d, a in dyn:
        n = lfo_n(s)
        if n:
            if n not in lfos: continue
            src, a = (S_LFO1, S_LFO2)[lfos.index(n)], a * lsign[n]
        elif s == menv: src = S_ENV2
        elif s == 13: src = S_VELO
        elif s == 14: src = S_WHEEL
        elif s == 15: src = S_PRESS
        elif s == 16: src, a = S_KEY, a * 0.6
        elif s == 17: src = S_RAND
        else: continue
        dst, wgt = dst_of(d)
        if dst is None:
            continue
        if dst == D_CUT:
            if src == S_ENV2:            # de envelope op het filter heeft hier een eigen regelaar
                p["fenv"] = clampi(p["fenv"] + a * 145 * 63 / 90)
                continue
            amt = a * 145 / 64
        elif dst == D_PITCH:
            amt = math.copysign(abs(a) ** (1 / 3), a)
        elif dst in (D_LEVA, D_LEVB):
            l0 = max(p["o"][dst - D_LEVA]["level"] / 127, 0.1)
            amt = (max(0, l0 + a) / l0) ** 2 - 1
        elif dst == D_AMP:
            amt = 2 * a
        elif dst == D_DETUNE:
            amt = a * 2
        else:
            amt = a
        amt = max(-1.0, min(1.0, amt))
        if abs(amt) * 63 >= 1:
            routes.append((abs(a) * wgt, src, dst, amt))
    merged = {}
    for w, src, dst, amt in routes:
        k = (src, dst)
        merged[k] = (merged.get(k, (0, 0))[0] + w, max(-1, min(1, merged.get(k, (0, 0))[1] + amt)))
    best = sorted(merged.items(), key=lambda kv: -kv[1][0])
    if len(best) > 6:
        notes.append(f"{len(best) - 6} routes weg")
    best = best[:6]
    if not any(k[0] == S_PRESS for k, _ in best) and len(best) < 6:      # de toetsen van de FM-1 voelen druk
        best.append(((S_PRESS, D_CUT), (0, 0.35)) if which and p["cut"] < 115 else ((S_PRESS, D_POSA), (0, 0.5)))
    for k, ((src, dst), (w, amt)) in enumerate(best):
        p["m"][k] = [src, dst, clampi(64 + 63 * amt)]
    # --------------------------------------------------------------------------------- spelen ---
    if P["voiceMono"] >= 0.5:
        p["mode"] = M_LEGATO if P["voiceLegato"] >= 0.5 else M_MONO
        g = P["voiceGlide"]
        p["glide"] = secs_to(g / 0.87) if g > 0.003 else 0
    # ------------------------------------------------------------------------------- effecten ---
    drv = 0.0
    for pre in ("fxDist", "fxDist2"):
        if P["fxDistortion" if pre == "fxDist" else "fxDist2"] >= 0.5:
            d = max(0, min(1, P[pre + "Drive"] + (st(41) if pre == "fxDist" else 0)))
            g = 1 + 2.949 * d ** 3 if int(P[pre + "Mode"]) == 0 else 1 + 31 * d * d
            drv = max(drv, math.sqrt(max(0, (g - 1) * max(0, min(1, P[pre + "Mix"] + (st(42) if pre == "fxDist" else 0))) / 11)))
    ott = 0.0
    for pre in ("fxComp", "fxComp2"):
        if P["fxCompressor" if pre == "fxComp" else "fxComp2"] >= 0.5:
            ott = max(ott, P[pre + "Depth"] * (0.85 if int(P[pre + "Mode"]) == 1 else 0.3))
    cho = 0.0
    if P["fxChorus"] >= 0.5: cho = max(cho, min(1, P["fxChorusMix"] + st(43)))
    if P["fxHyper"] >= 0.5: cho = max(cho, 0.8 * min(1, P["fxHyperMix"] + st(50)))
    dmix = min(1, max(0, P["fxDelayMix"] + st(45))) if P["fxDelay"] >= 0.5 else 0
    dt = math.sqrt(max(0, min(2.0, P["fxDelayTime"] / 1000) - 0.03) / 0.71)
    dfb = min(1, max(0, P["fxDelayFeedback"] + st(46)) * 0.95 / 0.92)
    rmix, t60 = 0.0, 2.0
    if P["fxReverb"] >= 0.5:
        rmix = min(1, max(0, P["fxReverbMix"] + st(47)))
        t60 = 0.8 * 7.5 ** min(1, max(0, P["fxReverbSize"] + st(48))) if int(P["fxReverbType"]) == 0 else P["fxReverbDecay"]
    if P["fxConv"] >= 0.5:
        cm = 0.8 * min(1, max(0, P["fxConvMix"] + st(51)))
        if cm > rmix: rmix, t60 = cm, max(t60, 2.5)
    p["fx"] = [clampi(127 * min(1, drv)), clampi(127 * ott), clampi(127 * cho), clampi(127 * min(1, dt)), clampi(127 * dfb),
               clampi(127 * dmix), clampi(127 * math.log2(max(0.4, min(12.8, t60)) / 0.4) / 5), clampi(127 * rmix)]
    if P["arpOn"] >= 0.5:
        p["arp"] = [1, min(4, int(P["arpMode"])), int(P["arpRate"]), int(P["arpOctaves"]) - 1, clampi(127 * P["arpGate"]), 80]
    for k in ("fxPhaser", "fxShift", "fxFilter"):
        if P.get(k, 0) >= 0.5:
            pen += 0.1 * P.get(k + "Mix", 0.5)
    return dict(name=attr["name"], cat=attr.get("category", ""), file=Path(path).name, patch=p, pen=round(pen, 3), notes=notes)

# -------------------------------------------------------------------------------- vergelijken met de opname ---
def features(x, sr):
    """mono -> (T x 24) dB per band per 50 ms, en de sterkte per frame"""
    import numpy as np
    n, hop = int(sr * 0.046) & ~1, int(sr * 0.05)
    T = 150
    w = np.hanning(n)
    fr = np.zeros((T, n))
    for t in range(T):
        seg = x[t * hop:t * hop + n]
        fr[t, :len(seg)] = seg
    S = np.abs(np.fft.rfft(fr * w, axis=1)) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    edges = 40 * (16000 / 40) ** (np.arange(25) / 24)
    B = np.stack([S[:, (f >= edges[b]) & (f < edges[b + 1])].sum(axis=1) for b in range(24)], axis=1)
    return B

def distance(Bref, B):
    import numpy as np
    eps = 1e-12
    tot_r, tot = Bref.sum() + eps, B.sum() + eps
    R, X = 10 * np.log10(Bref / tot_r + 1e-9), 10 * np.log10(B / tot + 1e-9)          # even hard gezet
    R, X = np.maximum(R, -75), np.maximum(X, -75)
    wgt = (Bref / tot_r) ** 0.35 + 1e-4                                              # wat hard klinkt telt zwaarder
    spec = (np.abs(R - X) * wgt).sum() / wgt.sum()
    er, ex = 10 * np.log10(Bref.sum(axis=1) / tot_r + 1e-9), 10 * np.log10(B.sum(axis=1) / tot + 1e-9)
    env = np.abs(np.maximum(er, -70) - np.maximum(ex, -70)).mean()                   # het verloop in de tijd
    return float(spec + 0.5 * env)

def render(patches, roots):
    import numpy as np
    with tempfile.TemporaryDirectory() as td:
        a, b = Path(td) / "in", Path(td) / "out"
        a.write_bytes(b"".join(bytes([r]) + pack(p) for p, r in zip(patches, roots)))
        subprocess.run([str(HOST), "batch", str(a), str(b)], check=True)
        x = np.frombuffer(b.read_bytes(), dtype="<i2").reshape(len(patches), -1, 2).astype(float) / 32768
    return x.mean(axis=2)

def read_ref(path):
    import numpy as np
    from scipy.io import wavfile
    sr, x = wavfile.read(str(path))
    x = x.astype(float)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x, sr

KNOBS = [("cut",), ("res",), ("fenv",), ("drive",), ("o", 0, "pos"), ("o", 1, "pos"), ("o", 0, "level"), ("o", 1, "level"),
         ("o", 0, "wamt"), ("o", 1, "wamt"), ("o", 0, "detune"), ("sub",), ("noise",), ("e", 0, 0), ("e", 0, 1), ("e", 0, 2),
         ("e", 0, 3), ("e", 1, 1), ("e", 1, 2), ("fx", 0), ("fx", 1), ("fx", 5), ("fx", 7), ("fx", 6)]
def knob_get(p, k):
    v = p
    for i in k:
        v = v[i]
    return v
def knob_set(p, k, val):
    q = json.loads(json.dumps(p))
    v = q
    for i in k[:-1]:
        v = v[i]
    v[k[-1]] = clampi(val)
    return q

def fit_one(job):
    item, refdir = job
    import numpy as np
    ref = Path(refdir) / (Path(item["file"]).stem + ".wav")
    if not ref.exists():
        return dict(item, score=None)
    x, sr = read_ref(ref)
    Bref = features(x, sr)
    root = 36 if item["cat"] == "Bass" else 60
    p = item["patch"]
    def knobs_for(p):
        ks = []
        for k in KNOBS:
            if k[0] == "o" and (p["o"][k[1]]["level"] == 0 and k[2] != "level"): continue
            if k[0] == "o" and k[2] == "wamt" and p["o"][k[1]]["warp"] == 0: continue
            if k[0] == "o" and k[2] == "detune" and p["o"][k[1]]["uni"] < 2: continue
            if k in (("res",), ("fenv",), ("drive",)) and p["cut"] >= 127 and p["fenv"] == 64 and k != ("fenv",): continue
            if k == ("fx", 6) and p["fx"][7] == 0: continue
            ks.append(k)
        return ks
    score0 = score = distance(Bref, features(render([p], [root])[0], 44100))
    for step in (20, 10, 5, 2):
        for _ in range(6):
            ks = knobs_for(p)
            cands = []
            for k in ks:
                v = knob_get(p, k)
                for d in (-step, step):
                    if 0 <= v + d <= 127 or (0 < v < 127):
                        cands.append(knob_set(p, k, v + d))
            if not cands:
                break
            X = render(cands, [root] * len(cands))
            sc = [distance(Bref, features(xx, 44100)) for xx in X]
            i = int(np.argmin(sc))
            if sc[i] < score - 0.01:
                p, score = cands[i], sc[i]
            else:
                break
    return dict(item, patch=p, score0=round(score0, 3), score=round(score, 3))

def main(argv):
    if not argv:
        raise SystemExit(__doc__)
    if argv[0] == "convert":
        dflt, rng = defaults()
        TAB = tables()
        out = []
        for f in sorted(PRESETS.glob("*.golfpreset")):
            c = convert(f, dflt, rng, TAB)
            if c:
                out.append(c)
        Path(argv[1]).write_text(json.dumps(out, indent=0))
        print(len(out), "presets omgezet;", sum(1 for c in out if c["pen"] < 0.5), "zonder grote verliezen")
    elif argv[0] == "fit":
        from multiprocessing import Pool
        items = json.loads(Path(argv[1]).read_text())
        with Pool(12) as pool:
            res = []
            for k, r in enumerate(pool.imap(fit_one, [(i, argv[2]) for i in items])):
                res.append(r)
                print(f"{k + 1:3d} {r['name'][:26]:26s} {r.get('score0')} -> {r.get('score')}  pen {r['pen']}", flush=True)
        Path(argv[3]).write_text(json.dumps(res, indent=0))
    elif argv[0] == "emit":
        emit(json.loads(Path(argv[1]).read_text()), float(argv[2]))

CATS = ["BASS", "LEAD", "PAD", "KEYS", "PLUCK", "ARP", "MOTION"]
CATMAP = {"Bass": 0, "Lead": 1, "Pad": 2, "Keys": 3, "Chords": 3, "Pluck": 4, "Perc": 4, "Arp": 5, "FX": 6, "Texture": 6}
def emit(items, limit):
    good = [i for i in items if i.get("score") is not None and i["score"] + 4 * i["pen"] <= limit]
    good.sort(key=lambda i: (CATMAP.get(i["cat"], 6), i["name"]))
    L = ["/* gemaakt door eigen/tools/golf_presets.py, niet bewerken: presets van de plugin Golf, omgezet en naast een opname",
         " * van de plugin bijgesteld. Per regel: naam, soort, de klank. */"]
    # te zacht (GOLF_BOOST, namen met komma's): de bronnen omhoog tot de sterkste op 127 staat; GOLF_DROP: weglaten
    boost = set(filter(None, os.environ.get("GOLF_BOOST", (ROOT / "tools/golf_presets_boost.txt").read_text().strip()).split(",")))
    drop = set(filter(None, os.environ.get("GOLF_DROP", "").split(",")))
    nm = lambda i: re.sub(r"[^A-Z0-9 ]", "", i["name"].upper())[:18].strip()
    good = [i for i in good if nm(i) not in drop]
    for i in good:
        p = i["patch"]
        if nm(i) in boost:
            top = max(p["o"][0]["level"], p["o"][1]["level"], p["sub"], p["noise"], 1)
            f = 127.0 / top
            for q in p["o"]:
                q["level"] = min(127, round(q["level"] * f))
            p["sub"] = min(127, round(p["sub"] * f))
            p["noise"] = min(127, round(p["noise"] * f))
        o = lambda q: "{%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}" % (q["table"], q["pos"], q["level"], q["oct"], q["semi"], q["fine"],
                                                                      q["uni"], q["detune"], q["width"], q["warp"], q["wamt"])
        name = re.sub(r"[^A-Z0-9 ]", "", i["name"].upper())[:18].strip()
        body = "{{%s, %s}, %d, %d, %d, %d, %d, %d, %d, %d, %d, {{%s}, {%s}}, {{%s}, {%s}}, {%s}, %d, %d, %s, %s, 64}" % (
            o(p["o"][0]), o(p["o"][1]), p["sub"], p["sub_oct"], p["noise"], p["ftype"], p["cut"], p["res"], p["drive"], p["fenv"],
            p["ftrk"], ", ".join(map(str, p["e"][0])), ", ".join(map(str, p["e"][1])), ", ".join(map(str, p["l"][0])),
            ", ".join(map(str, p["l"][1])), ", ".join("{%d, %d, %d}" % tuple(m) for m in p["m"]), p["mode"], p["glide"],
            ", ".join(map(str, p["fx"])), ", ".join(map(str, p["arp"])))
        L.append('    {"%s", %s, %d},' % (name, body, CATMAP.get(i["cat"], 6)))
    (ROOT / "dsp/gf_presets_golf.h").write_text("\n".join(L) + "\n")
    print(len(good), "presets in dsp/gf_presets_golf.h")

if __name__ == "__main__":
    main(sys.argv[1:])
