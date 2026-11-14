# Golf voor de M-VAVE FM-1

Een eigen firmware die van de FM-1 een wavetable-synthesizer maakt, naar de plugin Golf. Het is geen overzetting
van de plugin: de klankmotor is nieuw geschreven voor de kleine chip van de FM-1, met de golftabellen van Golf.

- `firmware/golf.fwsc`: het pakket voor de FM-1 (identiteit FM-1_2800).
- `luister/`: luistervoorbeelden, gemaakt met dezelfde klankcode als op het apparaat. `golf-plugin-tegen-fm1.mp3`
  laat per preset eerst de plugin en dan de FM-1 horen; `golf-fm1-sequencer.mp3` de stepsequencer.
- `eigen/`: de broncode (klankmotor in `eigen/dsp/`, het instrument in `eigen/app.c`).
- [TABELLEN.md](TABELLEN.md): de golftabellen en hun bronvermelding.
- [LEARNINGS.md](LEARNINGS.md): wat werkte, wat niet, en de metingen.

## Wat het is

- **Twee oscillatoren (A en B)** die elk door een golftabel morphen: 130 tabellen van Golf ingebouwd, en ruimte
  voor 32 eigen tabellen vanaf de Mac. Elke oscillator heeft tot zeven stemmen unisono met ontstemming en breedte,
  en een warp: SYNC, BEND, ASYM, PWM, FOLD of FM (B beweegt A).
- **Sub en ruis** erbij.
- **Filter**: ladder (24 dB), 12 dB, band, hoog en ACID, met resonantie, drive, een eigen envelope en volgen van
  de toetsen.
- **Beweging**: twee envelopes, twee LFO's (sinus, driehoek, zaag, blok, S&H, drift) en zes routes. Bronnen:
  MOD ENV, LFO 1 en 2, aanslag, de druk op een toets, het modulatiewiel, de toonhoogte en toeval.
- **Effecten**: drive, OTT (compressie in drie banden), chorus, delay en galm.
- **Stepsequencer**: 32 stappen, per stap tot vier noten, aanslag, lengte (ook gebonden), en een eigen stand van
  de golftabel en van het filter. Zo beweegt een reeks ook door de klank. Hij loopt in de klankmotor zelf, dus
  strak in de maat, en de arpeggiator kan zijn akkoorden uitspelen.
- **Arpeggiator**, en mono of legato met glijden.
- **Acht noten tegelijk.** De stemmen zijn verdeeld over de twee rekenkernen van de FM-1.

## Presets

168 presets: 72 eigen en 96 uit de plugin Golf. Die laatste zijn omgerekend naar deze klankmotor (twee oscillatoren,
een filter) en daarna automatisch bijgesteld naast een opname van de plugin, tot spectrum en verloop zo dicht
mogelijk bij elkaar lagen. Alleen de presets die dichtbij kwamen zijn meegenomen (96 van de 232 die om te zetten
waren).

    GOLF_PARAMS=params.txt /usr/local/bin/python3 eigen/tools/golf_presets.py convert conv.json
    /usr/local/bin/python3 eigen/tools/golf_presets.py fit conv.json OPNAMEN fit.json
    /usr/local/bin/python3 eigen/tools/golf_presets.py emit eigen/tools/golf_presets_fit.json 6

## Bediening

| | |
| --- | --- |
| toetsen | spelen, met aanslag en druk; OCT- en OCT+ het octaaf (bij loslaten); OCT- en OCT+ samen: noten vasthouden |
| PRESET | een preset kiezen (de bank staat op soort: bas, lead, pad, toetsen, pluk, arpeggio, beweging); SAVE bewaart de klank van nu als eigen preset (16 plekken) |
| K1 tot K4 | de vier regelaars van de pagina |
| ALGORITHM | door de pagina's bladeren |
| SELECT of SEL | oscillator A of B kiezen |
| HOME | PLAY: morph A, morph B, cutoff, resonantie. Nog eens HOME: naar de volgende soort presets |
| EDIT | de oscillator: WAVE, SHAPE, TUNE |
| GLO | MIX (sub, ruis) en VOICE (poly/mono/legato, glijden, volume, tempo) |
| ENV | FILTER, FILTER ENV, AMP ENV en MOD ENV |
| SEQ | de sequencer: STEP (noot, aanslag, lengte, morph van de stap) en PATTERN (lengte, tempo-deling, swing, filter van de stap) |
| LFO | LFO en MATRIX (de zes routes) |
| FX | EFFECTS en SPACE (delay en galm) |
| ARP | de arpeggiator aan of uit, en zijn pagina |
| PLAY | noten vasthouden. Op de sequencerpagina's start PLAY de sequencer; loopt hij, dan stopt PLAY hem overal. Terwijl hij loopt verplaatst een toets de reeks (C4 = zoals opgenomen) |
| REC | opnemen aan of uit: elke toets (of akkoord) vult een stap en gaat naar de volgende, SEL zet een rust. Lang indrukken: de reeks leeg |
| SELECT | op de sequencerpagina's: de stap kiezen |

Het scherm laat de golftabel van de gekozen oscillator zien als zestien golven achter elkaar. De golf die nu klinkt
licht op en beweegt mee met de routes. Rechts staat wat het filter doorlaat.

Over MIDI: noten, toonhoogtewiel, modulatiewiel (CC 1), pedaal (CC 64), druk per toets en per kanaal, en een
programmawissel kiest een preset. Elke regelaar van de klank is ook over MIDI te zetten: CC 102 kiest de regelaar
(zijn plek in `gf_patch_t`), CC 103 zet de waarde. CC 105 laat de belasting van het apparaat op het scherm zien.

De reeks van de sequencer wordt bewaard met SAVE op een sequencerpagina en blijft staan na uitzetten.

## Eigen golftabellen

    /usr/local/bin/python3 eigen/tools/golf_tabellen.py bank mijn.gfb A.wav B.wav
    ~/Desktop/fm1/venv-midi/bin/python eigen/tools/golf_tabellen.py stuur mijn.gfb

Een tabel is een wavetable-WAV zoals Serum, Vital en Golf ze bewaren (frames van 2048 samples). De bank blijft staan
bij een update van de firmware. `eigen/bank/extra.gfb` is een voorbeeld met zes extra tabellen van Golf.

## Bouwen, testen, installeren

    cd eigen
    ./bouw.sh host                 # simulator, draait tests/*.txt
    ./bouw.sh device --no-ble      # het pakket build/golf.fwsc (heeft Docker nodig)
    cc -O2 -ffp-contract=off -I $FM1_BASE/include -o build/gf_host test/gf_host.c dsp/gf_engine.c -lm
    build/gf_host                  # metingen van de klankmotor
    build/gf_host levels > dsp/gf_levels.h    # het volume van elke preset opnieuw uitrekenen
    build/gf_host demo build/demo  # luistervoorbeelden van elke preset
    build/gf_host seq uit.wav "ACID LIJN" REESE    # de beginreeks van de sequencer op presets

Installeren: `cd $FM1_BASE && python tools/fm1_install.py PAD/golf.fwsc --yes`. Terug naar de fabriek:
`fm1_install.py FM-1_015.fwsc --yes`.

Gebouwd op [fm1-base](https://github.com/charlesvestal/fm1-base) en felucca-bsp (MPL-2.0).
