/* SPDX-License-Identifier: GPL-3.0-only
 * KURBEL for VCV Rack: the KURBEL core. ESTAMPIE's sources as its browser build compiles them (web/emu/felucca_web.c),
 * used the way its host renders use them (tests/hostsim.c: one part, mix_block), without the device's main loop:
 * part 1 plays GURDY (the hurdy-gurdy) or TRIO (the shawm), notes go to it directly (voice.c trk_note_on /
 * trk_note_off). The crank (eng_gurdy.c crank_clicks: SELECT's clicks on the FM-1) is fed a speed instead of clicks,
 * with the same stroke rule. After GENRA's jiantcore/animalcore.c. Nothing of the firmware changes for this file. */
#include "../estampie/web/emu/felucca_web.c"
#include "kurbelcore.h"

#if defined(_WIN32)
#define JKU_EXPORT __declspec(dllexport)
#else
#define JKU_EXPORT __attribute__((visibility("default")))
#endif

#define JKU_T (&trk[0])
#define JKU_ENGI_TRIO 6u                             /* engines.c ENGINES[]: TRIO */

static uint32_t jku_ms_due;
static float jku_l[CTL], jku_r[CTL];
static uint32_t jku_have, jku_pos;
static int32_t jku_shawm = -1;                       /* TRIO's SHAWM preset (ESTAMPIE's) */
static float jku_avg, jku_since_stroke = 1.f;        /* the crank: the recent speed (~140 ms), time since a stroke */

static const uint8_t JKU_ID[JKU_NP] = {P_E0, P_E0 + 1, P_E0 + 2, P_E0 + 3, P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7,
                                       P_ATK, P_DEC, P_SUS, P_REL, P_DIST, P_CHOR, P_DLY, P_REV, P_LEVEL, P_VOICE,
                                       P_RING, P_TEMP, P_ROOT};

static uint32_t jku_npresets(void) { return ENG_GURDY.npresets + (jku_shawm >= 0 ? 1u : 0u); }
static uint32_t jku_preset_engine(uint32_t i) { return i % jku_npresets() < ENG_GURDY.npresets ? JKU_WHEEL : JKU_REED; }
static const char *jku_preset_name(uint32_t i)
{
    i %= jku_npresets();
    return i < ENG_GURDY.npresets ? ENG_GURDY.presets[i].name : ENG_TRIO.presets[jku_shawm].name;
}
static void jku_load_preset(uint32_t i)
{
    int16_t voice = JKU_T->p[P_VOICE], level = JKU_T->p[P_LEVEL], temp = JKU_T->p[P_TEMP];
    i %= jku_npresets();
    if (i < ENG_GURDY.npresets) {
        host_preset(JKU_T, ENGI_GURDY, i);
        JKU_T->p[P_RING] = (int16_t)((GURDY_RING >> i) & 1u);   /* the drone presets ring (ui.c, sound loads) */
    } else {
        host_preset(JKU_T, JKU_ENGI_TRIO, (uint32_t)jku_shawm);
        JKU_T->p[P_RING] = 0;
    }
    JKU_T->p[P_VOICE] = voice;                       /* the module's VOICE, LEVEL and TEMP stay */
    JKU_T->p[P_LEVEL] = level;
    JKU_T->p[P_TEMP] = temp;
}

static void jku_init(uint32_t seed)
{
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    persist_boot();
    settings_init();
    host_tracks_init();
    (void)seed;
    for (i = 0; i < ENG_TRIO.npresets; i++)
        if (!strcmp(ENG_TRIO.presets[i].name, "SHAWM"))
            jku_shawm = (int32_t)i;
    song.g[G_BPM] = 120;
    song.playing = 1;                                /* the transport "runs": RING's drones sound through the rests */
    JKU_T->p[P_VOICE] = V_LEGATO;
    JKU_T->p[P_TEMP] = 0;
    jku_load_preset(0);
    jku_have = jku_pos = 0;
}

static void jku_block(void)
{
    int32_t o[2 * CTL];
    uint32_t i;
    mix_block(o, CTL);
    for (i = 0; i < CTL; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u];
        l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        jku_l[i] = (float)l / 32768.0f;
        jku_r[i] = (float)r / 32768.0f;
    }
    jku_ms_due += CTL * 1000u;
    while (jku_ms_due >= FS) {
        jku_ms_due -= FS;
        fm1_ms++;
        web_ticks += 1000u * FM1_TICKS_PER_US;
    }
    jku_have = CTL;
    jku_pos = 0;
}

