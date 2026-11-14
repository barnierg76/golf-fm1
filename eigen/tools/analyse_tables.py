"""hoeveel harmonischen heeft elke Golf-tabel nodig? (verkenning)"""
import sys, wave, numpy as np
from pathlib import Path
D = Path(sys.argv[1])
rows = []
for f in sorted(D.glob("*.wav")):
    w = wave.open(str(f)); n = w.getnframes(); x = np.frombuffer(w.readframes(n), dtype="<i2").astype(float) / 32768
    fr = x[: n // 2048 * 2048].reshape(-1, 2048)
    S = np.abs(np.fft.rfft(fr, axis=1))[:, 1:] / 1024
    pk = S.max() or 1
    above = np.where((S / pk).max(axis=0) > 10 ** (-66 / 20))[0]
    H = int(above.max()) + 1 if len(above) else 1
    # hoe verschillend zijn buurframes (0 = glad, 1 = los van elkaar)
    d = np.mean(np.linalg.norm(np.diff(fr, axis=0), axis=1) / (np.linalg.norm(fr[:-1], axis=1) + 1e-9)) if len(fr) > 1 else 0
    rows.append((f.stem, len(fr), H, d))
for r in rows: print(f"{r[0]:40s} frames {r[1]:3d}  H {r[2]:4d}  stap {r[3]:.2f}")
H = np.array([min(r[2], 255) for r in rows]); print("som bytes bij 32 frames, H<=255:", int((H * 2 * 32).sum()), "gemiddeld H", H.mean())
