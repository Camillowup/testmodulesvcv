/* Host test of the SAURUS core: every preset calls, the KEYS kit, the call curves, the parameters. */
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include "../sauruscore/sauruscore.h"
static float L[44100], R[44100];
static double peak;
static double run(const jsa_api *a, uint32_t n)
{
    double s = 0;
    a->render(n, L, R);
    for (uint32_t i = 0; i < n; i++) {
        s += L[i] * L[i] + R[i] * R[i];
        if (fabs(L[i]) > peak) peak = fabs(L[i]);
    }
    return sqrt(s / (2 * n));
}
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./sauruscore.so";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    jsa_get_fn get = (jsa_get_fn)(void *)GetProcAddress(h, "jsa_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    jsa_get_fn get = (jsa_get_fn)dlsym(h, "jsa_get");
#endif
    if (!get) { printf("no jsa_get\n"); return 1; }
    const jsa_api *a = get();
    if (!a || a->api != JSA_API_VERSION) return 1;
    a->init(1);
    int fails = 0;
    char b[32];
    printf("presets %u, sr %u\n", a->npresets(), a->sample_rate());
    if (a->npresets() != 21) fails++;
    for (uint32_t i = 0; i < a->npresets(); i++) {
        a->load_preset(i);
        run(a, 4410);
        uint32_t note = a->get(JSA_CALL) == JSA_CALL_KEYS ? 38u : 48u;
        peak = 0;
        a->note_on(note, 100);
        double on = run(a, 22050);
        a->note_off(note);
        double tail = run(a, 44100);
        printf("%-13s on %.3f peak %.2f tail %.4f |", a->preset_name(i), on, peak, tail);
        for (uint32_t j = JSA_CALL; j <= JSA_WILD; j++) { a->format(j, a->get(j), b, sizeof b); printf(" %s=%s", a->label(j), b); }
        printf("\n");
        if (!(on > 0.003)) fails++;
        a->all_off();
        run(a, 44100);
    }
    int32_t c[5];
    for (uint32_t k = 0; k < JSA_NCALL; k++) {
        int has = a->call_curve(k, 0, c);
        printf("%s%s", a->call_name(k), has ? "" : "(-)");
        printf(k + 1 < JSA_NCALL ? " " : "\n");
        if (!has != (k == JSA_CALL_KEYS)) fails++;
    }
    for (uint32_t j = 0; j < JSA_NP; j++) printf("%s[%d..%d] ", a->label(j), a->min(j), a->max(j));
    printf("\n");
    a->set_bpm(90);
    printf(fails ? "FAIL %d\n" : "OK\n", fails);
    return fails;
}
