/* SPDX-License-Identifier: GPL-3.0-only
 * SWARM for VCV Rack: the SWARM core. The JIANT FM firmware's sources as its browser build compiles them
 * (web/emu/felucca_web.c), used as its host renders use them (tests/hostsim.c: one part, mix_block): part 1 is EXT
 * (eng_input.c) with SOURCE SWARM, always sounding (THRU; the module gates it for KEYS). There is no radio: the air
 * below makes up the devices around and hands their advertisements to sw_heard, as the BLE interrupt (bt_air_tap)
 * would. NEAR is the closeness PROX would give (sw_pxon / sw_pxv). Nothing of the firmware changes for this file.
 * After mathcore/mathcore.c. */
#include "../jiant/web/emu/felucca_web.c"
#include "swarmcore.h"

#if defined(_WIN32)
#define JSW_EXPORT __declspec(dllexport)
#else
#define JSW_EXPORT __attribute__((visibility("default")))
#endif

#define JSW_T (&trk[0])
#define JSW_PRESET0 9u                               /* INPUT_PRESETS: SWARM, SWARM PLUCK, SWARM CHOIR */
#define JSW_NPRESET 3u

static uint32_t jsw_ms_due;
static float jsw_l[CTL], jsw_r[CTL];
static uint32_t jsw_have, jsw_pos;

static const uint8_t JSW_ID[JSW_NP] = {P_E0 + 1, P_E0 + 2, P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7,
                                       P_DIST, P_CHOR, P_DLY, P_REV, P_LEVEL, P_ROOT, P_SCALE};

/* ------------------------------------------------------------------ the air: the devices around, made up */
#define VD_N 8u
static struct vdev {
    uint8_t addr[6], fam, ch, on;
    uint8_t data[10];
    uint32_t base;                                   /* its period (ticks, BUSY 0.5) */
    uint32_t next;                                   /* its next advertisement (tick) */
    float home, walk;                                /* its level: where it stands, how far it wandered */
    uint32_t born;                                   /* (the order they came in) */
} vd[VD_N];
static uint32_t vd_seed = 1, vd_rnd = 1, vd_count, vd_order;
static float vd_busy = 0.5f, vd_move = 0.3f, vd_near = 0.5f;
static uint32_t vd_voices = 4;

static uint32_t vd_rand(void) { vd_rnd ^= vd_rnd << 13; vd_rnd ^= vd_rnd >> 17; vd_rnd ^= vd_rnd << 5; return vd_rnd; }
static float vd_unit(void) { return (float)(vd_rand() >> 8) / 16777216.0f; }

/* a device of its own: address (the note), family (Apple Microsoft Google none), bytes, period, place, channel */
static void vd_make(struct vdev *d)
{
    static const uint16_t COMPANY[4] = {0x0499u, 0x004Cu, 0x0006u, 0x00E0u};
    uint32_t i;
    for (i = 0; i < 6u; i++)
        d->addr[i] = (uint8_t)vd_rand();
    d->fam = (uint8_t)(vd_rand() % 4u);
    for (i = 0; i < 10u; i++)
        d->data[i] = (uint8_t)vd_rand();
    d->data[0] = (uint8_t)(COMPANY[d->fam] & 255u);
    d->data[1] = (uint8_t)(COMPANY[d->fam] >> 8);
    d->base = (uint32_t)((0.12f + 1.1f * vd_unit()) * (float)SW_TPS);   /* 120 ms .. 1.2 s */
    d->home = 10.f + 50.f * vd_unit();
    d->walk = 0.f;
    d->ch = (uint8_t)(37u + vd_rand() % 3u);
    d->next = fm1_ticks() + vd_rand() % (d->base | 1u);
    d->born = vd_order++;
    d->on = 1;
}

/* its period now: BUSY 0 a quarter as often .. 1 eight times as often (a power of two either way), a little jitter */
static uint32_t vd_period(const struct vdev *d)
{
    float f = exp2f(2.f - 5.f * vd_busy) * (0.97f + 0.06f * vd_unit());
    float p = (float)d->base * f;
    return p < (float)(SW_TPS / 50u) ? SW_TPS / 50u : p > (float)(6u * SW_TPS) ? 6u * SW_TPS : (uint32_t)p;
}

