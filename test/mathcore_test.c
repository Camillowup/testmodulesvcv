/* Host test of the MATH core: presets, parameters, sound in KEYS and THRU, MOTION SYNC. */
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../mathcore/mathcore.h"
static float L[44100], R[44100];
static double rms(uint32_t n) { double s = 0; for (uint32_t i = 0; i < n; i++) s += L[i] * L[i] + R[i] * R[i]; return sqrt(s / (2 * n)); }
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./mathcore.so";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    jma_get_fn get = (jma_get_fn)(void *)GetProcAddress(h, "jma_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    jma_get_fn get = (jma_get_fn)dlsym(h, "jma_get");
#endif
    if (!get) { printf("no jma_get\n"); return 1; }
    const jma_api *a = get();
    if (!a || a->api != JMA_API_VERSION) return 1;
    a->init(1234);
    printf("rate %u, presets %u\n", a->sample_rate(), a->npresets());
    for (uint32_t i = 0; i < a->npresets(); i++) printf("  %2u %s\n", i, a->preset_name(i));
    int fails = 0;
    for (uint32_t i = 0; i < a->npresets(); i++) {
        char b[32];
        a->load_preset(i);
        a->render(4410, L, R);
        a->note_on(60, 100);
        a->render(22050, L, R);
        double on = rms(22050);
        a->note_off(60);
        a->render(44100, L, R);
        double off = rms(4410);
        printf("%-12s on %.3f  tail %.4f |", a->preset_name(i), on, off);
        for (uint32_t j = JMA_CHIP; j <= JMA_E7; j++) { a->format(j, a->get(j), b, sizeof b); printf(" %s=%s", a->label(j), b); }
        printf("\n");
        if (!(on > 0.005)) fails++;
    }
    /* labels and ranges */
    for (uint32_t j = 0; j < JMA_NP; j++) printf("j%u %s [%d..%d]\n", j, a->label(j), a->min(j), a->max(j));
    /* THRU: one held note, pitch moved without a new note */
    a->load_preset(0);
    a->note_on(60, 100);
    a->render(22050, L, R);
    double t1 = rms(22050);
    a->set_pitch(72 * 16);
    a->render(22050, L, R);
    double t2 = rms(22050);
    printf("THRU held: %.3f, after pitch +12: %.3f, voices %u\n", t1, t2, a->active_voices());
    if (!(t1 > 0.005 && t2 > 0.005)) fails++;
    a->all_off();
    /* MOTION SYNC */
    a->set(JMA_E4, 200);
    a->note_on(60, 100);
    for (uint32_t q = 0; q < 8; q++) { a->set_clock(120, q, 0); a->render(22050, L, R); printf("sync q%u %.3f\n", q, rms(22050)); }
    a->all_off();
    const uint16_t *v = a->vis(232, 150);
    uint32_t lit = 0;
    for (uint32_t i = 0; i < 232 * 150; i++) lit += v[i] != v[0];
    printf("vis: %u px differ from the corner\n", lit);
    printf(fails ? "FAIL %d\n" : "OK\n", fails);
    return fails;
}
