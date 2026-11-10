# Third-party material in GENRA Test

GENRA Test as a whole is free software under the **GNU General Public License, version 3 only** (`GPL-3.0-only`, full
text in `../LICENSE`). It contains the following parts by other authors. Each keeps its own licence; the texts are in
this folder. All of them are compatible with distributing the plugin under GPL-3.0.

| Component | Author(s) | Licence | Used in | Text |
| --- | --- | --- | --- | --- |
| JIANT FM, a fork of Felucca (firmware engine MATH, with the firmware around it) | Juan Ignacio Antonucci (Juan Jiant); Leo Kuroshita, Hügelton Instruments (Felucca) | GPL-3.0-only | MATH (`mathcore/`, `jiant.tar.gz`) | `../LICENSE`, `JIANT-Felucca-LICENSING.md` |
| msfa (FM synthesis from Dexed), integer port in Felucca | Google Inc., Pascal Gauthier; port by Leo Kuroshita | Apache-2.0 | JIANT firmware build | `Apache-2.0-msfa.txt` |
| CMU Pronouncing Dictionary (word list in the firmware) | Carnegie Mellon University | BSD-2-Clause | JIANT firmware build | `BSD-2-Clause-CMUdict.txt` |
| Fukiai icon font, fm1-clk320-probe (parts of the JIANT firmware tree) | Hügelton Instruments; curist | MIT | JIANT firmware build | `MIT-Fukiai.txt`, `MIT-fm1-clk320.txt` |
| Chakra Petch, IBM Plex Mono, Michroma (rasterised into the JIANT firmware) | their project authors | SIL OFL 1.1 | JIANT firmware build | `OFL-ChakraPetch.txt`, `OFL-IBMPlexMono.txt`, `OFL-Michroma.txt` |
| GENRA's shared sources (`../genra/`: knobs, Follow GENRA, link hub, JIANT UI) | Bastian Ofner (GENRA) | GPL-3.0-only | the whole plugin | `../LICENSE` |
