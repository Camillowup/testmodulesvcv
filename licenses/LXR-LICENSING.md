# The LXR firmware's sound engine in KESSEL

KESSEL and KESSEL FX play the sound engine of the **LXR firmware** by **Erica Synths**
(github.com/erica-synths/lxr-desktop-app, commit a43cd4f, 23 Oct 2026; see `kessel/SNAPSHOT.txt` in `kessel.tar.gz`).

- Licence: **GPL-3.0-only**, with the additional permission and the additional terms of the firmware's `NOTICE.md`
  (copied here as `LXR-NOTICE.md`; the full GPL text is `../LICENSE`).
- Taken: the headers the sound engine needs and its sources (the voices, oscillators, filters, envelopes,
  transients, wavetables, buffer tools, the effects and the effect bus, the init kit), **unchanged**.
- KESSEL's own on top (`kesselcore/`): the glue that renders each voice alone and runs the effect bus on a block of
  its own; `kc_cc.c`, the firmware's parameter handler (`midiParser_ccHandler`) taken over with its menu, NRPN and
  modulation bookkeeping commented out; stand-ins for the STM32 HAL (`kesselcore/compat/`).
- **Marked as modified** (NOTICE.md, additional term 2): this is a modified version of the LXR's sound engine,
  running as a VCV Rack module. It is not official Erica Synths firmware.
- **Trademarks** (NOTICE.md, additional term 1): "Erica Synths", "LXR" and "LXR-02" are Erica Synths' names; the
  module is called KESSEL and is not affiliated with, approved by or endorsed by Erica Synths. The names appear only
  to say truthfully where the engine comes from.
- Not taken: anything of the LXR's hardware, its front panel or its sequencer.
