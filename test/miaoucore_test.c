/* SPDX-License-Identifier: GPL-3.0-only */
/* The MIAOU core without Rack: every sound at a few pitches and parameter settings sounds, stays in range and ends;
 * the meow opens the mouth; sixteen voices at once; a fuzz of the parameters. Optional: WAVs into a folder.
 *   gcc -std=gnu11 -O2 test/miaoucore_test.c miaoucore/miaoucore.c -o mct -lm && ./mct [WAVDIR] */
#include "../miaoucore/miaoucore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static const char *NAMES[MC_SOUNDS] = {"piano", "bells", "organ", "banjo", "meow"};

static void wav(const char *dir, const char *name, const float *x, int n) {
  if (!dir) return;
  char fn[512];
  snprintf(fn, sizeof fn, "%s/%s.wav", dir, name);
  FILE *f = fopen(fn, "wb");
  if (!f) return;
  int bytes = n * 2;
  unsigned char h[44] = "RIFF....WAVEfmt \x10\0\0\0\1\0\1\0....\0\0\0\0\2\0\x10\0data....";
  int v[3] = {36 + bytes, 44100, 88200};
  memcpy(h + 4, &v[0], 4), memcpy(h + 24, &v[1], 4), memcpy(h + 28, &v[2], 4), memcpy(h + 40, &bytes, 4);
  fwrite(h, 1, 44, f);
  for (int i = 0; i < n; i++) {
    float s = x[i] < -1 ? -1 : x[i] > 1 ? 1 : x[i];
    short q = (short)(s * 32767);
    fwrite(&q, 2, 1, f);
  }
  fclose(f);
}

