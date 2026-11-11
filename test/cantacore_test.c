/* Host test of the CANTA core: every preset's lyrics, sound in all four languages, lyrics stepping, harmony, FIXED. */
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../cantacore/cantacore.h"
static float L[44100], R[44100];
static double run(const jca_api *a, uint32_t n)
{
    double s = 0;
    for (uint32_t i = 0; i < n; i += 32) a->render(32, L + i, R + i);
    for (uint32_t i = 0; i < n; i++) {
        if (!(L[i] == L[i]) || fabsf(L[i]) > 1.f || fabsf(R[i]) > 1.f) return -1.0;
        s += L[i] * L[i] + R[i] * R[i];
    }
    return sqrt(s / (2 * n));
}
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./cantacore.so";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    jca_get_fn get = (jca_get_fn)(void *)GetProcAddress(h, "jca_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    jca_get_fn get = (jca_get_fn)dlsym(h, "jca_get");
#endif
    if (!get) { printf("no jca_get\n"); return 1; }
    const jca_api *a = get();
    if (!a || a->api != JCA_API_VERSION) return 1;
    a->init(1);
    int fails = 0;
    char b[32];
    printf("presets %u, morae %u\n", a->npresets(), a->mora_count());
    for (uint32_t i = 0; i < a->npresets(); i++) {
        int32_t e = a->set_lyrics(a->preset_lyrics(i));
        a->load_preset(i);
        a->note_on(64, 100);
        double on = run(a, 22050);
        a->glide(67);
        double gl = run(a, 4410);
        a->note_off();
        double off = run(a, 44100);
        a->format(JCA_LANG, a->get(JCA_LANG), b, sizeof b);
        printf("%-9s %-8s lyrics %2u%s  on %.3f glide %.3f tail %.4f\n", a->preset_name(i), b, a->lyric_count(),
               e >= 0 ? " (BAD)" : "", on, gl, off);
        if (e >= 0 || on < 0.01 || gl < 0.005 || on > 0.9 || off < 0) fails++;
    }
    /* lyrics: each gate the next syllable, rests skipped, wrap */
    a->load_preset(0);
    if (a->set_lyrics("ka-ra / o-ke |") != -1) fails++;
    const char *want[5] = {"ka", "ra", "o", "ke", "ka"};
    for (int k = 0; k < 5; k++) {
        a->note_on(60, 100);
        run(a, 2205);
        int32_t s = a->lyric_sung();
        const char *got = s >= 0 ? a->lyric_text((uint32_t)s) : "?";
        if (strcmp(got, want[k])) { printf("syllable %d: %s, want %s\n", k, got, want[k]); fails++; }
        a->note_off();
        run(a, 2205);
    }
    if (a->set_lyrics("ka-xq") < 0) { printf("bad lyrics taken\n"); fails++; }
    if (a->lyric_count() != 6) { printf("bad lyrics changed the old ones\n"); fails++; }
    /* NEXT under a held gate moves on */
    a->lyric_seek(0);
    a->note_on(60, 100);
    run(a, 2205);
    a->next();
    run(a, 2205);
    if (strcmp(a->lyric_text((uint32_t)a->lyric_sung()), "ra")) { printf("NEXT did not move on\n"); fails++; }
    a->note_off();
    run(a, 22050);
    /* FIXED: always the same mora */
    a->set(JCA_LYRICS, 1);
    a->set(JCA_FIXED, 39);
    a->note_on(60, 100);
    run(a, 4410);
    if (a->sung_mora() != 39) { printf("FIXED sang %u\n", a->sung_mora()); fails++; }
    a->note_off();
    run(a, 22050);
    a->set(JCA_LYRICS, 0);
    /* ANIMAL babbles while held: several morae */
    a->set(JCA_LANG, JCA_ANIMAL);
    a->note_on(62, 100);
    uint32_t seen = 0, last = 999;
    for (int k = 0; k < 40; k++) { run(a, 1102); if (a->sung_mora() != last) { seen++; last = a->sung_mora(); } }
    if (seen < 4) { printf("ANIMAL babbled %u morae\n", seen); fails++; }
    a->note_off();
    run(a, 22050);
    /* harmony adds level */
    a->set(JCA_LANG, JCA_VOWEL);
    a->set(JCA_LYRICS, 1);
    a->set(JCA_LEVEL, 40);
    a->set(JCA_HINT, 0);
    a->note_on(60, 100);
    double solo = run(a, 22050);
    a->note_off();
    run(a, 44100);
    a->set(JCA_HINT, 5);
    a->set(JCA_HMIX, 100);
    a->note_on(60, 100);
    double duo = run(a, 22050);
    a->note_off();
    printf("harmony: solo %.3f, +7 %.3f\n", solo, duo);
    if (duo <= solo * 1.05) fails++;
    a->format(JCA_ATK, 30, b, sizeof b);
    printf("ATK 30 = %s; mouth %u\n", b, a->mouth());
    printf(fails ? "FAILED %d\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
