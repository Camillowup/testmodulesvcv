# Third-party code in GENRA CHOP

GENRA CHOP's WARP (time stretching of the loaded sample) uses two libraries, copied unchanged into `dep/`:

| Library | Version / commit | License | Source |
|---|---|---|---|
| Rubber Band Library 4.0.0 | e4296ac8 (2025-02-27) | GPL-2.0-or-later (`RubberBand-COPYING.txt`) | https://github.com/breakfastquay/rubberband |
| Signalsmith Stretch | 88061fd4 | MIT (`SignalsmithStretch-LICENSE.txt`) | https://github.com/Signalsmith-Audio/signalsmith-stretch |
| Signalsmith Linear | 7bb1c8f3 | MIT (`SignalsmithLinear-LICENSE.txt`) | https://github.com/Signalsmith-Audio/linear |

Rubber Band is GPL; the plugin as a whole is distributed under GPL-3.0-only, which "or later" permits.