static void vd_send(struct vdev *d, uint32_t now)
{
    uint8_t p[22];
    float lv;
    uint32_t i;
    p[0] = 0x00;                                     /* ADV_IND */
    p[1] = 20;
    for (i = 0; i < 6u; i++)
        p[2 + i] = d->addr[i];
    p[8] = 3;                                        /* AD: the manufacturer (sw_family) */
    p[9] = 0xFF;
    p[10] = d->data[0];
    p[11] = d->data[1];
    p[12] = 9;                                       /* AD: its data */
    p[13] = 0x16;
    for (i = 0; i < 8u; i++)
        p[14 + i] = d->data[2 + i];
    if (vd_move > 0.f) {                             /* it walks around: its level wanders, MOVE how far */
        d->walk += (vd_unit() - 0.5f) * 14.f * vd_move;
        d->walk *= 0.97f;
        if (vd_unit() < 0.04f * vd_move)             /* (and now and then to the other side) */
            d->ch = (uint8_t)(37u + vd_rand() % 3u);
    }
    lv = d->home * (0.45f + 0.55f * vd_near) + 60.f * vd_near + d->walk;
    lv = lv < 0.f ? 0.f : lv > 126.f ? 126.f : lv;
    sw_heard(p, sizeof p, (uint32_t)lv, d->ch, now);
}

/* once a block: who sends now; MOVE also lets one go and another come */
static void vd_step(void)
{
    uint32_t now = fm1_ticks(), i;
    for (i = 0; i < VD_N; i++) {
        struct vdev *d = &vd[i];
        if (i >= vd_voices || !d->on)
            continue;
        if ((int32_t)(now - d->next) >= 0) {
            vd_send(d, now);
            d->next = now + vd_period(d);
            if (vd_move > 0.f && vd_unit() < 0.004f * vd_move * vd_move)
                vd_make(d);                          /* (gone; another in its place: it swells in, the old fades) */
        }
    }
}

static void vd_renew(uint32_t seed)
{
    uint32_t i;
    vd_seed = seed ? seed : 1u;
    vd_rnd = vd_seed * 2654435761u | 1u;
    for (i = 0; i < VD_N; i++)
        vd_make(&vd[i]);
}

/* ------------------------------------------------------------------ the part */
static void jsw_init(uint32_t seed)
{
    memset(nor, 0xFF, sizeof nor);
    persist_boot();
    settings_init();
    prefs_update();
    host_tracks_init();
    trk_live = 1;
    session_dice(seed ? seed : 1u);
    host_preset(JSW_T, ENGI_INPUT, JSW_PRESET0);
    JSW_T->p[P_VOICE] = V_LEGATO;
    JSW_T->p[P_E3] = 0;                              /* THRU: it sounds; the module gates it for KEYS */
    song.g[G_BPM] = 120;
    sw_pxon = 1;                                     /* NEAR: the closeness, as PROX gives it */
    sw_pxv = 16384;
    jsw_have = jsw_pos = 0;
    vd_renew(seed);
}

static void jsw_block(void)
{
    int32_t o[2 * CTL];
    uint32_t i;
    vd_step();
    mix_block(o, CTL);
    for (i = 0; i < CTL; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u];
        l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        jsw_l[i] = (float)l / 32768.0f;
        jsw_r[i] = (float)r / 32768.0f;
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)l;
    }
    jsw_ms_due += CTL * 1000u;
    while (jsw_ms_due >= FS) {
        jsw_ms_due -= FS;
        fm1_ms++;
        web_ticks += 1000u * FM1_TICKS_PER_US;
    }
    jsw_have = CTL;
    jsw_pos = 0;
}

static void jsw_render(uint32_t n, float *l, float *r)
{
    while (n) {
        uint32_t m;
        if (jsw_pos >= jsw_have)
            jsw_block();
        m = jsw_have - jsw_pos < n ? jsw_have - jsw_pos : n;
        memcpy(l, jsw_l + jsw_pos, m * sizeof *l);
        memcpy(r, jsw_r + jsw_pos, m * sizeof *r);
        jsw_pos += m;
        l += m;
        r += m;
        n -= m;
    }
}

