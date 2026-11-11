/* SPDX-License-Identifier: GPL-3.0-only
 * FUMI for VCV Rack: FuMi-1's instrument (fumi.tar.gz, firmware/src/dsp/fumi.c, one compilation unit) behind a small C
 * interface (fumicore.h). The instrument runs as on the FM-1: its own render, its command queue, its 44.1 kHz; FUMI
 * plays it by notes in cents (V/OCT, per channel), its ornaments and its bend, and leaves the looper alone.
 * Built as a shared library; every module instance loads a private copy (the instrument's state is static). */
#include "fumicore.h"
#include "dsp/fumi.c"

#if defined(_WIN32)
#define JFU_EXPORT __declspec(dllexport)
#else
#define JFU_EXPORT __attribute__((visibility("default")))
#endif

_Static_assert((int)JFU_HON == (int)P_HON && (int)JFU_SCALE == (int)P_SCALE && (int)JFU_TUNING == (int)P_TUNING && (int)JFU_DEPTH == (int)P_DEPTH &&
               (int)JFU_FINE == (int)P_FINE && (int)JFU_A4 == (int)P_A4 && (int)JFU_VOICE == (int)P_VOICE && (int)JFU_YOIN == (int)P_YOIN &&
               (int)JFU_LEVEL == (int)P_LEVEL && (int)JFU_VIB_ON == (int)P_VIB_ON && (int)JFU_VIB_RATE == (int)P_VIB_RATE &&
               (int)JFU_VIB_DEPTH == (int)P_VIB_DEPTH && (int)JFU_TRILL_ON == (int)P_TRILL_ON && (int)JFU_TRILL_RATE == (int)P_TRILL_RATE &&
               (int)JFU_TRILL_FEEL == (int)P_TRILL_VAR && (int)JFU_MONO == (int)P_MONO && (int)JFU_SLIDE == (int)P_SLIDE &&
               (int)JFU_CHARACTER == (int)P_CHARACTER && (int)JFU_REVERB == (int)P_REVERB && (int)JFU_OROSHI == (int)P_TRILL_OROSHI &&
               (int)JFU_NP == (int)P_NPARAMS, "fumicore.h's parameters follow fumi.h");
_Static_assert((int)JFU_ORN_N == (int)ORN_N && (int)JFU_ORN_ATOOSHI == (int)ORN_ATOOSHI, "fumicore.h's ornaments follow fumi.h");

/* MIDI out goes nowhere here */
void fm_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static int32_t buf[2 * 256];
static tuning_t tun_ui;                           /* the tuning as set, for the pitch helpers (the render keeps its own) */
static int bend_up, bend_down;
static int16_t shadow[P_NPARAMS];
static int shadow_ok;
static void shadow_init(void);

