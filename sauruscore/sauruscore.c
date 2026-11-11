/* SPDX-License-Identifier: GPL-3.0-only
 * SAURUS for VCV Rack: the SAURUS core. PALEOTONE's sources as its host renders compile them (tests/hostsim.c: one
 * part, mix_block), without the device's main loop: part 1 plays SAURUS, notes go to it directly (voice.c trk_note_on /
 * trk_note_off). After kurbelcore/kurbelcore.c. Nothing of the firmware changes for this file. */
#define main paleotone_hostsim_main
#include "../paleotone/tests/hostsim.c"
#undef main
#undef __attribute__                                   /* (hostsim.c drops attributes: the export needs its own) */
#include "sauruscore.h"

#if defined(_WIN32)
#define JSA_EXPORT __declspec(dllexport)
#else
#define JSA_EXPORT __attribute__((visibility("default")))
#endif

#define JSA_T (&trk[0])

static uint32_t jsa_ms_due;
static float jsa_l[CTL], jsa_r[CTL];
static uint32_t jsa_have, jsa_pos;

static const uint8_t JSA_ID[JSA_NP] = {P_E0, P_E0 + 1, P_E0 + 2, P_E0 + 3, P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7,
                                       P_ATK, P_DEC, P_SUS, P_REL, P_DIST, P_CHOR, P_DLY, P_REV, P_LEVEL, P_VOICE};

static uint32_t jsa_npresets(void) { return ENG_SAURUS.npresets; }
static const char *jsa_preset_name(uint32_t i) { return ENG_SAURUS.presets[i % ENG_SAURUS.npresets].name; }
static void jsa_load_preset(uint32_t i)
{
    int16_t voice = JSA_T->p[P_VOICE], level = JSA_T->p[P_LEVEL];
    host_preset(JSA_T, ENGI_SAURUS, i % ENG_SAURUS.npresets);
    JSA_T->p[P_VOICE] = voice;                       /* the module's VOICE and LEVEL stay */
    JSA_T->p[P_LEVEL] = level;
}

static void jsa_init(uint32_t seed)
{
    host_tracks_init();
    (void)seed;
    song.g[G_BPM] = 120;
    JSA_T->p[P_VOICE] = V_MONO;
    host_preset(JSA_T, ENGI_SAURUS, 1);              /* TYRANT ROAR */
    JSA_T->p[P_VOICE] = V_MONO;
    jsa_have = jsa_pos = 0;
}

static void jsa_block(void)
{
    int32_t o[2 * CTL];
    uint32_t i;
    mix_block(o, CTL);
    for (i = 0; i < CTL; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u];
        l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        jsa_l[i] = (float)l / 32768.0f;
        jsa_r[i] = (float)r / 32768.0f;
    }
    jsa_ms_due += CTL * 1000u;
    while (jsa_ms_due >= FS) {
        jsa_ms_due -= FS;
        fm1_ms++;
    }
    jsa_have = CTL;
    jsa_pos = 0;
}

static void jsa_render(uint32_t n, float *l, float *r)
{
    while (n) {
        uint32_t m;
        if (jsa_pos >= jsa_have)
            jsa_block();
        m = jsa_have - jsa_pos < n ? jsa_have - jsa_pos : n;
        memcpy(l, jsa_l + jsa_pos, m * sizeof *l);
        memcpy(r, jsa_r + jsa_pos, m * sizeof *r);
        jsa_pos += m;
        l += m;
        r += m;
        n -= m;
    }
}

static void jsa_note_on(uint32_t note, uint32_t vel)
{
    trk_note_on(JSA_T, note & 127u, vel < 1u ? 1u : vel > 127u ? 127u : vel);
}
static void jsa_note_off(uint32_t note) { trk_note_off(JSA_T, note & 127u); }
static void jsa_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (JSA_T->v[i].gate)
            trk_note_off(JSA_T, JSA_T->v[i].note);
}

static const param_desc_t *jsa_desc(uint32_t j) { return track_desc(JSA_T, JSA_ID[j < JSA_NP ? j : 0]); }
static void jsa_set(uint32_t j, int32_t v)
{
    if (j < JSA_NP)
        JSA_T->p[JSA_ID[j]] = (int16_t)v;
}
static int32_t jsa_getp(uint32_t j) { return j < JSA_NP ? JSA_T->p[JSA_ID[j]] : 0; }
static const char *jsa_label(uint32_t j)
{
    const param_desc_t *d = jsa_desc(j);
    return d ? d->label : "";
}
static int32_t jsa_min(uint32_t j)
{
    const param_desc_t *d = jsa_desc(j);
    return d ? d->min : 0;
}
static int32_t jsa_max(uint32_t j)
{
    const param_desc_t *d = jsa_desc(j);
    return d ? d->max : 127;
}
static void jsa_format(uint32_t j, int32_t v, char *out, uint32_t n)
{
    char b[24];
    const char *u = "";
    uint32_t i, k;
    const param_desc_t *d = jsa_desc(j);
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

static uint32_t jsa_active(void)
{
    uint32_t i, a = 0;
    for (i = 0; i < NVOICE; i++)
        a += JSA_T->v[i].active ? 1u : 0u;
    return a;
}
static uint32_t jsa_sample_rate(void) { return FS; }
static void jsa_set_bpm(uint32_t bpm) { song.g[G_BPM] = (int16_t)(bpm < 20u ? 20u : bpm > 300u ? 300u : bpm); }
static const char *jsa_call_name(uint32_t c) { return c < JSA_NCALL ? N_SAUR_CALL[c] : ""; }
static int jsa_call_curve(uint32_t c, uint32_t what, int32_t *out)
{
    uint32_t i;
    if (c >= SC_COUNT)
        return 0;
    for (i = 0; i < 5u; i++)
        out[i] = what == 0u ? SAUR_CALL[c].p[i] : what == 1u ? SAUR_CALL[c].m[i] : SAUR_CALL[c].a[i];
    return 1;
}

static const jsa_api jsa_table = {
    JSA_API_VERSION,
    jsa_sample_rate,
    jsa_init,
    jsa_render,
    jsa_note_on, jsa_note_off, jsa_all_off,
    jsa_set, jsa_getp, jsa_label, jsa_min, jsa_max, jsa_format,
    jsa_npresets, jsa_preset_name, jsa_load_preset,
    jsa_active,
    jsa_set_bpm,
    jsa_call_name, jsa_call_curve,
};

JSA_EXPORT const jsa_api *jsa_get(void) { return &jsa_table; }
