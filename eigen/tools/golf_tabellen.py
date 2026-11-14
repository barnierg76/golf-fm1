#!/usr/bin/env python3
"""Eigen golftabellen voor Golf op de FM-1: een bank van hooguit 32 tabellen die achter de ingebouwde komt te staan
(TABLE op het apparaat). De bank staat los van de firmware in het geheugen van de FM-1 en blijft staan bij een update.

  golf_tabellen.py eigen A.wav B.wav ...      maak een bank van deze tabellen en stuur hem naar de FM-1
  golf_tabellen.py bank UIT.gfb TABEL ...     alleen de bank maken
  golf_tabellen.py stuur BANK.gfb             een bank naar de FM-1 sturen
  golf_tabellen.py wis                        de bank van de FM-1 halen
  golf_tabellen.py toon BANK.gfb              wat er in een bank zit

Een TABEL is een wavetable als geluidsbestand (WAV zoals Serum, Vital en Golf ze bewaren: frames van 2048 samples
achter elkaar), met naar wens  NAAM=bestand  en daarachter  :frame=1024  als de frames een andere lengte hebben.
Een bestand dat geen heel aantal frames is, telt als een enkele golf. Van elke tabel blijven 16 frames over, elk als
de sterkte en fase van hooguit 255 harmonischen.
Opties: --base MAP (fm1-base, voor het sturen). Maken heeft numpy en ffmpeg nodig (/usr/local/bin/python3), sturen
mido en python-rtmidi (~/Desktop/fm1/venv-midi/bin/python): maak de bank dus met het ene en stuur hem met het andere."""
import os, struct, subprocess, sys, time, zlib
from pathlib import Path

MAGIC = b"GFB1"
BANK_MAX = 0xDC000 - 0x97000            # de ruimte op de FM-1 (app.c BANK_BASE, BANK_MAX)
NMAX = 32
CMD = 0x61                              # app.c app_usb_cmd
OK_ADDR, FAIL_ADDR = 0xC0000000, 0xCF000000
ENTRY = "<16sIIf"

def make(specs, out=sys.stdout):
    import numpy as np
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from pack_tables import pack_spectra, NFR, HMAX
    if not 1 <= len(specs) <= NMAX:
        raise SystemExit(f"een bank heeft 1 tot {NMAX} tabellen")
    entries, data = [], b""
    for s in specs:
        parts = s.split(":")
        head, opt = parts[0], dict(p.split("=", 1) for p in parts[1:])
        name, path = head.split("=", 1) if "=" in head else (Path(head).stem, head)
        name = name.upper().split(" - ")[-1].strip()[:14] or "EIGEN"
        if not Path(path).is_file():
            raise SystemExit(f"{path}: geen bestand")
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-f", "f32le", "-"],
                             capture_output=True, check=True).stdout
        x = np.frombuffer(raw, dtype="<f4").astype(float)
        n = int(opt.get("frame", 2048))
        if len(x) < 16:
            raise SystemExit(f"{path}: te kort")
        if len(x) % n or len(x) < n:                              # geen heel aantal frames: een enkele golf
            n = len(x)
        fr = x.reshape(-1, n)
        pick = [round(i * (len(fr) - 1) / (NFR - 1)) for i in range(NFR)]
        c = np.fft.rfft(fr[pick], axis=1)[:, 1:HMAX + 1] / (n / 2)
        if c.shape[1] < HMAX:
            c = np.pad(c, ((0, 0), (0, HMAX - c.shape[1])))
        if not np.abs(c).max() > 0:
            raise SystemExit(f"{path}: stil")
        by, H, norm = pack_spectra(c)
        entries.append((name, len(by), H, norm))
        data += by
        print(f"  {name:14s} {len(fr):3d} frames van {n}  {H:3d} harmonischen  {len(by)} B", file=out)
    off, table = 16 + struct.calcsize(ENTRY) * len(entries), b""
    for name, size, H, norm in entries:
        table += struct.pack(ENTRY, name.encode("ascii", "replace"), off, H, norm)
        off += size
    body = struct.pack("<BBH", len(entries), 0, 0) + table + data
    bank = MAGIC + struct.pack("<I", 12 + len(body)) + b"\0\0\0\0" + body
    bank = bank[:8] + struct.pack("<I", zlib.crc32(bank[12:])) + bank[12:]
    if len(bank) > BANK_MAX:
        raise SystemExit(f"de bank is {len(bank)} B, er past {BANK_MAX} B: minder tabellen")
    print(f"bank: {len(entries)} tabellen, {len(bank)} B van {BANK_MAX}", file=out)
    return bank

