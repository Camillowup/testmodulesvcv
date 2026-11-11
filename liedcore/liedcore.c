/* SPDX-License-Identifier: GPL-3.0-or-later
 * LIED core: see liedcore.h. eSpeak NG 1.52.0 (espeak.tar.gz, GPL-3.0-or-later) in synchronous mode: the samples and
 * the phoneme events arrive in the callback while espeak_Synth() runs.
 */
#include "liedcore.h"
#include <espeak-ng/speak_lib.h>
#include <espeak-ng/espeak_ng.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

static int rate_hz = 0;
static char err[256] = "";

static float* g_out;
static int g_max, g_n;
static lic_ph* g_ph;
static int g_maxPh, g_nPh;

/* eSpeak's vowels, by the first character of their mnemonic (a e i o u, the capitals, schwa @, 3 6 9 0 2 &, V W Y y):
 * consonants never start with one of these (W is ø in German, w the English w). */
static int is_vowel(const char* s) {
	if (!s)
		return 0;
	while (*s == '\'' || *s == ',' || *s == '%' || *s == '=')   /* stress marks */
		s++;
	if (!*s)
		return 0;
	return strchr("aeiouAEIOUY@3690&2VWy", s[0]) != NULL;
}

static int cb(short* wav, int n, espeak_EVENT* ev) {
	for (int i = 0; wav && i < n && g_n < g_max; i++)
		g_out[g_n++] = wav[i] / 32768.f;
	for (; ev && ev->type != espeakEVENT_LIST_TERMINATED; ev++) {
		if (ev->type != espeakEVENT_PHONEME || g_nPh >= g_maxPh)
			continue;
		lic_ph* p = &g_ph[g_nPh];
		memset(p->name, 0, sizeof(p->name));
		memcpy(p->name, ev->id.string, 8);
		if (!p->name[0] || p->name[0] == '_')   /* pauses */
			continue;
		p->pos = (int) ((long long) ev->audio_position * rate_hz / 1000);
		p->vowel = is_vowel(p->name);
		g_nPh++;
	}
	return 0;
}

static int lic_init(const char* dataDir) {
	if (rate_hz > 0)
		return rate_hz;
	/* eSpeak sets the process's LC_CTYPE to UTF-8 when it starts; Rack's is given back (eSpeak reads UTF-8 itself) */
	char saved[128] = "C";
	const char* cur = setlocale(LC_CTYPE, NULL);
	if (cur)
		snprintf(saved, sizeof(saved), "%s", cur);
	int r = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, dataDir,
	                          espeakINITIALIZE_PHONEME_EVENTS | espeakINITIALIZE_DONT_EXIT);
	setlocale(LC_CTYPE, saved);
	if (r <= 0) {
		snprintf(err, sizeof(err), "eSpeak could not start (data: %s)", dataDir ? dataDir : "?");
		return -1;
	}
	rate_hz = r;
	espeak_SetSynthCallback(cb);
	return rate_hz;
}

static int lic_render(const char* voice, const char* text, int rate, int f0, float* out, int max, lic_ph* ph,
                      int maxPh, int* nPh) {
	if (rate_hz <= 0) {
		snprintf(err, sizeof(err), "not initialised");
		return -1;
	}
	if (espeak_SetVoiceByName(voice) != EE_OK) {
		snprintf(err, sizeof(err), "no voice %s", voice);
		return -2;
	}
	espeak_SetParameter(espeakRATE, rate, 0);
	espeak_SetParameter(espeakPITCH, 50, 0);
	espeak_ng_SetConstF0(f0 > 0 ? f0 : 0);           /* a constant pitch in Hz (0: the voice's own, flat) */
	espeak_SetParameter(espeakRANGE, 0, 0);       /* flat: the module sings the notes */
	espeak_SetParameter(espeakVOLUME, 100, 0);
	espeak_SetParameter(espeakWORDGAP, 0, 0);
	g_out = out;
	g_max = max;
	g_n = 0;
	g_ph = ph;
	g_maxPh = maxPh;
	g_nPh = 0;
	espeak_ERROR e = espeak_Synth(text, strlen(text) + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);
	if (e != EE_OK) {
		snprintf(err, sizeof(err), "synth error %d", (int) e);
		return -3;
	}
	espeak_Synchronize();
	if (nPh)
		*nPh = g_nPh;
	return g_n;
}

static const char* lic_error(void) {
	return err;
}

static const lic_api API = {LIC_API_VERSION, lic_init, lic_render, lic_error};

const lic_api* lic_get(void) {
	return &API;
}
