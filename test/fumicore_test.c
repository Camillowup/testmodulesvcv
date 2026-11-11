/* Host test of the FUMI core: every patch sounds and rings out, the pitch helpers (chromatic with the tuning, snapped to
 * the scale), 本数 moves 三, the trill and the ornaments, the bend, two instances' worth of state (a second init). */
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include "../fumicore/fumicore.h"
static float L[44100], R[44100];
static double rms(uint32_t n) { double s = 0; for (uint32_t i = 0; i < n; i++) s += L[i] * L[i] + R[i] * R[i]; return sqrt(s / (2 * n)); }
static double peak(uint32_t n) { double p = 0; for (uint32_t i = 0; i < n; i++) { double a = fabs(L[i]) > fabs(R[i]) ? fabs(L[i]) : fabs(R[i]); if (a > p) p = a; } return p; }
static double run(const jfu_api *a, uint32_t n) { a->render(n, L, R); return rms(n); }
static int approx(float a, float b, float e) { return fabsf(a - b) <= e; }
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./fumicore.so";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    jfu_get_fn get = (jfu_get_fn)(void *)GetProcAddress(h, "jfu_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    jfu_get_fn get = (jfu_get_fn)dlsym(h, "jfu_get");
#endif
    if (!get) { printf("no jfu_get\n"); return 1; }
    const jfu_api *a = get();
    if (!a || a->api != JFU_API_VERSION) return 1;
    a->init(1);
    int fails = 0;
    char b[32];
    printf("patches %u\n", a->npatches());
    if (a->npatches() != 6) fails++;
    double maxpk = 0;
    for (uint32_t i = 0; i < a->npatches(); i++) {
        a->set(JFU_VOICE, (int32_t)i);
        run(a, 4410);
        a->note(0, 1, a->pitch(57.f, 0), 100);
        double on = run(a, 22050), pk = peak(22050);
        a->note(0, 0, 0.f, 0);
        double tail = run(a, 22050);
        run(a, 44100);
        double after = run(a, 22050);
        if (pk > maxpk) maxpk = pk;
        printf("%-11s fam %u  on %.3f peak %.3f tail %.4f after %.5f\n", a->patch_name(i), a->patch_family(i), on, pk, tail, after);
        if (!(on > 0.003)) fails++;
        if (!(after < on * 0.05)) fails++;
    }
    printf("loudest peak %.3f\n", maxpk);
    /* the pitch: 1本 (三 = A3 = MIDI 57), SUIKO: fa is 20.8 cents low; equal: none; snapped: G#3 goes to A3 or ... */
    a->set(JFU_VOICE, 0);
    a->set(JFU_HON, 4);
    a->set(JFU_SCALE, 0);
    a->set(JFU_TUNING, 1);
    printf("san %d  A3 %.1f  A#3(fa) %.1f  white5 %.1f  white6 %.1f\n", a->san_midi(), a->pitch(57.f, 0), a->pitch(58.f, 0),
           a->white_cents(5), a->white_cents(6));
    if (a->san_midi() != 57) fails++;
    if (!approx(a->pitch(57.f, 0), -1200.f, 0.01f)) fails++;
    if (!approx(a->pitch(58.f, 0), -1100.f - 20.8f, 0.5f)) fails++;
    if (!approx(a->white_cents(6), a->pitch(58.f, 0), 0.01f)) fails++;
    /* snapped: IN is mi fa la ti do = 0 1 5 7 8: C4 (60, 3 above A3) goes to fa (58) or la (62)? nearest: 58 (2) vs 62 (2): either; 59 -> 58 */
    float s59 = a->pitch(59.f, 1), s61 = a->pitch(61.f, 1);
    printf("snap 59 -> %.1f, 61 -> %.1f\n", s59, s61);
    if (!approx(s59, a->pitch(58.f, 0), 0.01f) || !approx(s61, a->pitch(62.f, 0), 0.01f)) fails++;
    a->set(JFU_TUNING, 0);
    if (!approx(a->pitch(58.f, 0), -1100.f, 0.01f)) fails++;
    a->set(JFU_HON, 6);
    if (a->san_midi() != 59) fails++;
    a->set(JFU_HON, 4);
    /* the formats */
    a->format(JFU_YOIN, a->get(JFU_YOIN), b, sizeof b); printf("yoin %s", b);
    a->format(JFU_OROSHI, a->get(JFU_OROSHI), b, sizeof b); printf("  oroshi %s", b);
    a->format(JFU_HON, a->get(JFU_HON), b, sizeof b); printf("  hon %s\n", b);
    /* the trill (latched) re-plucks: more onsets than a plain note */
    a->set(JFU_VOICE, 4);                                 /* Hyoshigi: a dry crack, so the strokes show */
    a->note(0, 1, a->pitch(57.f, 0), 100);
    run(a, 2205);
    double plain = run(a, 22050);
    a->set(JFU_TRILL_ON, 1);
    run(a, 2205);
    double trill = run(a, 22050);
    a->set(JFU_TRILL_ON, 0);
    a->note(0, 0, 0.f, 0);
    run(a, 44100);
    printf("hyoshigi held %.4f, trilled %.4f\n", plain, trill);
    if (!(trill > plain * 2.0)) fails++;
    /* the ornaments and the bend do not break anything */
    a->set(JFU_VOICE, 0);
    a->note(1, 1, a->pitch(62.f, 0), 100);
    for (uint32_t o = 0; o < JFU_ORN_N; o++) { a->ornament(o, 1); run(a, 4410); a->ornament(o, 0); run(a, 2205); }
    a->bend(1, 0, 1); run(a, 4410); a->bend(0, 0, 1); a->bend(0, 1, 1); run(a, 4410); a->bend(0, 0, 1);
    a->note(1, 0, 0.f, 0);
    run(a, 4410);
    a->note(1, 1, a->pitch(62.f, 0), 100);
    double orn = run(a, 4410);
    a->note(1, 0, 0.f, 0);
    printf("a note after the ornaments and the bend %.3f\n", orn);
    if (!(orn > 0.001) || orn != orn) fails++;
    /* mono, poly: two channels at once */
    a->set(JFU_MONO, 0);
    a->note(2, 1, a->pitch(57.f, 0), 100);
    a->note(3, 1, a->pitch(64.f, 0), 100);
    run(a, 4410);
    printf("voices (poly, 2 held) %u\n", a->active_voices());
    if (a->active_voices() < 2) fails++;
    a->panic();
    run(a, 44100);
    a->init(2);
    if (a->get(JFU_VOICE) != 0 || a->san_midi() != 57) fails++;
    printf(fails ? "FAIL %d\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
