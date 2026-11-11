/* SPDX-License-Identifier: GPL-3.0-or-later
 * LIED core: eSpeak NG as a small library for GENRA Test's LIED (res/liedcore.dll / .so / .dylib).
 * Plain C, no Rack. One engine for the whole process (eSpeak keeps its state in static variables): the plugin calls it
 * from a single worker thread only. It turns text into audio at a flat pitch (the module sings it in tune) and
 * reports each phoneme's start, so the module can find the syllables and their vowels.
 */
#ifndef LIEDCORE_H
#define LIEDCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define LIC_API_VERSION 1

typedef struct {
	int pos;       /* first sample of the phoneme */
	int vowel;     /* 1: a vowel (a syllable's nucleus) */
	char name[9];  /* eSpeak's phoneme mnemonic */
} lic_ph;

typedef struct {
	int api; /* LIC_API_VERSION */
	/** dataDir: the folder espeak-ng-data. Returns the sample rate (22050), or < 0 on error (see error()). */
	int (*init)(const char* dataDir);
	/** voice: "de", "en-us+klatt2" …; text: UTF-8. A constant pitch f0 in Hz (0: the voice's own, flat), rate in words
	 * per minute.
	 * Writes up to max samples (-1 .. 1) and up to maxPh phonemes. Returns the number of samples, or < 0 on error. */
	int (*render)(const char* voice, const char* text, int rate, int f0, float* out, int max, lic_ph* ph, int maxPh,
	              int* nPh);
	const char* (*error)(void);
} lic_api;

typedef const lic_api* (*lic_get_fn)(void);

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const lic_api* lic_get(void);

#ifdef __cplusplus
}
#endif
#endif
