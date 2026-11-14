#!/usr/bin/env python3
"""A/B: de plugin en de FM-1 na elkaar, per preset.  ab.py FIT.json REFMAP UIT.mp3 NAAM ...  (numpy, scipy, ffmpeg)"""
import json, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import golf_presets as G
from scipy.io import wavfile
from scipy.signal import resample_poly

items = {i["name"]: i for i in json.loads(Path(sys.argv[1]).read_text())}
parts = []
for name in sys.argv[4:]:
    it = items[name]
    sr, x = wavfile.read(str(Path(sys.argv[2]) / (Path(it["file"]).stem + ".wav")))
    x = resample_poly(x.astype(float), 147, 160, axis=0)                       # 48000 -> 44100
    with tempfile.TemporaryDirectory() as td:
        a, b = Path(td) / "in", Path(td) / "out"
        a.write_bytes(bytes([36 if it["cat"] == "Bass" else 60]) + G.pack(it["patch"]))
        subprocess.run([str(G.HOST), "batch", str(a), str(b)], check=True)
        y = np.frombuffer(b.read_bytes(), dtype="<i2").reshape(-1, 2).astype(float) / 32768
    n = int(6.5 * 44100)
    for z in (x[:n], y[:n]):
        z = z / (np.sqrt((z ** 2).mean()) + 1e-9) * 0.1                        # even hard
        z = z / max(1.0, np.abs(z).max() / 0.95)
        z[-2205:] *= np.linspace(1, 0, 2205)[:, None]
        parts.append(z)
        parts.append(np.zeros((8820, 2)))
    parts.append(np.zeros((22050, 2)))
out = np.concatenate(parts)
tmp = sys.argv[3] + ".wav"
wavfile.write(tmp, 44100, (out * 32767).astype(np.int16))
subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", tmp, "-b:a", "192k", sys.argv[3]], check=True)
Path(tmp).unlink()
