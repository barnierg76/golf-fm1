#!/usr/bin/env python3
"""Golftabellen inpakken voor de Golf-motor: tools/tables.txt -> dsp/gf_tables.h.
  /usr/local/bin/python3 tools/pack_tables.py [MAP MET GOLFS TABELLEN]      (heeft numpy nodig)
Een tabel van Golf is 64 (of 32) frames van 2048 samples. Hier worden het 16 frames, elk als de sterkte en fase van
zijn harmonischen: een byte met hun aantal (hooguit 255), dan 2 bytes per harmonische: sterkte in 256 stappen over
72 dB onder de sterkste van de tabel (0 = stil), fase in 256 stappen. De FM-1 maakt daar bij het laden golven van voor elk octaaf (dsp/gf_engine.c gf_table_build)."""
import sys, wave
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
NFR, HMAX, FLOOR_DB = 16, 255, 60.0

def spectra_of(path):
    w = wave.open(str(path))
    n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype="<i2").astype(float) / 32768
    fr = x[: n // 2048 * 2048].reshape(-1, 2048)
    pick = [round(i * (len(fr) - 1) / (NFR - 1)) for i in range(NFR)]
    return np.fft.rfft(fr[pick], axis=1)[:, 1:HMAX + 1] / 1024          # c_k: x = sum |c| cos(k w + arg c)

def basic_shapes():
    """sinus -> driehoek -> zaag -> blok, in elkaar overlopend"""
    k = np.arange(1, HMAX + 1)
    sine = np.where(k == 1, 1.0, 0.0)
    tri = np.where(k % 2 == 1, (8 / np.pi ** 2) / k ** 2 * (-1.0) ** ((k - 1) // 2), 0.0)
    saw = (2 / np.pi) / k * (-1.0) ** (k + 1)
    sq = np.where(k % 2 == 1, (4 / np.pi) / k, 0.0)
    shapes, out = [sine, tri, saw, sq], []
    for i in range(NFR):
        t = 3 * i / (NFR - 1)
        a = min(int(t), 2)
        b = (1 - (t - a)) * shapes[a] + (t - a) * shapes[a + 1]          # sinusreeksen: c = -i b
        out.append(-1j * b)
    return np.array(out)

def synth(mag, ph):
    """de golven zoals het apparaat ze maakt (2048 punten), voor de piek"""
    c = mag * np.exp(1j * ph)
    X = np.zeros((len(c), 1025), complex)
    X[:, 1:c.shape[1] + 1] = c * 1024
    return np.fft.irfft(X, 2048, axis=1)

def pack_spectra(c):
    """de harmonischen van 16 frames -> (bytes, de meeste harmonischen, 1 / piek)"""
    mag = np.abs(c)
    ref = mag.max() or 1.0
    keep = np.where((mag / ref).max(axis=0) > 10 ** (-FLOOR_DB / 20))[0]
    H = int(keep.max()) + 1 if len(keep) else 1
    mag, ang = mag[:, :H], np.angle(c[:, :H]) + np.pi / 2          # als sinus: x = sum a sin(k w + fase)
    with np.errstate(divide="ignore"):
        m = np.round(255 + np.log2(mag / ref) * 255 / 12)
    m = np.clip(np.where(mag > 0, m, 0), 0, 255).astype(np.uint8)
    p = (np.round(ang / (2 * np.pi) * 256).astype(int) & 255).astype(np.uint8)
    m = np.where(mag / ref > 10 ** (-FLOOR_DB / 20), m, 0).astype(np.uint8)
    by = b""
    for f in range(NFR):                                           # per frame: aantal, dan (sterkte, fase)
        nz = np.nonzero(m[f])[0]
        hf = int(nz.max()) + 1 if len(nz) else 0
        by += bytes([hf]) + np.stack([m[f, :hf], p[f, :hf]], axis=1).tobytes()
    qmag = np.where(m > 0, 2.0 ** ((m.astype(float) - 255) * 12 / 255), 0.0)
    peak = np.abs(synth(qmag, p * 2 * np.pi / 256 - np.pi / 2)).max() or 1.0
    return by, H, 1.0 / peak

def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parents[1] / "Golf/plugin/assets/tables"
    out = ["/* gemaakt door eigen/tools/pack_tables.py, niet bewerken */", "#pragma once", '#include "gf_engine.h"']
    rows, total, idents = [], 0, []
    for line in (ROOT / "tools/tables.txt").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("="):
            c, name = basic_shapes(), line[1:]
        else:
            c, name = spectra_of(src / (line + ".wav")), line.split(" - ", 1)[1]
        by, H, norm = pack_spectra(c)
        ident = f"GF_TAB_{len(rows)}"
        out.append(f"static const uint8_t {ident}[{len(by)}] = {{")
        for i in range(0, len(by), 40):
            out.append("    " + ",".join(str(v) for v in by[i:i + 40]) + ",")
        out.append("};")
        short = name.upper()[:14]
        rows.append(f'    {{{ident}, {H}, {norm:.6f}f, "{short}"}},')
        idents.append("GT_" + "".join(ch if ch.isalnum() else "_" for ch in name.upper()))
        total += len(by)
        print(f"{len(rows) - 1:3d} {short:14s} H {H:3d}  {len(by):5d} B")
    out += ["static const gf_table_t GF_TABLES[] = {"] + rows + ["};", f"#define GF_NTABLES {len(rows)}u",
            "enum { " + ", ".join(idents) + " };"]
    (ROOT / "dsp/gf_tables.h").write_text("\n".join(out) + "\n")
    print(f"gf_tables.h: {len(rows)} tabellen, {total} B")

if __name__ == "__main__":
    main()
