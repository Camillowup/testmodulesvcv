/* SPDX-License-Identifier: GPL-3.0-only
 * CANTA core: MIKU-FM's singing voice for VCV Rack (see cantacore.h).
 * The voice (fvoice), its effects (vfx), the morae and the lyrics grammar are MIKU-FM's, unchanged (canta.tar.gz,
 * canta/SNAPSHOT.txt). What MIKU-FM's firmware/app/engine.c does around them for the vocal track is rebuilt here: the
 * knobs into the voice (the same mappings), the harmony voice, ANIMAL's and SCAT's babbling, the levels and the
 * limiter. The band, the sequencer and the song are not part of it; the presets are CANTA's own. */
#include "cantacore.h"
#include "firmware/audio/fvoice/fvoice.h"
#include "firmware/audio/vfx/vfx.h"
#include "firmware/song/mora.h"
#include "firmware/song/lyrics.h"

#if defined(_WIN32)
#define JCA_EXPORT __declspec(dllexport)
#else
#define JCA_EXPORT __attribute__((visibility("default")))
#endif

#define FS 44100u
#define CAP 256u
#define CHUNK 256u

static FvVoice fv, fv2;
static int32_t p[JCA_NP];
static LyUnit ly[CAP];
static uint32_t nly, lpos;
static int32_t lsung = -1;
static uint32_t rng = 0x6D2B79F5u, bt, bpm = 120;
static uint8_t vnote = 60, vvel = 110, vh, mora_now;
static int32_t buf[2u * CHUNK], vtmp[2u * CHUNK];

/* MIKU-FM's defaults (song.c sg_voice_default, the vocal track's Trk): its 0.5 sound */
static const int32_t DEF[JCA_NP] = {
    8, 79, 10, 50, 0, 0, 50, 63,      /* breath bright porta(10 = 50 ms) tension fry whisper formant human */
    13, 35, 40, 35,                    /* attack release scoop vibrato depth */
    0, 0, 0, 0,                        /* chorus delay reverb drive */
    100, JCA_JP, JCA_MID, 0, 70, 0, 39,  /* level lang chr harmony mix; FOLLOW; FIXED "ra" */
    50, 45, 0, 42, 3, 50, 20,          /* vrate vdel trem rsize dtime(3 = dotted 1/8) dfb crate */
    50, 50, 50, 8, JCA_MID, 50         /* clen clev vspd hdet hchr arate */
};
static const int32_t LO[JCA_NP] = {0};
static const int32_t HI[JCA_NP] = {
    100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
    127, 3, 4, 6, 100, 1, MORA_N - 1,
    100, 100, 100, 100, 4, 100, 100, 100, 100, 100, 100, 4, 100
};
static const char *const LABEL[JCA_NP] = {
    "BREATH", "BRIGHT", "GLIDE", "TENSE", "FRY", "WHISPR", "FORMNT", "HUMAN",
    "ATK", "REL", "SCOOP", "VIB", "CHORUS", "DELAY", "REVERB", "DRIVE",
    "LEVEL", "LANG", "CHAR", "HARMONY", "MIX", "LYRICS", "SYLLABLE",
    "VIB RATE", "VIB DELAY", "TREMOLO", "ROOM", "DLY TIME", "FEEDBACK", "CHO RATE",
    "CONS LEN", "CONS LVL", "VOWEL SPD", "HARM DET", "HARM CHAR", "BABBLE"
};
static const char *const LANGS[4] = {"JAPANESE", "ANIMAL", "SCAT", "VOWELS"};
static const char *const CHRS[5] = {"DEEP", "LOW", "MID", "YOUNG", "TINY"};
static const char *const HINTS[7] = {"OFF", "-12", "+3", "+4", "+5", "+7", "+12"};
static const char *const DTIMES[5] = {"1/16", "1/8T", "1/8", "1/8.", "1/4"};
static const int8_t hiv[7] = {0, -12, 3, 4, 5, 7, 12};
/* engine.c: SCAT's syllables (da ba bi do pa ra ri de be), ANIMAL's small pitch jumps */
static const uint8_t scat[9] = {57, 60, 61, 59, 65, 39, 40, 58, 63};
static const int8_t ajit[8] = {0, 0, 2, 3, 5, -2, 7, 4};

