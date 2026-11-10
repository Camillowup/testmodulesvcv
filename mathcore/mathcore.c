/* SPDX-License-Identifier: GPL-3.0-only
 * MATH for VCV Rack: the MATH core. The JIANT FM firmware's sources as its browser build compiles them
 * (web/emu/felucca_web.c), used the way its host renders use them (tests/hostsim.c: one part, mix_block), without the
 * device's main loop: part 1 plays the MATH engine (eng_lofi.c), notes go to it directly (voice.c trk_note_on /
 * trk_note_off), and its being is drawn by the firmware's own code (ui_engvis.c vx_draw) on its canvas (gfx.c).
 * After GENRA's jiantcore/animalcore.c. Nothing of the firmware changes for this file. */
#include "../jiant/web/emu/felucca_web.c"
#include "mathcore.h"

#if defined(_WIN32)
#define JMA_EXPORT __declspec(dllexport)
#else
#define JMA_EXPORT __attribute__((visibility("default")))
#endif

#define JMA_T (&trk[0])

static uint32_t jma_ms_due;
static float jma_l[CTL], jma_r[CTL];
static uint32_t jma_have, jma_pos;

static const uint8_t JMA_ID[JMA_NP] = {P_E0, P_E0 + 1, P_E0 + 2, P_E0 + 3, P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7,
                                       P_ATK, P_DEC, P_SUS, P_REL, P_DIST, P_CHOR, P_DLY, P_REV, P_LEVEL, P_VOICE,
                                       P_LRTRG};

static void jma_init(uint32_t seed)
{
    memset(nor, 0xFF, sizeof nor);
    persist_boot();
    settings_init();
    prefs_update();
    host_tracks_init();
    trk_live = 1;
    session_dice(seed ? seed : 1u);
    host_preset(JMA_T, ENGI_LOFI, 0);
    JMA_T->p[P_VOICE] = V_POLY;
    JMA_T->p[P_LRTRG] = 1;
    song.g[G_BPM] = 120;
    jma_have = jma_pos = 0;
}

static void jma_block(void)
{
    int32_t o[2 * CTL];
    uint32_t i;
    mix_block(o, CTL);
    for (i = 0; i < CTL; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u];
        l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        jma_l[i] = (float)l / 32768.0f;
        jma_r[i] = (float)r / 32768.0f;
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)l;
    }
    jma_ms_due += CTL * 1000u;
    while (jma_ms_due >= FS) {
        jma_ms_due -= FS;
        fm1_ms++;
        web_ticks += 1000u * FM1_TICKS_PER_US;
    }
    jma_have = CTL;
    jma_pos = 0;
}

static void jma_render(uint32_t n, float *l, float *r)
{
    while (n) {
        uint32_t m;
        if (jma_pos >= jma_have)
            jma_block();
        m = jma_have - jma_pos < n ? jma_have - jma_pos : n;
        memcpy(l, jma_l + jma_pos, m * sizeof *l);
        memcpy(r, jma_r + jma_pos, m * sizeof *r);
        jma_pos += m;
        l += m;
        r += m;
        n -= m;
    }
}

static void jma_note_on(uint32_t note, uint32_t vel)
{
    trk_note_on(JMA_T, note & 127u, vel < 1u ? 1u : vel > 127u ? 127u : vel);
}
static void jma_note_off(uint32_t note) { trk_note_off(JMA_T, note & 127u); }
static void jma_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (JMA_T->v[i].gate)
            trk_note_off(JMA_T, JMA_T->v[i].note);
}

static const param_desc_t *jma_desc(uint32_t j) { return track_desc(JMA_T, JMA_ID[j < JMA_NP ? j : 0]); }
static void jma_set(uint32_t j, int32_t v)
{
    if (j < JMA_NP)
        JMA_T->p[JMA_ID[j]] = (int16_t)v;
}
static int32_t jma_getp(uint32_t j) { return j < JMA_NP ? JMA_T->p[JMA_ID[j]] : 0; }
static const char *jma_label(uint32_t j)
{
    const param_desc_t *d = jma_desc(j);
    return d ? d->label : "";
}
static int32_t jma_min(uint32_t j)
{
    const param_desc_t *d = jma_desc(j);
    return d ? d->min : 0;
}
static int32_t jma_max(uint32_t j)
{
    const param_desc_t *d = jma_desc(j);
    return d ? d->max : 127;
}
static void jma_format(uint32_t j, int32_t v, char *out, uint32_t n)
{
    char b[24];
    const char *u = "";
    uint32_t i, k;
    const param_desc_t *d = jma_desc(j);
    if (!n)
        return;
    if (!d) {
        out[0] = 0;
        return;
    }
    param_format(d, v, b, &u);
    for (i = 0; b[i] && i + 1u < n; i++)
        out[i] = b[i];
    for (k = 0; u && u[k] && i + 1u < n; k++)
        out[i++] = u[k];
    out[i] = 0;
}

static uint32_t jma_npresets(void) { return ENG_LOFI.npresets; }
static const char *jma_preset_name(uint32_t i) { return ENG_LOFI.presets[i % ENG_LOFI.npresets].name; }
static void jma_load_preset(uint32_t i)
{
    int16_t voice = JMA_T->p[P_VOICE], level = JMA_T->p[P_LEVEL], rtrg = JMA_T->p[P_LRTRG];
    host_preset(JMA_T, ENGI_LOFI, i % ENG_LOFI.npresets);
    JMA_T->p[P_VOICE] = voice;                       /* the module's VOICE, LEVEL and RTRG stay */
    JMA_T->p[P_LEVEL] = level;
    JMA_T->p[P_LRTRG] = rtrg;
}

static const uint16_t *jma_vis(uint32_t w, uint32_t h)
{
    if (w > JMA_VIS_W)
        w = JMA_VIS_W;
    if (h > JMA_VIS_H)
        h = JMA_VIS_H;
    ui.frame++;
    cv_begin(w, h, T_BG);
    vx_draw(JMA_T, 0, 0, (int32_t)w, (int32_t)h, -1);
    return cv_px;
}
static uint32_t jma_active(void)
{
    uint32_t i, a = 0;
    for (i = 0; i < NVOICE; i++)
        a += JMA_T->v[i].active ? 1u : 0u;
    return a;
}
static uint32_t jma_sample_rate(void) { return FS; }

static void jma_set_pitch(int32_t p16)
{
    uint32_t i;
    p16 = p16 < 0 ? 0 : p16 > 2047 ? 2047 : p16;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &JMA_T->v[i];
        if (v->active) {
            v->pitch16 = p16;
            v->pitch_cur = p16;
        }
    }
}

static void jma_set_clock(uint32_t bpm, uint32_t quarter, uint32_t pos)
{
    song.g[G_BPM] = (int16_t)(bpm < 20u ? 20u : bpm > 300u ? 300u : bpm);
    bar_n = quarter / 4u;
    beat_n = quarter % 4u;
    beat_pos = pos;
}

static const jma_api jma_table = {
    JMA_API_VERSION,
    jma_sample_rate,
    jma_init,
    jma_render,
    jma_note_on, jma_note_off, jma_all_off,
    jma_set, jma_getp, jma_label, jma_min, jma_max, jma_format,
    jma_npresets, jma_preset_name, jma_load_preset,
    jma_vis,
    jma_active,
    jma_set_pitch,
    jma_set_clock,
};

JMA_EXPORT const jma_api *jma_get(void) { return &jma_table; }