static void jku_render(uint32_t n, float *l, float *r)
{
    while (n) {
        uint32_t m;
        if (jku_pos >= jku_have)
            jku_block();
        m = jku_have - jku_pos < n ? jku_have - jku_pos : n;
        memcpy(l, jku_l + jku_pos, m * sizeof *l);
        memcpy(r, jku_r + jku_pos, m * sizeof *r);
        jku_pos += m;
        l += m;
        r += m;
        n -= m;
    }
}

static void jku_note_on(uint32_t note, uint32_t vel)
{
    trk_note_on(JKU_T, note & 127u, vel < 1u ? 1u : vel > 127u ? 127u : vel);
}
static void jku_note_off(uint32_t note) { trk_note_off(JKU_T, note & 127u); }
static void jku_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (JKU_T->v[i].gate)
            trk_note_off(JKU_T, JKU_T->v[i].note);
}

static const param_desc_t *jku_desc(uint32_t j) { return track_desc(JKU_T, JKU_ID[j < JKU_NP ? j : 0]); }
static void jku_set(uint32_t j, int32_t v)
{
    if (j < JKU_NP)
        JKU_T->p[JKU_ID[j]] = (int16_t)v;
}
static int32_t jku_getp(uint32_t j) { return j < JKU_NP ? JKU_T->p[JKU_ID[j]] : 0; }
static const char *jku_label(uint32_t j)
{
    const param_desc_t *d = jku_desc(j);
    return d ? d->label : "";
}
static int32_t jku_min(uint32_t j)
{
    const param_desc_t *d = jku_desc(j);
    return d ? d->min : 0;
}
static int32_t jku_max(uint32_t j)
{
    const param_desc_t *d = jku_desc(j);
    return d ? d->max : 127;
}
static void jku_format(uint32_t j, int32_t v, char *out, uint32_t n)
{
    char b[24];
    const char *u = "";
    uint32_t i, k;
    const param_desc_t *d = jku_desc(j);
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

static uint32_t jku_engine(void) { return JKU_T->engine == ENGI_GURDY ? JKU_WHEEL : JKU_REED; }
static uint32_t jku_active(void)
{
    uint32_t i, a = 0;
    for (i = 0; i < NVOICE; i++)
        a += JKU_T->v[i].active ? 1u : 0u;
    return a;
}
static uint32_t jku_sample_rate(void) { return FS; }
static void jku_set_bpm(uint32_t bpm) { song.g[G_BPM] = (int16_t)(bpm < 20u ? 20u : bpm > 300u ? 300u : bpm); }

/* eng_gurdy.c crank_clicks' rule on a speed: a stroke when the speed is 1.7 x the recent one (~140 ms), on a wheel
 * already turning (6 clicks/s), at most one per 70 ms */
static void jku_crank(float v, float dt)
{
    if (v < 0.f)
        v = 0.f;
    jku_since_stroke += dt;
    if (v < 2.f) {                                   /* (below 2 clicks/s the bow has no pressure: stopped) */
        jku_avg = 0.f;
    } else if (jku_avg <= 0.f) {
        jku_avg = v;
    } else {
        if (v > jku_avg * 1.7f && jku_avg >= 6.f && jku_since_stroke >= 0.07f) {
            crank.strokes++;
            jku_since_stroke = 0.f;
        }
        jku_avg += (v - jku_avg) * (dt < 0.14f ? dt / 0.14f : 1.f);
    }
    crank.speed = (uint32_t)(v * 16.f + 0.5f);
}
static void jku_stroke(void) { crank.strokes++; }
static uint32_t jku_strokes(void) { return crank.strokes; }

static const jku_api jku_table = {
    JKU_API_VERSION,
    jku_sample_rate,
    jku_init,
    jku_render,
    jku_note_on, jku_note_off, jku_all_off,
    jku_set, jku_getp, jku_label, jku_min, jku_max, jku_format,
    jku_npresets, jku_preset_name, jku_preset_engine, jku_load_preset,
    jku_engine,
    jku_active,
    jku_set_bpm,
    jku_crank, jku_stroke, jku_strokes,
};

JKU_EXPORT const jku_api *jku_get(void) { return &jku_table; }