/* ---------------------------------------------------------------- presets (CANTA's own) */
typedef struct { const char *name, *lyrics; int8_t set[24]; } Preset;   /* set: (index, value) pairs, -1 ends */
static const Preset PRE[] = {
    {"KIKO", "ka-ze ni no-ru | so-ra e to-bu | ha-ru no hi", {JCA_LANG, JCA_JP, JCA_CHR, JCA_MID, -1}},
    {"HANA", "ha-na ga sa-ku | mi-chi wo a-ru-ku | ra-ra ra", {JCA_LANG, JCA_JP, JCA_CHR, JCA_YOUNG, JCA_BRIGHT, 88,
        JCA_REVERB, 30, JCA_CHORUS, 20, JCA_SCOOP, 30, -1}},
    {"YUME", "yu-me no na-ka de | ki-mi wo ma-tsu | o-_ o", {JCA_LANG, JCA_JP, JCA_CHR, JCA_LOW, JCA_ATK, 30, JCA_REL, 60,
        JCA_VIB, 55, JCA_GLIDE, 30, JCA_BREATH, 25, JCA_REVERB, 55, JCA_DELAY, 20, -1}},
    {"DUET", "a-o-i u-mi | shi-ro-i fu-ne | ko-ko ni i-ru", {JCA_LANG, JCA_JP, JCA_CHR, JCA_MID, JCA_HINT, 3, JCA_HMIX, 70,
        JCA_HCHR, JCA_LOW, JCA_REVERB, 35, -1}},
    {"ROBO", "ro-bo ro-bo | pi pi pa | de-n-shi no u-ta", {JCA_LANG, JCA_JP, JCA_CHR, JCA_TINY, JCA_HUMAN, 0, JCA_VIB, 0,
        JCA_SCOOP, 0, JCA_TENSE, 80, JCA_BRIGHT, 100, JCA_GLIDE, 0, JCA_DRIVE, 25, -1}},
    {"SASAYAKI", "sa-sa-ya-ku yo | ma-ta a-shi-ta", {JCA_LANG, JCA_JP, JCA_CHR, JCA_YOUNG, JCA_WHISPER, 70, JCA_BREATH, 60,
        JCA_TENSE, 30, JCA_REVERB, 40, JCA_DELAY, 25, -1}},
    {"MOMO", "mo-mo", {JCA_LANG, JCA_ANIMAL, JCA_CHR, JCA_TINY, JCA_BRIGHT, 90, JCA_ARATE, 60, JCA_SCOOP, 55, -1}},
    {"KUMA", "ku-ma", {JCA_LANG, JCA_ANIMAL, JCA_CHR, JCA_DEEP, JCA_FRY, 45, JCA_ARATE, 30, JCA_TENSE, 35, -1}},
    {"NANA", "na-na", {JCA_LANG, JCA_SCAT, JCA_CHR, JCA_MID, JCA_DELAY, 30, JCA_DTIME, 2, JCA_REVERB, 25, -1}},
    {"BEBOP", "be-bo", {JCA_LANG, JCA_SCAT, JCA_CHR, JCA_LOW, JCA_DRIVE, 30, JCA_FRY, 15, JCA_HINT, 1, JCA_HMIX, 45, -1}},
    {"SORA", "a-i-u-e-o", {JCA_LANG, JCA_VOWEL, JCA_CHR, JCA_YOUNG, JCA_ATK, 40, JCA_REL, 65, JCA_CHORUS, 45,
        JCA_REVERB, 60, JCA_VIB, 45, JCA_HINT, 4, JCA_HMIX, 55, -1}},
    {"CHOIR", "a-_ o-_ u-_ o-_", {JCA_LANG, JCA_VOWEL, JCA_CHR, JCA_DEEP, JCA_ATK, 55, JCA_REL, 75, JCA_BREATH, 30,
        JCA_CHORUS, 60, JCA_REVERB, 75, JCA_HINT, 1, JCA_HMIX, 80, JCA_HCHR, JCA_MID, -1}},
};
#define NPRE (sizeof(PRE) / sizeof(PRE[0]))

