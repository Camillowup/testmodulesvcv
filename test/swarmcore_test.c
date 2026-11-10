/* Host test of the SWARM core: presets, sound, the air (VOICES BUSY MOVE NEAR), ARRIVE, NEW, the drawing. */
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include "../swarmcore/swarmcore.h"
static float L[44100], R[44100];
static double rms(uint32_t n) { double s = 0; for (uint32_t i = 0; i < n; i++) s += L[i] * L[i] + R[i] * R[i]; return sqrt(s / (2 * n)); }
static const jsw_api *a;
static double run(double sec) { uint32_t n = (uint32_t)(sec * 44100), blk = 64; double s = 0; uint32_t k = 0;
    while (k < n) { a->render(blk, L, R); for (uint32_t i = 0; i < blk; i++) s += L[i] * L[i] + R[i] * R[i]; k += blk; }
    return sqrt(s / (2.0 * n)); }
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./swarmcore.so";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    jsw_get_fn get = (jsw_get_fn)(void *)GetProcAddress(h, "jsw_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    jsw_get_fn get = (jsw_get_fn)dlsym(h, "jsw_get");
#endif
    if (!get) { printf("no jsw_get\n"); return 1; }
    a = get();
    if (!a || a->api != JSW_API_VERSION) return 1;
    int fails = 0;
    a->init(1234);
    printf("rate %u, presets %u, seed %u\n", a->sample_rate(), a->npresets(), a->seed());
    for (uint32_t j = 0; j < JSW_NP; j++) { char b[24]; a->format(j, a->get(j), b, sizeof b); printf("j%u %s [%d..%d] = %s\n", j, a->label(j), a->min(j), a->max(j), b); }
    for (uint32_t i = 0; i < a->npresets(); i++) {
        a->load_preset(i);
        a->air(4, 0.5f, 0.3f, 0.5f);
        double r1 = run(4.0);
        printf("%-12s rms %.4f devices %u near %u\n", a->preset_name(i), r1, a->devices(), a->closeness());
        if (!(r1 > 0.003)) fails++;
    }
    a->load_preset(0);
    a->air(8, 1.f, 1.f, 1.f);
    printf("8 busy near: %.4f devices %u near %u\n", run(4.0), a->devices(), a->closeness());
    a->air(1, 0.f, 0.f, 0.f);
    printf("1 calm far: %.4f devices %u near %u\n", run(8.0), a->devices(), a->closeness());
    a->arrive();
    printf("arrive: %.4f\n", run(2.0));
    a->renew(99);
    printf("renew: %.4f seed %u\n", run(3.0), a->seed());
    a->set_key(12);
    a->set_clock(90, 0);
    printf("key +12: %.4f\n", run(2.0));
    const uint16_t *v = a->vis(232, 150);
    uint32_t lit = 0;
    for (uint32_t i = 0; i < 232 * 150; i++) lit += v[i] != v[0];
    printf("vis: %u px differ from the corner\n", lit);
    if (lit < 50) fails++;
    printf(fails ? "FAIL %d\n" : "OK\n", fails);
    return fails;
}