static const param_desc_t *jsw_desc(uint32_t j) { return track_desc(JSW_T, JSW_ID[j < JSW_NP ? j : 0]); }
static void jsw_set(uint32_t j, int32_t v)
{
    if (j < JSW_NP)
        JSW_T->p[JSW_ID[j]] = (int16_t)v;
}
static int32_t jsw_getp(uint32_t j) { return j < JSW_NP ? JSW_T->p[JSW_ID[j]] : 0; }
static const char *jsw_label(uint32_t j)
{
    const param_desc_t *d = jsw_desc(j);
    return d ? d->label : "";
}
static int32_t jsw_min(uint32_t j)
{
    const param_desc_t *d = jsw_desc(j);
    return d ? d->min : 0;
}
static int32_t jsw_max(uint32_t j)
{
    const param_desc_t *d = jsw_desc(j);
    return d ? d->max : 127;
}
static void jsw_format(uint32_t j, int32_t v, char *out, uint32_t n)
{
    char b[24];
    const char *u = "";
    uint32_t i, k;
    const param_desc_t *d = jsw_desc(j);
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

static uint32_t jsw_npresets(void) { return JSW_NPRESET; }
static const char *jsw_preset_name(uint32_t i) { return INPUT_PRESETS[JSW_PRESET0 + i % JSW_NPRESET].name; }
static void jsw_load_preset(uint32_t i)
{
    int16_t level = JSW_T->p[P_LEVEL], root = JSW_T->p[P_ROOT], scale = JSW_T->p[P_SCALE];
    host_preset(JSW_T, ENGI_INPUT, JSW_PRESET0 + i % JSW_NPRESET);
    JSW_T->p[P_VOICE] = V_LEGATO;
    JSW_T->p[P_E3] = 0;
    JSW_T->p[P_LEVEL] = level;                       /* the module's LEVEL, ROOT and SCALE stay */
    JSW_T->p[P_ROOT] = root;
    JSW_T->p[P_SCALE] = scale;
}

static const uint16_t *jsw_vis(uint32_t w, uint32_t h)
{
    if (w > JSW_VIS_W)
        w = JSW_VIS_W;
    if (h > JSW_VIS_H)
        h = JSW_VIS_H;
    ui.frame++;
    cv_begin(w, h, T_BG);
    vx_draw(JSW_T, 0, 0, (int32_t)w, (int32_t)h, -1);
    return cv_px;
}

static void jsw_air(uint32_t voices, float busy, float move, float nearv)
{
    vd_voices = voices < 1u ? 1u : voices > VD_N ? VD_N : voices;
    vd_busy = busy < 0.f ? 0.f : busy > 1.f ? 1.f : busy;
    vd_move = move < 0.f ? 0.f : move > 1.f ? 1.f : move;
    vd_near = nearv < 0.f ? 0.f : nearv > 1.f ? 1.f : nearv;
    sw_pxv = (int16_t)(vd_near * 32767.f);
}
static void jsw_arrive(void)
{
    uint32_t i, k = 0, o = 0xFFFFFFFFu;
    for (i = 0; i < vd_voices && i < VD_N; i++)      /* the one that came first goes */
        if (vd[i].born < o)
            o = vd[i].born, k = i;
    vd_make(&vd[k]);
    vd[k].next = fm1_ticks();
}
static void jsw_renew(uint32_t seed) { vd_renew(seed); }
static uint32_t jsw_seed(void) { return vd_seed; }
static void jsw_set_key(int32_t s) { sw_key = (int8_t)(s < -36 ? -36 : s > 36 ? 36 : s); }
static void jsw_set_clock(uint32_t bpm, uint32_t pos)
{
    song.g[G_BPM] = (int16_t)(bpm < 20u ? 20u : bpm > 300u ? 300u : bpm);
    sm.free = pos;                                   /* (the lead's grid in time with the song) */
}
static uint32_t jsw_devices(void) { return sm.nd; }
static uint32_t jsw_closeness(void) { return sm.mq; }
static uint32_t jsw_sample_rate(void) { return FS; }

static const jsw_api jsw_table = {
    JSW_API_VERSION,
    jsw_sample_rate,
    jsw_init,
    jsw_render,
    jsw_set, jsw_getp, jsw_label, jsw_min, jsw_max, jsw_format,
    jsw_npresets, jsw_preset_name, jsw_load_preset,
    jsw_vis,
    jsw_air, jsw_arrive, jsw_renew, jsw_seed,
    jsw_set_key, jsw_set_clock,
    jsw_devices, jsw_closeness,
};

JSW_EXPORT const jsw_api *jsw_get(void) { return &jsw_table; }