static uint32_t rnd(void) { uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x; }
static uint32_t animal_wait(uint32_t r) { return (180u - (uint32_t)p[JCA_ARATE] * 4u / 5u) * 441u / 10u + r % 3529u; }
static uint32_t babble_wait(uint32_t r)
{
    return p[JCA_LANG] == JCA_ANIMAL ? animal_wait(r) : p[JCA_LANG] == JCA_SCAT ? 1323000u / (bpm ? bpm : 120u) : 0u;
}
static int32_t hint_st(void) { return hiv[p[JCA_HINT] < 0 ? 0 : p[JCA_HINT] > 6 ? 6 : p[JCA_HINT]]; }
static uint8_t clampn(int32_t n) { return (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n); }

/* the knobs into both voices, as engine.c's en_render does for the vocal track */
static void apply(void)
{
    uint32_t csc = 25u + (uint32_t)p[JCA_CLEN] * 3u / 2u;
    int32_t ps = p[JCA_CHR] * 100 + (p[JCA_FORMANT] - 50) * 2;
    FvVoice *h = &fv2;
    fv.vib_depth = (uint16_t)p[JCA_VIB];
    fv.porta = (uint16_t)(p[JCA_GLIDE] * 5);
    fv.breath = (uint8_t)(p[JCA_BREATH] * 127 / 100);
    fv.bright = (uint8_t)(p[JCA_BRIGHT] * 127 / 100);
    fv.attack = (uint16_t)(p[JCA_ATK] * p[JCA_ATK] / 20);
    fv.release = (uint16_t)(p[JCA_REL] * p[JCA_REL] / 20);
    fv.scoop = (uint8_t)p[JCA_SCOOP];
    fv.human = (uint8_t)(p[JCA_HUMAN] * 127 / 100);
    fv.vib_rate = (uint16_t)(30 + p[JCA_VRATE] / 2);
    fv.vib_delay = (uint16_t)(p[JCA_VDEL] * p[JCA_VDEL] / 10);
    ps = ps < 0 ? 0 : ps > 400 ? 400 : ps;
    fv.chr = (uint8_t)(ps / 100);
    fv.chf = (uint8_t)(ps % 100);
    fv.tension = (uint8_t)p[JCA_TENSE];
    fv.fry = (uint8_t)p[JCA_FRY];
    fv.whisper = (uint8_t)p[JCA_WHISPER];
    fv.cscale = (uint8_t)csc;
    fv.vscale = (uint8_t)(25 + p[JCA_VSPD] * 3 / 2);
    fv.clevel = (uint16_t)(p[JCA_CLEV] * 2);
    h->vib_depth = fv.vib_depth; h->vib_rate = fv.vib_rate; h->vib_delay = fv.vib_delay; h->porta = fv.porta;
    h->attack = fv.attack; h->release = fv.release; h->breath = fv.breath; h->bright = fv.bright;
    h->human = fv.human; h->scoop = fv.scoop; h->tension = fv.tension; h->fry = fv.fry; h->whisper = fv.whisper;
    h->cscale = fv.cscale; h->vscale = fv.vscale; h->clevel = fv.clevel;
    h->chr = (uint8_t)(p[JCA_HCHR] % 5); h->chf = 0; h->tune = (int16_t)(p[JCA_HDET] * 1024 / 1000);
    if (!hint_st() && h->gated) fv_note_off(h);
}

static void hnote(int32_t note, uint32_t cons, uint32_t vw, uint32_t vel)
{
    int32_t iv = hint_st();
    if (!iv) return;
    fv_note_on_mora(&fv2, clampn(note + iv), (uint8_t)cons, (uint8_t)vw, (uint8_t)vel);
}