static uint32_t c_rate(void) { return 44100u; }
static void c_init(uint32_t seed)
{
    (void)seed;
    fm_init();
    tuning_defaults(&tun_ui);
    shadow_ok = 0;
    shadow_init();
    bend_up = bend_down = 0;
    fm_set(P_MIDI_OUT, 0);
    shadow[P_MIDI_OUT] = 0;
}
static void c_render(uint32_t n, float *l, float *r)
{
    while (n) {
        uint32_t k = n > 256u ? 256u : n, i;
        fm_render(buf, k, 4096u);
        for (i = 0; i < k; i++) {                 /* FuMi-1 is quiet (the Suiko is gentle; MASTER makes up for it on the
                                                   * FM-1): x8 (+18 dB) brings its loudest patch near 0.6 */
            l[i] = (float)buf[2 * i] * (8.0f / 8388608.0f);
            r[i] = (float)buf[2 * i + 1] * (8.0f / 8388608.0f);
        }
        l += k;
        r += k;
        n -= k;
    }
}
/* fm_set posts to the render's queue; the value is applied (and clamped) there: keep our own copy for get() */
static void shadow_init(void)
{
    int i;
    if (shadow_ok)
        return;
    for (i = 0; i < P_NPARAMS; i++)
        shadow[i] = PARAMS[i].def;
    shadow_ok = 1;
}
static int32_t clampp(uint32_t p, int32_t v)
{
    const fm_param_t *pi = fm_param_info((int)p);
    return v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
}
static void c_set(uint32_t p, int32_t v)
{
    if (p >= P_NPARAMS)
        return;
    shadow_init();
    v = clampp(p, v);
    shadow[p] = (int16_t)v;
    fm_set((int)p, (int)v);
    /* the pitch helpers below read the tuning at once: keep a copy of it in step (the render's own is set by apply) */
    if (p == P_HON) tun_ui.hon = (int8_t)v;
    if (p == P_SCALE) tun_ui.scale = (uint8_t)v;
    if (p == P_TUNING) tun_ui.tuning = (uint8_t)v;
    if (p == P_DEPTH) tun_ui.depth = (float)v * 0.01f;
    if (p == P_FINE) tun_ui.fine = (float)v;
    if (p == P_A4) tun_ui.a4 = (float)v;
}
static int32_t c_get(uint32_t p) { shadow_init(); return p < P_NPARAMS ? shadow[p] : 0; }
static int32_t c_min(uint32_t p) { return fm_param_info((int)p)->lo; }
static int32_t c_max(uint32_t p) { return fm_param_info((int)p)->hi; }
static int32_t c_def(uint32_t p) { return fm_param_info((int)p)->def; }
static void c_format(uint32_t p, int32_t v, char *out, uint32_t n)
{
    char b[16];
    uint32_t i;
    if (!n)
        return;
    if (p == P_YOIN) {                            /* 余韻 as a time, as apply() makes it: 20 ms .. 8 s */
        float t = 0.02f * fm_exp2f((float)clampp(p, v) * 0.01f * 8.64f);
        int ds = (int)(t * 10.0f + 0.5f);
        if (t < 1.0f) {
            int ms = (int)(t * 1000.0f + 0.5f);
            b[0] = 0;
            itoa_s(ms, b);
            for (i = 0; b[i]; i++) {}
            b[i++] = 'm'; b[i++] = 's'; b[i] = 0;
        } else {
            itoa_s(ds / 10, b);
            for (i = 0; b[i]; i++) {}
            b[i++] = '.'; b[i++] = (char)('0' + ds % 10); b[i++] = 's'; b[i] = 0;
        }
    } else {
        fm_param_text((int)p, (int)v, b);
    }
    for (i = 0; i + 1 < n && b[i]; i++)
        out[i] = b[i];
    out[i] = 0;
}
static void c_note(uint32_t ch, int on, float cents, int vel)
{
    if (ch >= JFU_NCH)
        return;
    if (on)
        fm_note_vel(FM_NOTE_ID + (int)ch, 1, cents, vel);
    else
        fm_note(FM_NOTE_ID + (int)ch, 0, 0.0f);
}
static float c_pitch(float midi, int snap)
{
    int san = tuning_san_midi(&tun_ui), n;
    float frac = 0.0f;
    if (snap) {
        const uint8_t *deg = TN_SCALE[tun_ui.scale < SC_N ? tun_ui.scale : SC_IN];
        float rel = midi - (float)san, best = 1e9f;
        int oct = (int)fm_floorf(rel / 12.0f), o, k;
        n = san;
        for (o = oct - 1; o <= oct + 1; o++)
            for (k = 0; k < 5; k++) {
                float d = (float)(deg[k] + 12 * o) - rel;
                if (d < 0.0f)
                    d = -d;
                if (d < best) {
                    best = d;
                    n = san + deg[k] + 12 * o;
                }
            }
    } else {
        n = (int)fm_floorf(midi + 0.5f);
        frac = midi - (float)n;
    }
    return 100.0f * ((float)(n - 69) + frac) + tuning_offset(&tun_ui, n - san) + tun_ui.fine;
}
static float c_white(uint32_t key) { return tuning_white_cents(&tun_ui, (int)key); }
static int c_san(void) { return tuning_san_midi(&tun_ui); }
static void c_orn(uint32_t orn, int on) { if (orn < ORN_N) fm_ornament((int)orn, on ? 1 : 0); }
static void c_bend(int up, int down, int held)
{
    if (up != bend_up)
        fm_bend(BEND_UP, up, held);
    if (down != bend_down)
        fm_bend(BEND_DOWN, down, held);
    bend_up = up;
    bend_down = down;
}
static void c_panic(void) { fm_panic(); }
static uint32_t c_npatch(void) { return (uint32_t)fm_patch_count(); }
static const char *c_pname(uint32_t i) { return fm_patch_name((int)i); }
static const char *c_pja(uint32_t i) { return fm_patch_ja((int)i); }
static uint32_t c_pfam(uint32_t i) { return (uint32_t)fm_patch_family((int)i); }
static uint32_t c_voices(void) { return fm_nvoices; }
static float c_peak(void) { return fm_peak; }

static const jfu_api API = {
    JFU_API_VERSION, c_rate, c_init, c_render, c_set, c_get, c_min, c_max, c_def, c_format, c_note, c_pitch, c_white,
    c_san, c_orn, c_bend, c_panic, c_npatch, c_pname, c_pja, c_pfam, c_voices, c_peak,
};

JFU_EXPORT const jfu_api *jfu_get(void) { return &API; }