def show(bank, out=sys.stdout):
    if bank[:4] != MAGIC or struct.unpack_from("<I", bank, 4)[0] != len(bank) or \
            struct.unpack_from("<I", bank, 8)[0] != zlib.crc32(bank[12:]):
        raise SystemExit("geen (hele) Golf-bank")
    for i in range(bank[12]):
        name, off, H, norm = struct.unpack_from(ENTRY, bank, 16 + struct.calcsize(ENTRY) * i)
        print(f"  {name.rstrip(bytes(1)).decode():14s} {H:3d} harmonischen", file=out)

def send(bank, base, out=sys.stdout):
    sys.path.insert(0, str(Path(base) / "tools"))
    import fm1_install as I
    try:
        backend = I.MidoBackend()
    except ImportError:
        raise SystemExit("sturen heeft mido en python-rtmidi nodig: draai dit met ~/Desktop/fm1/venv-midi/bin/python")
    up = I.Updater(backend, "Golf")
    dev = up.find(only_port=False)
    if not dev:
        raise SystemExit("geen FM-1 gevonden: zit de USB-kabel erin en draait Golf?")
    if not dev.id.text.startswith("FM-1_28"):
        dev.link.close()
        raise SystemExit(f"de FM-1 draait {dev.id.text}, niet Golf")
    link = dev.link
    body = struct.pack("<II", len(bank), zlib.crc32(bank))
    msg = bytes([0x00, 0x59, CMD, len(body), 0, 0]) + body + bytes([~sum(body) & 0xFF])
    try:
        link.drain()
        link.send(b"\xF0" + I.pack7(msg) + b"\xF7")
        last, asked = time.monotonic(), 0
        while True:
            pkt = link.read(0.5)
            if pkt is None:
                if link.lost:
                    raise SystemExit("\nde FM-1 is losgeraakt: stuur de bank opnieuw")
                if time.monotonic() - last > (12.0 if asked else 4.0):
                    raise SystemExit("\nde FM-1 vraagt niets meer" if asked else
                                     "de FM-1 antwoordt niet: een oudere Golf, zonder eigen tabellen?")
                continue
            r = I.parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last, asked = time.monotonic(), asked + 1
            if addr & 0xFF000000 in (OK_ADDR, FAIL_ADDR):
                link.send(I.response(addr, b"success\0", fl))
                print(file=out)
                if addr & 0xFF000000 == FAIL_ADDR:
                    why = {1: "de bank is te groot", 2: "het geheugen wissen", 3: "de overdracht", 4: "het schrijven",
                           5: "de controle achteraf"}.get(addr & 0xFF, f"code {addr & 0xFF}")
                    raise SystemExit(f"mislukt: {why}. De FM-1 heeft nu geen eigen tabellen; stuur de bank opnieuw.")
                print(f"klaar: de FM-1 heeft nu {addr & 0xFF} eigen tabel{'len' if addr & 0xFF != 1 else ''} "
                      "(TABLE, voorbij de ingebouwde)" if bank else "klaar: de eigen tabellen zijn van de FM-1 gehaald", file=out)
                return
            if n > 512 or addr >= len(bank):
                raise SystemExit(f"\nde FM-1 vroeg om {addr:#x}+{n}, buiten de bank")
            d = bank[addr:addr + n]
            link.send(I.response(addr, d + bytes(n - len(d)), fl))
            print(f"\rsturen {min(addr + n, len(bank)) * 100 // len(bank):3d}%", end="", file=out, flush=True)
    finally:
        link.close()

def main(argv):
    base, args, i = os.environ.get("FM1_BASE", str(Path.home() / "Desktop/fm1/ref/fm1-base")), [], 0
    while i < len(argv):
        if argv[i] == "--base":
            base, i = argv[i + 1], i + 1
        else:
            args.append(argv[i])
        i += 1
    if not args or args[0] not in ("eigen", "bank", "stuur", "wis", "toon"):
        raise SystemExit(__doc__)
    if args[0] == "bank":
        Path(args[1]).write_bytes(make(args[2:]))
    elif args[0] == "toon":
        show(Path(args[1]).read_bytes())
    elif args[0] == "stuur":
        bank = Path(args[1]).read_bytes()
        show(bank)
        send(bank, base)
    elif args[0] == "wis":
        send(b"", base)
    else:
        send(make(args[1:]), base)

if __name__ == "__main__":
    main(sys.argv[1:])
