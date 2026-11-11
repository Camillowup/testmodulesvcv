# Licensing

Miku-FM has two kinds of content under different terms: the **code** and the **character art**. Not legal advice; for the
character, Crypton Future Media's Japanese texts are the ones that count.

## Code: GPL-3.0-only (and MIT for the old host prototype)

- The firmware and everything built into it is **GPL-3.0-only** ([LICENSE](LICENSE)). It has to be: it is built on
  [Dinghy](https://github.com/zvenson/dinghy) (GPL-3.0-only), and the site's installer uses Felucca's web installer
  (`site/js/`, GPL-3.0-only, by Leo Kuroshita, Hügelton Instruments).
- Files marked `SPDX-License-Identifier: MIT` (the original host prototype and some tools) are MIT
  ([LICENSES/MIT.txt](LICENSES/MIT.txt)).
- The firmware package also carries three JieLi AC79 SDK files that Dinghy's build adds (see Dinghy's licensing).

## The singer: Hatsune Miku fan art under the Piapro Character License (PCL)

The singer's pictures are fan art of **Hatsune Miku**, a character of Crypton Future Media, INC., used under the
[Piapro Character License](https://piapro.jp/license/pcl/summary) and Crypton's
[character usage guidelines](https://piapro.jp/license/character_guideline).

> この作品はピアプロ・キャラクター・ライセンスに基づいてクリプトン・フューチャー・メディア株式会社のキャラクター「初音ミク」を描いたものです。
>
> This work depicts Crypton Future Media, INC.'s character "Hatsune Miku" under the Piapro Character License (PCL),
> https://piapro.jp/license/pcl/summary. © Crypton Future Media, INC. www.piapro.net

**These files are the character art.** They are **not** GPL or MIT, and nobody can relicense them:

- `assets/singer/` (including the paired outfit source sheets), `firmware/ui/singer_sprites.h` , `firmware/ui/singer_compact.h` and `firmware/ui/wardrobe_sprites.h` (that art as pixel data in the firmware),
- `site/media/` pictures and video (banner, icons, `dancer.png`, `screens/`, `trailer.*`), `docs/card*.png`, `docs/screens/`.

**What the PCL means for Miku-FM** (from the PCL text, articles 3 and 4, and the guidelines):

- **Free and non-commercial, always.** No selling, and no payment "under any name" for it: no paid downloads, no
  donations or tips for it, no ads. Do not sell hardware with Miku-FM installed.
- **Not offensive**, nothing that harms the character's or Crypton's reputation, nothing against public order.
- **Not official.** Miku-FM is an unofficial fan project: not made, endorsed or checked by Crypton Future Media, Yamaha
  or M-VAVE, and it must not look like an official product.
- **No sublicensing.** Anyone who uses the art does so under the PCL themselves, directly with Crypton. The GPL on the
  code does not reach the art.
- **Keep the credit** above where the art is shown or shared (the site and the trailer carry it).
- Using part of the character's name in the title of a fan work is allowed by the PCL (article 3(1)(3)); that is why
  the project is called Miku-FM.

**Want something you can sell, or a fully GPL firmware?** Replace the art with your own (not depicting Crypton's
characters): put new sheets in `assets/singer/` and run `python3 tools/sprite_pack.py`; replace `site/media/`.

## Trademarks

「初音ミク」 (Hatsune Miku) and 「ピアプロ」 (piapro) are registered trademarks of Crypton Future Media, INC. VOCALOID is a
registered trademark of Yamaha Corporation. M-VAVE and FM-1 are trademarks of their owners. Miku-FM uses no voice data
or software from Crypton or Yamaha: the voice is synthesised by its own code.

## The demo song

「おとのはこ」 (`assets/songs/`): original lyrics written for Miku-FM with ChatGPT, provided by Michael Perry; melody original
to Miku-FM. Free to use with the firmware.

### Additional generated presentation assets

`companion/media/*-face.png` and `companion/media/*-stage.png` are exports of the
same packed character pixels. They retain the PCL notice and noncommercial
restrictions above. The companion displays the attribution beside the artwork. Character review
screenshots and sprite contact sheets in `docs/evidence/native-integration/`
retain the same PCL notice; see that directory's README.
`assets/stage/effects.png`, `firmware/ui/stagefx_sprites.h` and
`companion/media/effects.png` are the separately generated generic effects art
listed in `assets/stage/README.md` and `assets/NOTICE.md`; the code licence does
not relicense their pixels. Factory-song provenance is documented in
`assets/songs/factory/README.md`; no third-party recording or modern
accompaniment is bundled.

## Embedded DX7 engine and factory patches

`firmware/audio/dx7/dx7.c,h`, constants, generated lookup tables and reference
fixtures adapt Google MSFA (Copyright2012–2013 Google Inc.) under Apache-2.0.
The complete license is in `firmware/audio/dx7/LICENSE`; source revision and
changes are in its README. The combined application remains GPL-3.0-only.

`assets/dx7/factory.packed` and generated `firmware/audio/dx7/bank.h` hold ten
**original** DX7 voices authored for this project in `tools/dx7_candidates.py`. They
contain no third-party patch data and are GPL-3.0-only project data like the rest of
the application. (An earlier eight-voice bank taken from Dexed's bundled cartridges had
no clear redistribution licence and was removed; it appears only in historical
documents.) Users may load other DX7 voices themselves; MIKU does not redistribute them.
The MSFA engine's Apache-2.0 licence is separate and unchanged. The regression
fixtures in `firmware/audio/dx7/reference_vectors.h` are raw outputs of unmodified MSFA
for these original voices (Apache-2.0 header retained). Engine and patch licenses do not
change the character art's PCL terms. DX7 is a Yamaha product name; this remains unofficial software.
