/* Host test of the KURBEL core: presets of both engines, sound, the crank (speed, stroke), RING. */
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include "../kurbelcore/kurbelcore.h"
static float L[44100], R[44100];
static double rms(uint32_t n) { double s = 0; for (uint32_t i = 0; i < n; i++) s += L[i] * L[i] + R[i] * R[i]; return sqrt(s / (2 * n)); }
/* n frames in blocks of 32, the crank fed every block */
static double run(const jku_api *a, uint32_t n, float crank)
{
    for (uint32_t i = 0; i < n; i += 32) {
        if (crank >= 0.f) a->crank(crank, 32.f / 44100.f);
        a->render(32, L + i, R + i);
    }
    return rms(n);
}
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./kurbelcore.so";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    jku_get_fn get = (jku_get_fn)(void *)GetProcAddress(h, "jku_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    jku_get_fn get = (jku_get_fn)dlsym(h, "jku_get");
#endif
    if (!get) { printf("no jku_get\n"); return 1; }
    const jku_api *a = get();
    if (!a || a->api != JKU_API_VERSION) return 1;
    a->init(1);
    int fails = 0;
    char b[32];
    printf("presets %u\n", a->npresets());
    for (uint32_t i = 0; i < a->npresets(); i++) {
        a->load_preset(i);
        run(a, 4410, -1);
        a->note_on(62, 100);
        double on = run(a, 22050, -1);
        a->note_off(62);
        double tail = run(a, 22050, -1);
        printf("%-13s %s on %.3f tail %.3f ring %d |", a->preset_name(i), a->engine() ? "REED " : "WHEEL", on, tail, a->get(JKU_RING));
        for (uint32_t j = JKU_E0; j <= JKU_E7; j++) { a->format(j, a->get(j), b, sizeof b); printf(" %s=%s", a->label(j), b); }
        printf("\n");
        if (!(on > 0.005)) fails++;
        a->all_off();
    }
    /* the crank: COUP CRANK, a note held; still, turning, faster, a ruck */
    a->load_preset(0);
    a->set(JKU_RING, 0);
    a->set(JKU_E5, JKU_COUP_CRANK);
    a->note_on(62, 100);
    double c0 = run(a, 22050, 0.f), c1 = run(a, 22050, 15.f), c2 = run(a, 22050, 40.f);
    uint32_t s0 = a->strokes();
    run(a, 4410, 20.f);
    run(a, 2205, 60.f);                              /* a ruck: 20 -> 60 clicks/s */
    uint32_t s1 = a->strokes();
    printf("crank: still %.4f, 15/s %.3f, 40/s %.3f; strokes %u -> %u\n", c0, c1, c2, s0, s1);
    if (!(c0 < c1 && c1 < c2 + 0.05 && s1 != s0)) fails++;
    a->all_off();
    /* temperaments */
    a->set(JKU_E5, 2);
    for (int t = 0; t < 4; t++) { a->set(JKU_TEMP, t); a->format(JKU_TEMP, t, b, sizeof b); printf("TEMP %s ", b); }
    printf("\nROOT %s..%s\n", a->label(JKU_ROOT), a->label(JKU_RING));
    printf(fails ? "FAIL %d\n" : "OK\n", fails);
    return fails;
}