/* one mora (engine.c's sing): consonant + vowel; a held vowel ('_') or VOWELS has no consonant; moraic n hums */
static void sing(uint32_t note, uint32_t mora, uint32_t vel, uint32_t flags)
{
    uint32_t v, cons;
    if ((flags & LY_SEPARATE) && fv.gated) { fv_note_off(&fv); fv_note_off(&fv2); }
    v = (flags & LY_SUSTAIN) ? vh : mora_vowel(mora);
    cons = p[JCA_LANG] == JCA_VOWEL || (flags & LY_SUSTAIN) ? FC_NONE : mora_cons(mora);
    if (p[JCA_LANG] == JCA_VOWEL && v > 4u) v = vh;     /* VOWELS: moraic n sings the vowel before it */
    if (v < 5u) vh = (uint8_t)v;
    fv_note_on_mora(&fv, (uint8_t)note, (uint8_t)cons, v < 5u ? (uint8_t)v : vh, (uint8_t)vel);
    hnote((int32_t)note, cons, v < 5u ? v : vh, vel);
    if (!(flags & LY_SUSTAIN)) mora_now = (uint8_t)mora;
    vnote = (uint8_t)note;
    vvel = (uint8_t)vel;
    bt = babble_wait(rnd());
}

/* the next sung unit of the lyrics (rests skipped); -1 when there is none */
static int32_t take_unit(void)
{
    uint32_t k;
    for (k = 0; k < nly; k++) {
        uint32_t i = (lpos + k) % nly;
        if (!(ly[i].flags & LY_REST)) {
            lpos = (i + 1u) % nly;
            return (int32_t)i;
        }
    }
    return -1;
}

static void syllable(uint32_t note, uint32_t vel)
{
    uint32_t lang = (uint32_t)p[JCA_LANG];
    if (lang == JCA_ANIMAL || lang == JCA_SCAT) {         /* the first babbled syllable now, the rest while held */
        uint32_t r = rnd(), mo = lang == JCA_ANIMAL ? (r % 3u ? 6u + r % 40u : 47u + r % 23u) : scat[r % 9u];
        lsung = -1;
        sing(note, mo, vel, 0);
        return;
    }
    if (p[JCA_LYRICS] == 1) {
        lsung = -1;
        sing(note, (uint32_t)p[JCA_FIXED], vel, 0);
        return;
    }
    lsung = take_unit();
    if (lsung < 0) sing(note, 1, vel, 0);                /* no lyrics: "a" */
    else sing(note, ly[lsung].mora, vel, ly[lsung].flags & (LY_SUSTAIN | LY_SEPARATE));
}

static void jca_note_on(uint32_t note, uint32_t vel)
{
    if (fv.gated) { fv_note_off(&fv); fv_note_off(&fv2); }   /* a new gate: a new onset, never legato */
    syllable(note & 127u, vel < 1u ? 1u : vel > 127u ? 127u : vel);
}
static void jca_glide(uint32_t note)
{
    if (!fv.gated) return;
    note &= 127u;
    fv_note_on(&fv, (uint8_t)note, vh, vvel);
    if (hint_st() && fv2.gated) fv_note_on(&fv2, clampn((int32_t)note + hint_st()), vh, vvel);
    vnote = (uint8_t)note;
}
static void jca_next(void)
{
    if (!fv.gated) {                                      /* not singing: only the lyrics move on */
        if (p[JCA_LANG] != JCA_ANIMAL && p[JCA_LANG] != JCA_SCAT && p[JCA_LYRICS] == 0) take_unit();
        return;
    }
    syllable(vnote, vvel);                                /* gated: legato, the consonant articulated */
}
static void jca_note_off(void) { fv_note_off(&fv); fv_note_off(&fv2); }
static void jca_all_off(void) { jca_note_off(); }

/* ---------------------------------------------------------------- render */
/* MIKU-FM's mx_limit (fmband_mx.c): soft knee from 3/4 full scale, hard cap */
static void limit(int32_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++) {
        int32_t x = b[i] < -2147483647 ? -2147483647 : b[i], a = x < 0 ? -x : x;
        if (a > 24576) {
            int32_t t = a - 24576;
            a = t > 16383 ? 32767 : 24576 + t - ((t * t) >> 15);
            if (a > 32767) a = 32767;
            x = x < 0 ? -a : a;
        }
        b[i] = x;
    }
}

