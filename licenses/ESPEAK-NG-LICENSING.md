# eSpeak NG in LIED

LIED speaks its lyrics with **eSpeak NG 1.52.0** (https://github.com/espeak-ng/espeak-ng), by Jonathan Duddington
(eSpeak), Reece H. Dunn and the eSpeak NG contributors.

- The library sources (`../espeak.tar.gz`: `src/libespeak-ng`, `src/ucd-tools`, `src/include`) are under the
  **GNU General Public License, version 3 or later** (`GPL-3.0-or-later`); the full text is in `../LICENSE` and in
  the archive (`espeak/COPYING`).
- ucd-tools (Reece H. Dunn), part of the archive: GPL-3.0-or-later; the Unicode Character Database it is built from:
  Unicode License (`espeak/COPYING.UCD` in the archive).
- The compiled language data in `../res/espeak-ng-data` (phoneme tables, dictionaries of de, en, fr, es, it, nl, pt,
  sv, pl, fi, la, the language and voice files) is built from the same release with its own tools, unchanged, and is
  under the same licence (GPL-3.0-or-later).
- Changed for LIED (see `espeak/SNAPSHOT.txt`): two lines so that the Klatt voices also honour
  `espeak_ng_SetConstF0()`. `../liedcore/` (the wrapper) is GPL-3.0-or-later as well.

GPL-3.0-or-later code may be part of a GPL-3.0-only work; the plugin as a whole stays GPL-3.0-only.
