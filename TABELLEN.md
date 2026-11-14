# De golftabellen van Golf voor de FM-1

De firmware bevat 130 golftabellen uit de plugin Golf. Elke tabel is in de firmware opgeslagen als de sterkte en
de fase van zijn harmonischen (16 frames per tabel); er zit geen opgenomen geluid in.

De meeste tabellen zijn eigen werk, gemaakt met formules. Een deel is een nieuwe opbouw (resynthese) van opnamen
of golfvormen die hun makers vrij hebben gegeven (CC0) of onder Creative Commons Naamsvermelding (CC BY) hebben
uitgebracht. De volledige bronvermelding van de plugin staat hieronder, in het Engels zoals de licenties dat vragen.

## De tabellen in deze firmware

1. Basic Shapes  (eigen formule)
2. Analog - Saw to Square
3. Analog - Warm Saw
4. Analog - Saw Stack
5. Analog - Pulse Width
6. Analog - PWM Smooth
7. Analog - Sync Saw
8. Analog - Sync Sweep
9. Analog - Hoover
10. Analog - Reese Source
11. Analog - Acid Sweep
12. Analog - Ladder Sweep
13. Analog - Classic Morph
14. Analog - Poly Pad
15. Analog - Tube Strings
16. Analog - Saw Sub
17. Analog - Triangle to Saw
18. Digital - FM 1-2
19. Digital - FM 1-7
20. Digital - FM Feedback
21. Digital - FM Growl
22. Digital - FM Bell Bass
23. Digital - FM Lattice
24. Digital - Fold Triangle
25. Digital - Hyperfold
26. Digital - Comb Sweep
27. Digital - Phase Distortion Reso
28. Digital - Phase Bloom
29. Digital - Odd to Even
30. Digital - Riddim
31. Digital - Neuro
32. Digital - Screech
33. Digital - Chip
34. Digital - Bitcrush Saw
35. Digital - Metallic
36. Digital - Laser
37. Digital - Harmonic Steps
38. Digital - Spiral Partials
39. Digital - Xenharmonic
40. Growl - Bite
41. Growl - Jaw
42. Growl - Wub
43. Growl - Yoi
44. Growl - Snarl
45. Growl - Throat
46. Growl - Reese Grind
47. Growl - Squelch
48. Growl - Scream
49. Growl - Mouth Drive
50. Vocal - Vowels AEIOU
51. Vocal - Choir Aah
52. Vocal - Choir Ah to Oo
53. Vocal - Formant Sweep
54. Vocal - Talk
55. Vocal - Wow
56. Vocal - Yeah
57. Vocal - Throat Song
58. Vocal - Vocoder
59. Vocal - Angel
60. Vocal - Bass AEIOU
61. Keys - Digital EP
62. Keys - Reed EP
63. Keys - Grand Bright
64. Keys - Vibraphone
65. Keys - Kalimba
66. Keys - Glockenspiel
67. Keys - Clavinet Cycles
68. Keys - Harp Bright
69. Orchestra - Cello Section
70. Orchestra - Violin Section
71. Orchestra - French Horn
72. Orchestra - Trumpet
73. Orchestra - Flute
74. Orchestra - Chapel Organ
75. Orchestra - Clarinet
76. Organic - Harmonium
77. Organic - Music Box
78. Organic - Glass Bell
79. Organic - Koto
80. Organic - Pipe Organ
81. Organic - String Machine
82. Organic - Strings to Voices
83. Organic - Handpan
84. Organic - Felt to Tine
85. Spectral - Fifth Stack
86. Spectral - Major Chord
87. Spectral - Minor Seven
88. Spectral - Octave Stack
89. Spectral - Overtone Singer
90. Spectral - Shimmer
91. Spectral - Bell Chord
92. Swarm - Seven Saws
93. Swarm - Fifth Swarm
94. Swarm - Octave Swarm
95. Swarm - PWM Ensemble
96. Swarm - Slow Choir Saws
97. Textures - Singing Bowl
98. Textures - Tubular Bell
99. Textures - Warm to Glass
100. Textures - Harmonic Bloom
101. Cinematic - Dark Choir
102. Cinematic - Horizon
103. Cinematic - Bowed Metal
104. Cinematic - Low Brass Blare
105. Digital - FM 1-1
106. Digital - Phase Distortion Saw
107. Analog - Saw Filter Sweep
108. Spectral - Soft Saw
109. Analog - Tape Wear
110. Organic - Ocarina
111. Organic - Soft Brass
112. Analog - Tape Saw
113. Spectral - Dark Saw
114. Spectral - Spectral Smear
115. Organic - Cello
116. Digital - Digital 2
117. Analog - Sync Square
118. Digital - FM 1-3
119. Digital - FM 1-4
120. Digital - FM 3-2
121. Organic - Horn
122. Spectral - Overtone Cycles
123. Digital - Phase Grit
124. Textures - Birds Cycles
125. Organic - Oboe
126. Spectral - Cloud
127. Analog - Square Filter Sweep
128. Organic - Felt Upright
129. Analog - Poly Stack
130. Analog - Poly Brass

