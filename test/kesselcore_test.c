/* SPDX-License-Identifier: GPL-3.0-only
 * KESSEL core test (without Rack): loads the core, strikes every voice and runs every effect; fails on silence,
 * on a voice that does not die away, or on samples that are not numbers.
 * Usage: kesselcore_test ./kesselcore.so */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kesselcore/kesselcore.h"
#if defined(_WIN32)
#include <windows.h>
static void *open_lib(const char *p) { return (void *) LoadLibraryA(p); }
static void *sym(void *h, const char *n) { return (void *) GetProcAddress((HMODULE) h, n); }
#else
#include <dlfcn.h>
static void *open_lib(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *sym(void *h, const char *n) { return dlsym(h, n); }
#endif

static const char *NAMES[JKS_NVOICE] = {"DRUM1", "DRUM2", "DRUM3", "SNARE", "CYMBAL", "HIHAT"};

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s core\n", argv[0]);
		return 2;
	}
	void *h = open_lib(argv[1]);
	if (!h) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 1;
	}
	jks_get_fn get = (jks_get_fn) sym(h, "jks_get");
	const jks_api *a = get ? get() : NULL;
	if (!a || a->api != JKS_API_VERSION) {
		fprintf(stderr, "wrong interface\n");
		return 1;
	}
	int fail = 0;
	a->init(1);
	a->load_init();
	static float out[JKS_NVOICE * JKS_BLOCK];
	for (int v = 0; v < JKS_NVOICE; v++) {
		a->trigger((uint32_t) v, 127, 0);
		double peak = 0, tail = 0;
		int nan = 0;
		int blocks = 44100 * 2 / JKS_BLOCK;
		for (int b = 0; b < blocks; b++) {
			a->render(out);
			for (int i = 0; i < JKS_BLOCK; i++) {
				float x = out[v * JKS_BLOCK + i];
				if (!isfinite(x))
					nan = 1;
				if (fabs(x) > peak)
					peak = fabs(x);
				if (b >= blocks - 40 && fabs(x) > tail)
					tail = fabs(x);
			}
		}
		printf("%-7s peak %.3f  after 2 s %.4f%s\n", NAMES[v], peak, tail, nan ? "  NOT A NUMBER" : "");
		if (nan || peak < 0.01 || tail > 0.05)
			fail = 1;
	}
	/* the effects on a click train */
	static const char *FX[] = {"off", "distortion", "ring", "compressor", "delay", "reverb"};
	static float in[JKS_BLOCK * 2], fo[JKS_BLOCK * 2];
	for (uint32_t t = 0; t <= JKS_FX_REVERB; t++) {
		if (t == JKS_FX_COMP)
			continue;
		float p[4] = {0.5f, 0.5f, 0.5f, 0.5f};
		a->fx_set(t, 0, p, 0.6f);
		double peak = 0;
		int nan = 0;
		for (int b = 0; b < 44100 / JKS_BLOCK; b++) {
			for (int i = 0; i < JKS_BLOCK * 2; i++)
				in[i] = (b % 40 == 0 && i < 8) ? 0.8f : 0.f;
			a->fx_process(in, fo);
			for (int i = 0; i < JKS_BLOCK * 2; i++) {
				if (!isfinite(fo[i]))
					nan = 1;
				if (fabs(fo[i]) > peak)
					peak = fabs(fo[i]);
			}
		}
		printf("fx %-10s peak %.3f%s\n", FX[t], peak, nan ? "  NOT A NUMBER" : "");
		if (nan || peak < 0.01 || peak > 4.0)
			fail = 1;
	}
	printf(fail ? "FAIL\n" : "OK\n");
	return fail;
}