static void chunk(uint32_t n)
{
    uint32_t i, j;
    int32_t hm = p[JCA_HMIX] * 229, l = p[JCA_LEVEL], g = (l * l * 2 * 23170) >> 15;   /* mx_gain at the centre (-3 dB) */
    VxSet fx;
    for (j = 0; j < 2u * n; j++) buf[j] = 0;
    for (i = 0; i < n;) {
        uint32_t m = n - i;
        if (bt && fv.gated) { if (m > bt) m = bt; bt -= m; }
        fv_render(&fv, buf + 2u * i, m);
        if (fv2.gated || fv2.env || fv2.gcur || fv2.nl) {   /* the harmony voice, mixed in */
            for (j = 0; j < 2u * m; j++) vtmp[j] = 0;
            fv_render(&fv2, vtmp, m);
            for (j = 0; j < 2u * m; j++) buf[2u * i + j] += (vtmp[j] * hm) >> 15;
        }
        i += m;
        if (!bt && fv.gated && (p[JCA_LANG] == JCA_ANIMAL || p[JCA_LANG] == JCA_SCAT)) {   /* the next babbled syllable */
            uint32_t r = rnd(), mo = p[JCA_LANG] == JCA_ANIMAL ? (r % 3u ? 6u + r % 40u : 47u + r % 23u) : scat[r % 9u];
            int32_t nt = (int32_t)vnote + (p[JCA_LANG] == JCA_ANIMAL ? ajit[(r >> 8) & 7u] : 0);
            uint32_t v = mora_vowel(mo);
            fv_note_on_mora(&fv, clampn(nt), (uint8_t)mora_cons(mo), v < 5u ? (uint8_t)v : vh, vvel);
            hnote(nt > 127 ? 127 : nt, mora_cons(mo), v < 5u ? v : vh, vvel);
            mora_now = (uint8_t)mo;
            bt = babble_wait(r >> 16);
        }
    }
    fx.drv = (uint8_t)p[JCA_DRIVE]; fx.trem = (uint8_t)p[JCA_TREM]; fx.cho = (uint8_t)p[JCA_CHORUS];
    fx.dly = (uint8_t)p[JCA_DELAY]; fx.rev = (uint8_t)p[JCA_REVERB];
    fx.rsize = (uint8_t)p[JCA_RSIZE]; fx.dtime = (uint8_t)(p[JCA_DTIME] * 25); fx.dfb = (uint8_t)p[JCA_DFB];
    fx.crate = (uint8_t)p[JCA_CRATE];
    vx_render(buf, n, &fx, bpm);
    for (j = 0; j < 2u * n; j++) {
        int32_t x = ((buf[j] * g) >> 15) * 3;               /* the voice's level and +3.5 dB, as on the device */
        x = x > 131071 ? 131071 : x < -131071 ? -131071 : x;
        buf[j] = (x * 3072) >> 12;                            /* master 100: 0.75 */
    }
    limit(buf, n);
}

static void jca_render(uint32_t n, float *l, float *r)
{
    uint32_t done = 0, i;
    while (done < n) {
        uint32_t m = n - done > CHUNK ? CHUNK : n - done;
        chunk(m);
        for (i = 0; i < m; i++) {
            l[done + i] = (float)buf[2u * i] * (1.f / 32768.f);
            r[done + i] = (float)buf[2u * i + 1u] * (1.f / 32768.f);
        }
        done += m;
    }
}

