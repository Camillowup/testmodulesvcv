/* Host test of the LIED core: eSpeak NG starts on res/espeak-ng-data, every language and voice of the module speaks,
 * the pitch is the constant one asked for, the phonemes come with their places and the vowels are found. */
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include "../liedcore/liedcore.h"
static float buf[22050 * 20];
static lic_ph ph[512];
/* the strongest period between 60 and 600 Hz in the middle of [a, b) */
static double pitch(int a, int b, int sr)
{
    int m = (a + b) / 2, n = 1024;
    if (b - a < n) n = b - a;
    int s = m - n / 2;
    double best = 0, bl = 0;
    for (int l = sr / 600; l < sr / 60; l++) {
        double c = 0;
        for (int i = 0; i + l < n; i++) c += buf[s + i] * buf[s + i + l];
        if (c > best) { best = c; bl = l; }
    }
    return bl > 0 ? sr / bl : 0;
}
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./liedcore.so";
    const char *data = argc > 2 ? argv[2] : "res/espeak-ng-data";
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LoadLibrary failed: %lu\n", (unsigned long)GetLastError()); return 1; }
    lic_get_fn get = (lic_get_fn)(void *)GetProcAddress(h, "lic_get");
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    lic_get_fn get = (lic_get_fn)dlsym(h, "lic_get");
#endif
    if (!get) { printf("no lic_get\n"); return 1; }
    const lic_api *a = get();
    if (!a || a->api != LIC_API_VERSION) return 1;
    int sr = a->init(data);
    printf("init %d %s\n", sr, a->error());
    if (sr <= 0) return 1;
    int fails = 0;
    const char *langs[][2] = {{"de", "Wir singen durch die Nacht"}, {"en", "We sing all night"}, {"en-us", "Hello world"},
                              {"fr", "Nous chantons la nuit"}, {"es", "Cantamos toda la noche"}, {"it", "Cantiamo tutta la notte"},
                              {"nl", "Wij zingen de hele nacht"}, {"pt", "Cantamos a noite toda"}, {"sv", "Vi sjunger hela natten"},
                              {"pl", "Śpiewamy całą noc"}, {"fi", "Laulamme koko yön"}, {"la", "Cantamus tota nocte"}};
    for (int k = 0; k < 12; k++) {
        int np = 0, nv = 0;
        int n = a->render(langs[k][0], langs[k][1], 130, 150, buf, sizeof buf / sizeof buf[0], ph, 512, &np);
        for (int i = 0; i < np; i++) nv += ph[i].vowel;
        printf("%-6s %6d samples %3d phonemes %2d vowels |", langs[k][0], n, np, nv);
        for (int i = 0; i < np && i < 14; i++) printf(" %s%s", ph[i].name, ph[i].vowel ? "*" : "");
        printf("\n");
        if (n < sr / 4 || nv < 3) fails++;
    }
    const char *vars[] = {"klatt", "klatt2", "klatt3", "klatt4", "klatt5", "klatt6", "m1", "m3", "m6", "f1", "f3", "f5", "croak", "whisper"};
    for (int k = 0; k < 14; k++) {
        char v[32];
        snprintf(v, sizeof v, "de+%s", vars[k]);
        int np = 0;
        int n = a->render(v, "Nacht", 130, 150, buf, sizeof buf / sizeof buf[0], ph, 512, &np);
        int va = -1, vb = n;
        for (int i = 0; i < np; i++)
            if (ph[i].vowel) { va = ph[i].pos; vb = i + 1 < np ? ph[i + 1].pos : n; break; }
        double f = va >= 0 ? pitch(va, vb, sr) : 0;
        double pk = 0;
        for (int i = 0; i < n; i++) if (fabs(buf[i]) > pk) pk = fabs(buf[i]);
        printf("%-10s %6d samples, vowel %d..%d, pitch %.1f Hz, peak %.2f\n", v, n, va, vb, f, pk);
        if (n < sr / 8 || va < 0 || pk < 0.02) fails++;
        if (k < 12 && fabs(f - 150) > 12) fails++;
    }
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