/* plays a note for `hold` s, lets go, renders `tail` s more; returns the peak; *mouth the widest mouth */
static float play(mc_engine *m, int sound, float pitch, float hold, float tail, float *buf, int *len, int *mouth, int *endsOk) {
  int n = 0, mx = 0;
  float peak = 0.f;
  mc_note_on(m, 0, sound, pitch, 100);
  int total = (int)((hold + tail) * MC_RATE), off = (int)(hold * MC_RATE);
  float tmp[MC_MAXFRAMES];
  for (int i = 0; i < total; i += 32) {
    if (i >= off && i - 32 < off) mc_note_off(m, 0);
    mc_render(m, tmp, 32);
    for (int k = 0; k < 32; k++) {
      if (!isfinite(tmp[k])) { peak = 99.f; }
      if (fabsf(tmp[k]) > peak) peak = fabsf(tmp[k]);
      if (buf && n < 44100 * 8) buf[n++] = tmp[k];
    }
    if (mc_mouth(m) > mx) mx = mc_mouth(m);
  }
  if (len) *len = n;
  if (mouth) *mouth = mx;
  if (endsOk) *endsOk = mc_active(m) == 0;
  return peak;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : NULL;
  static float buf[44100 * 8];
  mc_engine *m = mc_new(1);
  CHECK(m, "mc_new");
  for (int s = 0; s < MC_SOUNDS; s++) {
    for (int k = 0; k < 3; k++) {
      float pitch = 48.f + 12.f * k + 0.5f;
      int len, mouth, ends;
      mc_reset(m);
      float pk = play(m, s, pitch, 0.6f, 6.0f, buf, &len, &mouth, &ends);
      printf("%-6s %5.1f  peak %.3f  mouth %3d  %s\n", NAMES[s], pitch, pk, mouth, ends ? "ends" : "RINGS ON");
      CHECK(pk > 0.02f && pk <= 1.0f, "%s at %.1f: peak %.3f", NAMES[s], pitch, pk);
      CHECK(ends, "%s at %.1f does not end", NAMES[s], pitch);
      if (s == MC_MEOW) CHECK(mouth > 100, "the meow does not open the mouth (%d)", mouth);
      else CHECK(mouth == 0, "%s opens the mouth", NAMES[s]);
      if (k == 1) {
        char nm[64];
        snprintf(nm, sizeof nm, "%s", NAMES[s]);
        wav(dir, nm, buf, len);
      }
    }
  }
  /* the parameters at their ends */
  for (int p = 0; p < MC_NPARAMS; p++)
    for (int e = 0; e < 2; e++) {
      mc_engine *q = mc_new(7);
      mc_set(q, p, (float)e);
      for (int s = 0; s < MC_SOUNDS; s++) {
        int ends;
        mc_reset(q);
        float pk = play(q, s, 60.f, 0.5f, 18.f, NULL, NULL, NULL, &ends);
        CHECK(pk > 0.005f && pk <= 1.0f, "param %d=%d %s: peak %.3f", p, e, NAMES[s], pk);
        CHECK(ends, "param %d=%d %s does not end", p, e, NAMES[s]);
      }
      mc_free(q);
    }
  /* the meow's length follows MEOW */
  {
    mc_engine *q = mc_new(3);
    int lens[2];
    for (int e = 0; e < 2; e++) {
      mc_set(q, MC_P_MEOW, e ? 1.f : 0.f);
      mc_reset(q);
      mc_note_on(q, 0, MC_MEOW, 60.f, 100);
      float tmp[32];
      int i = 0;
      while (mc_active(q) && i < 44100 * 4) mc_render(q, tmp, 32), i += 32;
      lens[e] = i;
    }
    printf("meow length %.2f s .. %.2f s\n", lens[0] / 44100.f, lens[1] / 44100.f);
    CHECK(lens[0] < 12000 && lens[1] > 60000, "MEOW length %d .. %d", lens[0], lens[1]);
    mc_free(q);
  }
  /* sixteen voices at once, all the sounds, a held pitch moving */
  {
    mc_reset(m);
    for (int c = 0; c < 16; c++) mc_note_on(m, c, c % MC_SOUNDS, 40.f + c * 2.f, 90);
    float tmp[64], pk = 0.f;
    for (int i = 0; i < 44100; i += 64) {
      mc_pitch(m, 2, 44.f + (i / 4410) * 0.1f);
      mc_render(m, tmp, 64);
      for (int k = 0; k < 64; k++)
        if (fabsf(tmp[k]) > pk || !isfinite(tmp[k])) pk = isfinite(tmp[k]) ? fabsf(tmp[k]) : 99.f;
    }
    printf("16 voices: peak %.3f, %d active\n", pk, mc_active(m));
    CHECK(pk > 0.1f && pk <= 1.0f, "16 voices: peak %.3f", pk);
    CHECK(mc_active(m) > 0, "16 voices: none left");
    mc_all_off(m);
    CHECK(mc_active(m) == 0, "all off");
  }
  /* a fuzz: random notes, parameters, lengths */
  {
    srand(5);
    float tmp[64];
    for (int it = 0; it < 20000; it++) {
      int r = rand() % 8;
      if (r < 3) mc_note_on(m, rand() % 20, rand() % MC_SOUNDS, (float)(rand() % 12000) / 100.f, 1 + rand() % 127);
      else if (r < 5) mc_note_off(m, rand() % 20);
      else if (r == 5) mc_set(m, rand() % MC_NPARAMS, (float)(rand() % 1001) / 1000.f);
      else if (r == 6) mc_pitch(m, rand() % 20, (float)(rand() % 12000) / 100.f);
      int f = 1 + rand() % 64;
      mc_render(m, tmp, f);
      for (int k = 0; k < f; k++)
        if (!isfinite(tmp[k]) || fabsf(tmp[k]) > 1.f) {
          CHECK(0, "fuzz: sample %f", tmp[k]);
          it = 1 << 30;
          break;
        }
    }
  }
  mc_free(m);
  printf(fails ? "%d FAILED\n" : "all good\n", fails);
  return fails ? 1 : 0;
}