/* ---------------------------------------------------------------- parameters */
static void jca_set(uint32_t j, int32_t v)
{
    if (j >= JCA_NP) return;
    v = v < LO[j] ? LO[j] : v > HI[j] ? HI[j] : v;
    if (j == JCA_FIXED && v < 1) v = 1;
    if (p[j] == v) return;
    p[j] = v;
    apply();
}
static int32_t jca_getp(uint32_t j) { return j < JCA_NP ? p[j] : 0; }
static const char *jca_label(uint32_t j) { return j < JCA_NP ? LABEL[j] : ""; }
static int32_t jca_min(uint32_t j) { return j < JCA_NP ? (j == JCA_FIXED ? 1 : LO[j]) : 0; }
static int32_t jca_max(uint32_t j) { return j < JCA_NP ? HI[j] : 0; }

static void put(char *o, uint32_t n, const char *s)
{
    uint32_t i = 0;
    if (!n) return;
    for (; s[i] && i + 1u < n; i++) o[i] = s[i];
    o[i] = 0;
}
static void puti(char *o, uint32_t n, const char *pre, int32_t v, const char *post)
{
    char t[40], d[12];
    uint32_t k = 0, i, nd = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    for (i = 0; pre[i] && k < 20u; i++) t[k++] = pre[i];
    if (v < 0) t[k++] = '-';
    do { d[nd++] = (char)('0' + u % 10u); u /= 10u; } while (u && nd < 11u);
    while (nd) t[k++] = d[--nd];
    for (i = 0; post[i] && k < 38u; i++) t[k++] = post[i];
    t[k] = 0;
    put(o, n, t);
}
static void putdec(char *o, uint32_t n, int32_t tenths, const char *post)   /* 55 -> "5.5" + post */
{
    char t[24];
    puti(t, sizeof t, "", tenths / 10, "");
    {
        uint32_t k = 0;
        while (t[k]) k++;
        t[k++] = '.';
        t[k++] = (char)('0' + tenths % 10);
        t[k] = 0;
        for (uint32_t i = 0; post[i] && k < 22u; i++) t[k++] = post[i];
        t[k] = 0;
    }
    put(o, n, t);
}
static void jca_format(uint32_t j, int32_t v, char *o, uint32_t n)
{
    switch (j) {
    case JCA_GLIDE: puti(o, n, "", v * 5, " ms"); break;
    case JCA_ATK: case JCA_REL: puti(o, n, "", v * v / 20, " ms"); break;
    case JCA_SCOOP: case JCA_VIB: puti(o, n, "", v, " ct"); break;
    case JCA_TENSE: case JCA_FORMANT: case JCA_VSPD: case JCA_CLEN: case JCA_CLEV:
        if (j == JCA_VSPD || j == JCA_CLEN || j == JCA_CLEV) puti(o, n, "", j == JCA_CLEV ? v * 2 : (25 + v * 3 / 2), "%");
        else puti(o, n, v > 50 ? "+" : "", v - 50, "");
        break;
    case JCA_LEVEL: puti(o, n, "", v * 100 / 127, "%"); break;
    case JCA_LANG: put(o, n, LANGS[v & 3]); break;
    case JCA_CHR: case JCA_HCHR: put(o, n, CHRS[v % 5]); break;
    case JCA_HINT: put(o, n, HINTS[v < 0 ? 0 : v > 6 ? 6 : v]); break;
    case JCA_LYRICS: put(o, n, v ? "FIXED" : "FOLLOW"); break;
    case JCA_FIXED: put(o, n, mora_str((uint32_t)v)); break;
    case JCA_VRATE: putdec(o, n, 30 + v / 2, " Hz"); break;
    case JCA_VDEL: puti(o, n, "", v * v / 10, " ms"); break;
    case JCA_DTIME: put(o, n, DTIMES[v < 0 ? 0 : v > 4 ? 4 : v]); break;
    case JCA_CRATE: putdec(o, n, 1 + v * 39 / 100, " Hz"); break;
    case JCA_HDET: puti(o, n, "", v * 30 / 100, " ct"); break;
    default: puti(o, n, "", v, "%"); break;
    }
}

