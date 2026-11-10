"""Shared pieces of the GENRA panels (tools/*_panel.py).

The GENRA strip: every instrument takes GENRA's track outputs at the same place, with the same labels, in the same
order, bottom left: V/OCT · GATE · VEL · ACC · SLIDE · MOD on a tray in the track colour, CLOCK · RESET on a grey
tray right after it. An instrument leaves out what it has no use for; the order of the rest stays.
"""

STRIP = ["V/OCT", "GATE", "VEL", "ACC", "SLIDE", "MOD"]
SYNC = ["CLOCK", "RESET"]
TRAY = "#141519"
SYNC_LINE = "#5a5c66"
SLOT = 9.4      # jack pitch on the strip (mm)
GAP = 2.2       # between the note tray and the sync tray
TRAY_TOP = -4.6  # tray top relative to the jack centre
TRAY_H = 11.2


def dim(color, f=0.35):
    """The strip's line as printed: the track colour dimmed (it lights up when GENRA feeds the jack, src/Follow.cpp)."""
    c = color.lstrip("#")
    r, g, b = (int(c[i:i + 2], 16) for i in (0, 2, 4))
    t = TRAY.lstrip("#")
    tr, tg, tb = (int(t[i:i + 2], 16) for i in (0, 2, 4))
    return "#%02x%02x%02x" % (round(r * f + tr * (1 - f)), round(g * f + tg * (1 - f)), round(b * f + tb * (1 - f)))


def _tray(e, x, y, n, line):
    w = n * SLOT
    e(f'<rect x="{x + 0.3:.2f}" y="{y + TRAY_TOP:.2f}" width="{w - 0.6:.2f}" height="{TRAY_H}" rx="1" fill="{TRAY}"/>')
    e(f'<rect x="{x + 1.2:.2f}" y="{y + TRAY_TOP:.2f}" width="{w - 2.4:.2f}" height="0.55" rx="0.27" fill="{line}"/>')


def genra_strip(jack, e, x0, y, have, color, sync=None):
    """Draw the strip from x0 (left edge) with jack centres at y.
    have: {label: component name} of the note inputs the instrument has (labels from STRIP);
    sync: the same for CLOCK / RESET (None or {} = no sync tray).
    jack(name, x, y, label) and e(svg) are the panel script's own functions. Returns the x after the strip."""
    notes = [lb for lb in STRIP if lb in have]
    x = x0
    if notes:
        _tray(e, x, y, len(notes), dim(color))
        for i, lb in enumerate(notes):
            jack(have[lb], x + SLOT / 2 + i * SLOT, y, lb)
        x += len(notes) * SLOT
    sy = [lb for lb in SYNC if sync and lb in sync]
    if sy:
        x += GAP
        _tray(e, x, y, len(sy), SYNC_LINE)
        for i, lb in enumerate(sy):
            jack(sync[lb], x + SLOT / 2 + i * SLOT, y, lb)
        x += len(sy) * SLOT
    return x


def strip_width(n_notes, n_sync):
    return n_notes * SLOT + (GAP + n_sync * SLOT if n_sync else 0)
