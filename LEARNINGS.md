# Golf voor de FM-1: lessen en metingen

14 november 2026. De chip: JieLi AC79, 360 MHz, twee kernen, alleen enkele precisie, geen wiskundebibliotheek.

## Rekentijd: wat het verschil maakte

Gemeten op het apparaat (belasting en haperingen op het scherm, eerst met REC, nu met MIDI CC 105), elke regelaar gezet over MIDI (CC 102/103) en het unisono
vastgezet (CC 104), zodat elk onderdeel apart te meten is.

| stap | zes noten, 14 oscillatoren per noot (84 samen) |
| --- | --- |
| eerste versie (blok 32, alles in flash, een kern) | 132 %, hapert |
| blok 64, geen deling per sample in het filter, geen exp2/sinus per unisono-stem | 114 % |
| de stemcode in het werkgeheugen (`.app_ram`, `flatten`) en de stemmen over twee kernen | 60 %, geen haperingen |

Acht noten van de zwaarste presets (SUPERZAAG, OCTAAFZWERM, NEVELBANK): 76 tot 77 %, geen haperingen. Stil: 5 %.

- **De effecten waren niet het probleem.** Drive, OTT, chorus, delay en galm kosten samen een paar procent. De
  kosten zaten per stem: het filter (float, vier trappen) en het rekenwerk per blok.
- **Dure functies per blok tellen mee als per sample.** Per unisono-stem een `fm_exp2f` en twee `fm_sin_turns` per
  blok van 32 samples kostte ongeveer evenveel als de binnenste lus. Vervangen door een korte reeks (2^x als
  1 + x ln 2 + ..., voor hooguit 60 cent) en een kromme door drie punten voor links-rechts.
- **Een deling per sample is duur.** In het ladderfilter 1 / (1 + k G^4) aan het begin en eind van het blok
  uitrekenen en daartussen recht laten lopen.
- **Code in het werkgeheugen.** fm1-base draait code vanuit het flashgeheugen via een cache. Een functie in
  `.app_ram` met `flatten` (alles erin, ook fastmath) draait vanuit RAM. De stemfunctie is 9 KB van de 32 KB.
  Vanuit flash roep je hem aan via een `volatile` functiewijzer (een gewone aanroep reikt niet zo ver).
- **De tweede kern** (`cpu2 = true`, `cpu2_fallback = true`): de even stemmen op kern 0, de oneven op kern 1, elk
  in een eigen buffer en met een eigen kladblok. Gedeelde toestand in de stemcode moest eruit: de toevalsgenerator
  van de LFO is nu per stem. De motor kent alleen twee functiewijzers (`fork`, `join`); de app vult
  `plat_cpu2_run` en `plat_cpu2_wait` in. Op de Mac blijven ze leeg.
- Bij overbelasting verlaagt de app het unisono (`uni_cap`) en laat het langzaam terugkomen.

## Klank

- **Golftabellen als harmonischen.** Een tabel is 16 frames, per frame een byte met het aantal harmonischen en dan
  sterkte (72 dB in 255 stappen) en fase per harmonische. 130 tabellen: 406 KB (was 461 KB voor 104 voordat het
  aantal per frame werd opgeslagen en alles onder -60 dB wegviel). Bij het laden maakt het apparaat er met een
  omgekeerde FFT acht versies van, voor elk octaaf een, met steeds minder harmonischen.
- **Valse tonen kwamen niet van de tabellen maar van te kleine tabellen en van de verzadiging.** Zaag op C7: -44 dB
  met tabellen van 64 punten en rechte interpolatie, -70,6 dB met 512 punten voor de laagste drie niveaus en 256 en
  128 daarboven. De verzadiging in het filter krijgt het signaal zacht aangeboden (x 0,3) en de begrenzer aan de
  uitgang is recht tot 0,7.
- **Stemmen stelen zonder klik**: een gestolen stem houdt zijn fasen en zijn filtertoestand. Meten met een sinus
  (sprong / piek 0,08, grens 0,12); een zaag maakt zelf sprongen en zegt niets.
- **Volume per preset**: RMS naar -18 dBFS bij een akkoord, maar de piek niet boven -3 dBFS. Zonder die tweede
  grens kwam een klank die snel uitsterft (PLUK) tegen de begrenzer.

## Platform (fm1-base)

- `turn K1 3` in een simulatortest komt als drie losse stappen aan, niet als een snelle draai.
- De venv's van de FM-1-map hebben geen numpy en de MIDI-venv geen PIL: de bank maken met
  `/usr/local/bin/python3`, sturen met `venv-midi`.
- In zsh is `$n:a` een padbewerking: schrijf `${n}` in een ffmpeg-filter.
- De pool is bijna vol (328 van 344 KB): twee sets tabellen (2 x 76 KB), delay, galm, chorus.

## Presets van de plugin overnemen

- **Omrekenen alleen is niet genoeg.** De plugin heeft drie oscillatoren, twee filters en andere krommen. Na het
  omrekenen van elke regelaar (`golf_presets.py convert`) klonk een deel anders. Wat hielp: de plugin een vaste
  frase laten spelen (`GolfSmoke --render-files`), de firmware dezelfde frase (`gf_host batch`), en dan regelaar voor
  regelaar zoeken (stappen 20, 10, 5, 2) naar de kleinste afstand in 24 frequentiebanden plus het verloop van het
  volume. Voorbeelden: 7,7 naar 4,3 en 5,5 naar 1,9. 232 presets op 12 processen: ongeveer een half uur.
- **Een grens, geen alles-of-niets.** Afstand plus vier keer de straf voor wat niet over te zetten is (derde
  oscillator, sample, tweede filter): tot 6 gaan er 96 mee. De rest blijft weg in plaats van half te kloppen.
- **Te zacht na het omzetten.** Vijftien presets kwamen niet op volume, ook niet met het volume vol open. De bronnen
  (oscillatoren, sub, ruis) opschalen tot de sterkste op 127 staat loste er veertien op
  (`tools/golf_presets_boost.txt`).
- De plugin had 26 tabellen nodig die nog niet in de firmware zaten; die zijn toegevoegd (130 samen).

## Stepsequencer

- **In de klankmotor, niet in de app.** De app tekent en leest knoppen in een ander tempo dan het geluid. De
  sequencer telt in `gf_render` per blok van 64 samples (1,5 ms) en roept zelf `gf_note_on` en `gf_note_off` aan.
  Gemeten: 8 s op 120 BPM in zestienden is precies 64 stappen. Omdat hij gewone noten geeft, werkt de arpeggiator
  er vanzelf op door.
- **Per stap een eigen stand van de golftabel en het filter** kost niets: twee getallen die de stemcode bij de
  positie en de cutoff optelt.
- **Twee knoppen samen pas bij loslaten beslissen.** Vasthouden zit op OCT- en OCT+ samen. Eerst veranderde de eerste
  knop bij het indrukken al het octaaf, en dat liet de noot los die je net wilde vasthouden. Nu verandert het octaaf
  bij het loslaten van een knop, en alleen als de andere niet ook ingedrukt was. De simulator ving dit (`press`,
  `release`), niet het apparaat.
- **Meten op het apparaat terwijl iemand speelt gaat mis.** De knoppen op afstand en de handen van de speler lopen
  door elkaar. Voor een meting eerst vragen of het apparaat vrij is. De statusregel zit nu op MIDI CC 105 in plaats
  van op een knop, zodat een meting geen knop meer nodig heeft.
- Belasting op het apparaat met de sequencer aan: 14 % (REESE, de reeks alleen), 44 % met acht noten erbij, geen
  haperingen.