/* ---------------------------------------------------------------- lyrics, presets, state */
static int32_t jca_set_lyrics(const char *text)
{
    LyUnit tmp[CAP];
    uint32_t c = 0, at = 0;
    int e = ly_compile(text, 1, tmp, CAP, &c, &at);
    if (e != LY_OK) return (int32_t)at;
    for (uint32_t i = 0; i < c; i++) ly[i] = tmp[i];
    nly = c;
    lpos = 0;
    lsung = -1;
    return -1;
}
static uint32_t jca_lyric_count(void) { return nly; }
static const char *jca_lyric_text(uint32_t i)
{
    if (i >= nly || (ly[i].flags & LY_REST)) return "";
    if (ly[i].flags & LY_SUSTAIN) return "_";
    return mora_str(ly[i].mora);
}
static uint32_t jca_lyric_flags(uint32_t i) { return i < nly ? ly[i].flags : 0u; }
static uint32_t jca_lyric_pos(void) { return lpos; }
static void jca_lyric_seek(uint32_t i) { lpos = nly ? i % nly : 0u; }
static int32_t jca_lyric_sung(void) { return fv.gated || fv_level(&fv) > 8u ? lsung : -1; }
static uint32_t jca_mora_count(void) { return MORA_N; }
static const char *jca_mora_name(uint32_t i) { return mora_str(i); }
static uint32_t jca_sung_mora(void) { return mora_now; }
static uint32_t jca_mouth(void) { return fv_mouth(&fv); }
static uint32_t jca_level(void) { return fv_level(&fv); }
static uint32_t jca_gated(void) { return fv.gated; }
static void jca_set_bpm(uint32_t b) { bpm = b < 20u ? 20u : b > 300u ? 300u : b; }
static uint32_t jca_npresets(void) { return (uint32_t)NPRE; }
static const char *jca_preset_name(uint32_t i) { return PRE[i % NPRE].name; }
static const char *jca_preset_lyrics(uint32_t i) { return PRE[i % NPRE].lyrics; }
static void jca_load_preset(uint32_t i)
{
    const Preset *q = &PRE[i % NPRE];
    uint32_t j;
    int32_t keepl = p[JCA_LYRICS], keepf = p[JCA_FIXED], keepv = p[JCA_LEVEL];
    for (j = 0; j < JCA_NP; j++) p[j] = DEF[j];
    p[JCA_LYRICS] = keepl;                                /* how the lyrics are sung and the level stay */
    p[JCA_FIXED] = keepf;
    p[JCA_LEVEL] = keepv;
    for (j = 0; j + 1u < sizeof q->set && q->set[j] >= 0; j += 2u) p[(uint32_t)q->set[j]] = q->set[j + 1u];
    apply();
    jca_set_lyrics(q->lyrics);
}

static uint32_t jca_sample_rate(void) { return FS; }
static void jca_init(uint32_t seed)
{
    uint32_t j;
    fv_init(&fv);
    fv_init(&fv2);
    vx_init();
    fv2.rng = 0x2545F491u;                                /* the harmony voice drifts on its own */
    fv.vib_rate = 55;
    fv.vib_delay = 200;
    rng = seed ? seed : 0x6D2B79F5u;
    bt = 0;
    vnote = 60;
    vvel = 110;
    vh = 0;
    mora_now = 0;
    for (j = 0; j < JCA_NP; j++) p[j] = DEF[j];
    apply();
    jca_set_lyrics(PRE[0].lyrics);
}

static const jca_api jca_table = {
    JCA_API_VERSION, jca_sample_rate, jca_init, jca_render, jca_set, jca_getp, jca_label, jca_min, jca_max, jca_format,
    jca_note_on, jca_glide, jca_next, jca_note_off, jca_all_off, jca_set_bpm,
    jca_set_lyrics, jca_lyric_count, jca_lyric_text, jca_lyric_flags, jca_lyric_pos, jca_lyric_seek, jca_lyric_sung,
    jca_mora_count, jca_mora_name, jca_sung_mora, jca_mouth, jca_level, jca_gated,
    jca_npresets, jca_preset_name, jca_preset_lyrics, jca_load_preset,
};

JCA_EXPORT const jca_api *jca_get(void) { return &jca_table; }