## Bronnen en naamsvermelding

```
Golf wavetables: sources and credits
=====================================

Most of Golf's tables are Golf's own, generated from formulas (tools/make_wavetables.py and the "Golf (generated)"
tables of tools/make_wavetables2.py). The tables below were made from recordings or single-cycle waveforms that their
authors dedicated to the public domain (CC0 1.0, https://creativecommons.org/publicdomain/zero/1.0/, or The Unlicense),
released without restrictions, or licensed under Creative Commons Attribution (CC BY). CC BY requires the credit given
here; for the others we give it anyway, with thanks.

Every table here is a resynthesis: harmonic levels measured from the source, rebuilt as new 2048-sample frames. No
source audio is shipped as such. tools/make_wavetables2.py names the exact source file of every table.

Adventure Kid Waveforms (AKWF)
  Author:  Kristoffer Ekstrand / Adventure Kid, https://www.adventurekid.se
  Source:  https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE (folder AKWF/, commit 8de90bf)
  Licence: CC0 1.0. "To the extent possible under law, Kristoffer Ekstrand has waived all copyright and related or
           neighboring rights to AKWF Waveforms." "The AKWF Waveforms are released into the public domain so do your
           thing!"
  Tables:  every table whose name ends in "Cycles".

VSCO 2 Community Edition
  Author:  Versilian Studios, https://versilian-studios.com/vsco-community/
  Licence: CC0 1.0
  Tables:  Orchestra - Cello Section, Cello Section Low, Viola Section, Violin Section, Solo Violin, Flute, Clarinet,
           Oboe, French Horn, Trombone, Trumpet, Cello Bow, Violin Bow, Horn Swell, Flute Breath; Keys - Pizzicato.

Versilian Community Sample Library (VCSL)
  Author:  Versilian Studios and contributors, https://github.com/sgossner/VCSL
  Licence: CC0 1.0
  Tables:  Keys - Grand Soft, Grand Hard, Grand Low, Grand Bright (Kawai grand), Vibraphone, Marimba, Marimba Low,
           Glockenspiel, Kalimba, Harp, Harp Bright; Orchestra - Chapel Organ;
           Keys - Upright Gentle, Upright, Upright Deep (Knight upright, sampled by Simon Dalzell of Ivy Audio), Hand Chimes,
           Hand Chimes High, Kalimba Tanzania.

Upright Piano KW
  Author:  Gonzalo and Roberto (FreePats project), a Kawai upright; http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html#UprightKW
  Licence: CC0 1.0 ("Published under the terms of Creative Commons CC0 public domain dedication")
  Tables:  Keys - Upright Soft, Upright Hard, Upright Low, Upright High, Upright Muted.

Big Cat cello
  Author:  Karoryfer Lecolds and bigcat instruments, cello played by Kamila Borowiak; https://karoryfer.com
  Licence: CC0 1.0
  Tables:  Orchestra - Solo Cello, Solo Cello Low.

Osiris Piano
  Author:  Versilian Studios LLC and Karoryfer Lecolds
  Licence: CC0 1.0
  Tables:  Keys - Soft Grand, Soft Grand Bright, Soft Grand Low, Soft Grand Pedal (una corda recordings).

Hohner Pianet T (CC BY 3.0, credit required)
  Credit:  Hohner Pianet T samples by Greg Sullivan (http://www.sullivang.net/), mapped by kinwie.
           Licensed under Creative Commons Attribution 3.0 Unported, https://creativecommons.org/licenses/by/3.0/
           Changed: resynthesised into wavetables (harmonic levels measured over the note's decay).
  Tables:  Keys - Reed EP Soft, Reed EP.

Epic Choir / VocalSet (CC BY 4.0, credit required)
  Credit:  Choir samples derived from VocalSet: Julia Wilkins, Prem Seetharaman, Alison Wahl, Bryan Pardo (2018),
           "VocalSet: A Singing Voice Dataset", https://doi.org/10.5281/zenodo.1442513; epic-choir by thepok.
           Licensed under Creative Commons Attribution 4.0, https://creativecommons.org/licenses/by/4.0/
           Changed: resynthesised into wavetables (harmonic levels of the p, mf and f layers).
  Tables:  Vocal - Sopranos Ah, Sopranos Oo, Altos Ah, Altos Oo, Tenors Ah, Tenors Oo, Basses Ah, Basses Oo,
           Choir Ah to Oo.

University of Iowa Musical Instrument Samples (2012)
  Author:  University of Iowa Electronic Music Studios (Lawrence Fritts, director)
  Licence: "freely available ... and may be downloaded and used for any projects, without restrictions."
  Tables:  Keys - Bowed Vibraphone, Bowed Vibraphone Low.

publicsamples / modularsamples (The Unlicense)
  Author:  modularsamples, https://github.com/publicsamples (also on Freesound as CC0)
  Licence: The Unlicense: "This is free and unencumbered content released into the public domain."
  Tables:  Keys - Digital EP (recorded from an Ensoniq ESQ-1); Analog - Six Voice Pad, Ensemble Strings (Roland
           Juno-6), PWM Strings, Poly Pad (Roland Jupiter-4), Dark Chorus Pad, Thin Brass (Roland JX-8P), Tube Strings
           (Oberheim Xpander), Mono Bass Decay, Mono Bass Velocity (Moog Minitaur).
           (Instrument names describe the hardware that was recorded; no affiliation with the makers.)


Third table bank (2026-10-27): 94 tables
============================================
54 tables are Golf's own (generated from formulas; no credit needed).

Surge XT third-party wavetables, folder Venus Theory/Chaos/VT Chaos 1.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Cinematic - Chaos Field
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Venus Theory/Chaos/VT Chaos 5.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Cinematic - Chaos Flare
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Venus Theory/Elevate/VT Elevate 2.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Cinematic - Elevate
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Emu VSCO/Brass/STAC Tenor Trombone Splat.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by The Emu (J Riley Hill), built from VSCO 2 CE samples (Versilian Studios, CC0), from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Cinematic - Trombone Stab
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT factory wavetables, Generated/Multi Bandpass Sweep.wt, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables (GPL-3.0)
  Credit:  Wavetable from Surge XT (c) Vember Audio / Claes Johanson and the Surge Synth Team, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Bandpass Arcs
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Philippe Favre/Combs/Combs 4.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Comb Arcs
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder A.Liv/Crust PD/Harmonic 15.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by A.Liv, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Crust Bloom
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Mutable Instruments Tides wavetable data (bank1 row5), https://github.com/pichenettes/eurorack/blob/master/tides/resources.cc (MIT)
  Credit:  Wavetable data from Mutable Instruments Tides by Emilie Gillet, MIT licence (ship the MIT notice)
  Tables:  Digital - Drawbar Steps
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Surge XT third-party wavetables, folder Venus Theory/FM/VT FM 4.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - FM Lattice
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Spherical Wavetable Navigator firmware, inc/spheres/wavetable_SWN_wf_rm_hs.h layer3, https://github.com/4ms/SWN/tree/master/inc/spheres (MIT)
  Credit:  Wavetable data from the Spherical Wavetable Navigator firmware, Copyright (c) 2017 Dan Green, MIT licence (ship the MIT notice; the maker asks that its company name is not used on derivatives)
  Tables:  Digital - Folded Ring
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Mutable Instruments Plaits wavetable data (bank1 row5), https://github.com/pichenettes/eurorack/blob/master/plaits/resources.cc (MIT)
  Credit:  Wavetable data from Mutable Instruments Plaits by Emilie Gillet, MIT licence (ship the MIT notice)
  Tables:  Digital - Harmonic Stairs
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Mutable Instruments Plaits wavetable data (bank1 row6), https://github.com/pichenettes/eurorack/blob/master/plaits/resources.cc (MIT)
  Credit:  Wavetable data from Mutable Instruments Plaits by Emilie Gillet, MIT licence (ship the MIT notice)
  Tables:  Digital - Harmonic Steps
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Surge XT third-party wavetables, folder Philippe Favre/FM/FM 5.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Moire FM
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Philippe Favre/Phase Mod/Phase Mod 4.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Phase Bloom
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder TNMG/Chemical Reaction.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by TNMG, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Reaction
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Ocean Swift Synthesis/Wave Remorse 3.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Ocean Swift Synthesis, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Remorse
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Qonundrai/Qonundrum.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Qonundrai, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Riddle
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Spherical Wavetable Navigator firmware, inc/spheres/wavetable_ring_mod_JQ.h layer2, https://github.com/4ms/SWN/tree/master/inc/spheres (MIT)
  Credit:  Wavetable data from the Spherical Wavetable Navigator firmware, Copyright (c) 2017 Dan Green, MIT licence (ship the MIT notice; the maker asks that its company name is not used on derivatives)
  Tables:  Digital - Ring Lattice
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Surge XT third-party wavetables, folder TNMG/Shape Shifter.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by TNMG, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Shape Shifter
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Ocean Swift Synthesis/FM Snags 1.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Ocean Swift Synthesis, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Snagged FM
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Spherical Wavetable Navigator firmware, inc/spheres/wavetable_jero_JQ.h layer2, https://github.com/4ms/SWN/tree/master/inc/spheres (MIT)
  Credit:  Wavetable data from the Spherical Wavetable Navigator firmware, Copyright (c) 2017 Dan Green, MIT licence (ship the MIT notice; the maker asks that its company name is not used on derivatives)
  Tables:  Digital - Spiral Partials
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Surge XT factory wavetables, Generated/Sine Windowed FM.wt, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables (GPL-3.0)
  Credit:  Wavetable from Surge XT (c) Vember Audio / Claes Johanson and the Surge Synth Team, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Windowed FM
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder TNMG/Xenharmonic.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by TNMG, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Digital - Xenharmonic
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Qonundrai/Abrasive Warmth.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Qonundrai, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Abrasive Warmth
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Venus Theory/Acid/VT Acid 5.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Acid Fizz
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Venus Theory/Acid/VT Acid 2.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Acid Pixel
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Qonundrai/Careful Now.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Qonundrai, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Brittle Edge
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Venus Theory/Growl/VT Growl 4.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Grit Engine
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder TNMG/FM Scream Redux.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by TNMG, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Scream
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Venus Theory/Growl/VT Growl 6.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Venus Theory, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Snarl Engine
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Qonundrai/Squelch Pluck.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Qonundrai, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Growl - Squelch
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Emu VSCO/String/PIZZ Bass Soft.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by The Emu (J Riley Hill), built from VSCO 2 CE samples (Versilian Studios, CC0), from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Organic - Plucked Bass Body
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Philippe Favre/Resonances/Resonance 25.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Organic - Resonant Body
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Philippe Favre/Resonances/Resonance 11.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Organic - Struck Resonance
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Surge XT third-party wavetables, folder Philippe Favre/Filtering/Filtering 5.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Swarm - Resonant Glide
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Mutable Instruments Tides wavetable data (bank2 row2), https://github.com/pichenettes/eurorack/blob/master/tides/resources.cc (MIT)
  Credit:  Wavetable data from Mutable Instruments Tides by Emilie Gillet, MIT licence (ship the MIT notice)
  Tables:  Vocal - Buzz Formant
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Surge XT third-party wavetables, folder Philippe Favre/Formant/Formant 6.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by Philippe Favre, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Vocal - Formant Arc
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Mutable Instruments Tides wavetable data (bank2 row1), https://github.com/pichenettes/eurorack/blob/master/tides/resources.cc (MIT)
  Credit:  Wavetable data from Mutable Instruments Tides by Emilie Gillet, MIT licence (ship the MIT notice)
  Tables:  Vocal - Pulse Formant
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).

Surge XT third-party wavetables, folder TNMG/Spectra Voice.wav, https://github.com/surge-synthesizer/surge/tree/main/resources/data/wavetables_3rdparty (GPL-3.0)
  Credit:  Wavetable by TNMG, from Surge XT, GPL-3.0, https://github.com/surge-synthesizer/surge
  Tables:  Vocal - Spectral Voice
  Licence: GNU General Public License v3.0, full text in licences/GPL-3.0.txt. The source data is offered under the GPL-3.0 and was converted into Golf's table format.

Spherical Wavetable Navigator firmware, inc/spheres/hp_TalkativeFM.h layer1, https://github.com/4ms/SWN/tree/master/inc/spheres (MIT)
  Credit:  Wavetable data from the Spherical Wavetable Navigator firmware, Copyright (c) 2017 Dan Green, MIT licence (ship the MIT notice; the maker asks that its company name is not used on derivatives)
  Tables:  Vocal - Talkative FM
  Licence: MIT, full text with copyright notice in licences/ (MIT-*.txt).
```
