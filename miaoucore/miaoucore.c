/* SPDX-License-Identifier: GPL-3.0-only */
/* The MIAOU core: MIAOU's five sounds (a chip piano, FM bells, a square organ, a plucked banjo (Karplus-Strong) and a
 * meow (a voice through three moving formants)) and its toy speaker, from MIAOU's src/toy.c (github.com/pjac1810/miaou,
 * commit 417e693, GPL-3.0-only), with its voices in a struct instead of static variables, sixteen of them, fractional
 * pitches, and seven parameters scaling what the firmware held fixed. The beats, songs, recorder and click are left
 * out. Integer audio as in the firmware, 44.1 kHz. */
#include "miaoucore.h"
#include "svf_table.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define Q15 32767
#define CTRL 16u
#define KS_N 1024

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t sat15(int32_t x) {
  if (x >= 49152) return Q15;
  if (x <= -49152) return -Q15;
  int32_t x2 = (int32_t)(((int64_t)x * x) >> 15);
  int32_t x3 = (int32_t)(((int64_t)x2 * x) >> 15);
  return x - ((x3 * 4855) >> 15);
}

/* MIAOU's platform helpers (foma's platform/common.c) */
static int32_t sine(uint32_t phase) { /* a refined parabola, ~0.1 % */
  int32_t x = (int32_t)phase >> 16, ax = x < 0 ? -x : x, y, ay;
  y = (x * (32768 - ax)) >> 13;
  ay = y < 0 ? -y : y;
  y += ((((y * ay) >> 15) - y) * 7373) >> 15;
  return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static const uint32_t INC_C9[12] = {815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
                                    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t note_inc(int note) {
  note = clampi(note, 0, 127);
  return INC_C9[note % 12] >> (10 - note / 12);
}

static inline int32_t noise(uint32_t *nz) {
  *nz ^= *nz << 13, *nz ^= *nz >> 17, *nz ^= *nz << 5;
  return (int32_t)*nz >> 16;
}
static uint32_t inc_q8(int32_t p) {
  p = clampi(p, 0, 127 * 256);
  int n = p >> 8, f = p & 255;
  uint32_t a = note_inc(n), b = note_inc(n + 1);
  return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}
static uint32_t decay_ms(int32_t ms) { /* a per-CTRL-frames decay, Q16 */
  int32_t n = ms * 441;
  if (n <= (int32_t)CTRL * 10) return 0;
  return 65536u - (uint32_t)(10485760 / n);
}
static inline int32_t onepole_a(int32_t hz) {
  int32_t w = (clampi(hz, 1, 20000) * 4669) >> 10;
  return (int32_t)(((uint32_t)w << 15) / (uint32_t)(32768 + w));
}
static inline int32_t blep(uint32_t t, uint32_t inc, uint32_t rcp) {
  if (t < inc) {
    uint64_t xx = ((uint64_t)(t >> 16) * rcp) >> 17;
    int32_t x = xx > 32768u ? 32768 : (int32_t)xx;
    return 2 * x - ((x * x) >> 15) - 32768;
  }
  uint32_t d = 0u - t;
  if (d < inc) {
    uint64_t xx = ((uint64_t)(d >> 16) * rcp) >> 17;
    int32_t x = 32768 - (xx > 32768u ? 32768 : (int32_t)xx);
    return (x * x) >> 15;
  }
  return 0;
}

/* a state-variable filter (Simper): coefficients from SVF_G's index and k (Q16) */
typedef struct {
  int32_t a1, a2, a3;
} svfc_t;
typedef struct {
  int32_t ic1, ic2;
} svfs_t;
static void svf_coef(svfc_t *c, int idx, uint32_t kq) {
  uint32_t g = SVF_G[clampi(idx, 0, 1023)];
  uint32_t den = 65536u + (uint32_t)(((uint64_t)g * (g + kq)) >> 16);
  c->a1 = (int32_t)(0xFFFFFFFFu / den), c->a2 = (int32_t)(((int64_t)g * c->a1) >> 16), c->a3 = (int32_t)(((int64_t)g * c->a2) >> 16);
}
static inline int32_t svf_run(const svfc_t *c, svfs_t *s, int32_t x, int32_t *band) {
  int32_t v0 = x * 64, v3 = v0 - s->ic2;
  int32_t v1 = (int32_t)(((int64_t)c->a1 * s->ic1 + (int64_t)c->a2 * v3) >> 16);
  int32_t v2 = s->ic2 + (int32_t)(((int64_t)c->a2 * s->ic1 + (int64_t)c->a3 * v3) >> 16);
  s->ic1 = clampi(2 * v1 - s->ic1, -134217728, 134217728), s->ic2 = clampi(2 * v2 - s->ic2, -134217728, 134217728);
  *band = v1 >> 6;
  return v2 >> 6;
}

/* ---------------------------------------------------------------- the voices */
typedef struct {
  uint8_t inst, note, gate, vel, on;
  int16_t src;
  int32_t pq; /* the pitch, semitones x 256 */
  uint32_t age, t;
  uint32_t ph, ph2, inc, inc2, rcp;
  int32_t env, amp, lp, lpa;
  int16_t *ks;
  uint32_t kw, klen;
  int32_t kprev, kfb;
  uint32_t u, urate;
  svfc_t fc[3];
  svfs_t fs[3];
} voice_t;

struct mc_engine {
  voice_t V[MC_VOICES];
  int16_t KS[MC_VOICES][KS_N];
  uint32_t vage, nz;
  int32_t sp_hp, sp_lp;
  int mouth, look;
  float p[MC_NPARAMS];
  /* the parameters as the engine uses them */
  float decayF, toneF;
  uint32_t meowFrames;
  int32_t swoopQ8, breathQ8, kittenIdx, speaker;
};

/* the meow's path: formants (SVF_G indices, a kitten's: a third up), loudness, pitch (cents) at u = 0..256 */
#define MK 5
static const int16_t MU[MK] = {0, 40, 128, 215, 256};
static const int16_t MF1[MK] = {448, 510, 608, 510, 472}, MF2[MK] = {661, 760, 695, 617, 578}, MF3[MK] = {779, 801, 796, 773, 767};
static const int16_t MA[MK] = {30, 100, 100, 70, 0}, MP[MK] = {-200, 300, 200, -100, -500};
static int32_t mpath(const int16_t *y, int32_t u) {
  int k = 0;
  while (k < MK - 2 && u > MU[k + 1]) k++;
  int32_t d = MU[k + 1] - MU[k];
  return y[k] + (y[k + 1] - y[k]) * clampi(u - MU[k], 0, d) / d;
}

static void derive(mc_engine *m) {
  m->decayF = powf(2.f, (m->p[MC_P_DECAY] - 0.5f) * 4.f);
  m->toneF = powf(2.f, (m->p[MC_P_TONE] - 0.5f) * 3.f);
  m->meowFrames = (uint32_t)(24255.f * powf(2.f, (m->p[MC_P_MEOW] - 0.5f) * 3.f)); /* 0.55 s at 0.5 */
  m->swoopQ8 = (int32_t)(m->p[MC_P_SWOOP] * 2.f * 256.f);
  m->breathQ8 = (int32_t)(m->p[MC_P_BREATH] / 0.3f * 256.f);
  m->kittenIdx = (int32_t)lroundf((m->p[MC_P_KITTEN] - 0.5f) * 18.f * 8.84f);
  m->speaker = (int32_t)lroundf(m->p[MC_P_SPEAKER] * 100.f);
}

mc_engine *mc_new(uint32_t seed) {
  mc_engine *m = (mc_engine *)calloc(1, sizeof(mc_engine));
  if (!m) return NULL;
  m->nz = seed ? seed : 123457u;
  for (int i = 0; i < MC_NPARAMS; i++) m->p[i] = 0.5f;
  m->p[MC_P_BREATH] = 0.3f, m->p[MC_P_SPEAKER] = 0.4f;
  derive(m);
  mc_reset(m);
  return m;
}
void mc_free(mc_engine *m) { free(m); }
void mc_reset(mc_engine *m) {
  for (int i = 0; i < MC_VOICES; i++) m->V[i].on = 0, m->V[i].gate = 0, m->V[i].ks = m->KS[i];
  m->sp_hp = m->sp_lp = 0, m->mouth = 0;
}
void mc_set(mc_engine *m, int param, float v) {
  if (param < 0 || param >= MC_NPARAMS) return;
  v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
  if (m->p[param] == v) return;
  m->p[param] = v;
  derive(m);
}

static void set_inc(voice_t *v, int32_t pq) {
  v->inc = inc_q8(pq), v->rcp = 0xFFFFFFFFu / ((v->inc >> 16) | 1u);
}
/* the banjo's string: as long as one period of the voice's pitch (samples) */
static uint32_t ks_len(const voice_t *v) {
  uint32_t hz100 = (uint32_t)(((uint64_t)v->inc * 44100u) >> 25) + 1u;
  uint32_t len = (44100u * 128u) / hz100;
  if (len > KS_N - 4) len = KS_N - 4;
  if (len < 8) len = 8;
  return len;
}

void mc_note_off(mc_engine *m, int src) {
  for (int i = 0; i < MC_VOICES; i++) {
    voice_t *v = &m->V[i];
    if (v->on && v->gate && v->src == src) {
      v->gate = 0;
      if (v->inst == MC_MEOW) { /* a short meow: it ends soon, as a cat would */
        uint32_t rest = (256u << 16) - v->u, r = rest / 5292u;
        if (v->u < (256u << 16) && r > v->urate) v->urate = r;
      }
    }
  }
}

void mc_note_on(mc_engine *m, int src, int inst, float pitch, int vel) {
  mc_note_off(m, src);
  inst = clampi(inst, 0, MC_SOUNDS - 1);
  int32_t pq = clampi((int32_t)lroundf(pitch * 256.f), 0, 115 * 256);
  int note = clampi((pq + 128) >> 8, 0, 127);
  int best = -1;
  uint32_t old = 0xFFFFFFFFu;
  for (int i = 0; i < MC_VOICES; i++)
    if (!m->V[i].on) {
      best = i;
      break;
    }
  if (best < 0)
    for (int i = 0; i < MC_VOICES; i++)
      if (!m->V[i].gate && m->V[i].age < old) old = m->V[i].age, best = i;
  if (best < 0)
    for (int i = 0; i < MC_VOICES; i++)
      if (m->V[i].age < old) old = m->V[i].age, best = i;
  voice_t *v = &m->V[best];
  v->inst = (uint8_t)inst, v->note = (uint8_t)note, v->pq = pq, v->gate = 1, v->vel = (uint8_t)clampi(vel, 1, 127), v->on = 1;
  v->src = (int16_t)src, v->age = ++m->vage, v->t = 0, v->env = 0, v->lp = 0;
  set_inc(v, pq + (inst == MC_MEOW ? 12 * 256 : 0));
  if (inst == MC_BELLS) v->inc2 = (uint32_t)(((uint64_t)v->inc * 7) >> 1); /* 3.5: a bell's ratio */
  if (inst == MC_BANJO) {
    /* the string: as long as one period, filled with a pluck (TONE: brighter or softer than MIAOU's) */
    uint32_t len = ks_len(v);
    v->ks = m->KS[best], v->klen = len << 16, v->kw = 0, v->kprev = 0;
    int sh = m->toneF > 1.6f ? 0 : m->toneF > 0.75f ? 1 : m->toneF > 0.45f ? 2 : 3;
    int32_t p = 0;
    for (uint32_t i = 0; i < KS_N; i++) {
      int32_t n = noise(&m->nz);
      p += sh ? (n - p) >> sh : n - p;
      v->ks[i] = (int16_t)((p * 3) >> 2);
    }
    /* the loop's gain: 0.993 x 0.5 in MIAOU (Q16 32540); DECAY stretches its ringing */
    float fb = 1.f - 0.007f / m->decayF;
    v->kfb = (int32_t)(fb * 32768.f);
  }
  if (inst == MC_MEOW) v->u = 0, v->urate = (256u << 16) / (m->meowFrames | 1u), v->fs[0] = v->fs[1] = v->fs[2] = (svfs_t){0, 0};
  m->look = clampi((note - 66) / 3, -8, 8);
}

void mc_pitch(mc_engine *m, int src, float pitch) {
  int32_t pq = clampi((int32_t)lroundf(pitch * 256.f), 0, 115 * 256);
  for (int i = 0; i < MC_VOICES; i++) {
    voice_t *v = &m->V[i];
    if (v->on && v->gate && v->src == src && v->pq != pq) {
      v->pq = pq, v->note = (uint8_t)clampi((pq + 128) >> 8, 0, 127);
      if (v->inst != MC_MEOW) set_inc(v, pq); /* (the meow sets its own every block) */
      if (v->inst == MC_BELLS) v->inc2 = (uint32_t)(((uint64_t)v->inc * 7) >> 1);
      if (v->inst == MC_BANJO) v->klen = ks_len(v) << 16; /* the ringing string, at its new length */
    }
  }
}

void mc_all_off(mc_engine *m) {
  for (int i = 0; i < MC_VOICES; i++) m->V[i].on = 0, m->V[i].gate = 0;
}

static inline int32_t dms(const mc_engine *m, int32_t ms) { return (int32_t)(ms * m->decayF); }

void mc_render(mc_engine *m, float *out, int frames) {
  if (frames > MC_MAXFRAMES) frames = MC_MAXFRAMES;
  int32_t mix[MC_MAXFRAMES];
  for (int i = 0; i < frames; i++) mix[i] = 0;
  int mouth = 0;
  for (uint32_t b0 = 0; b0 < (uint32_t)frames; b0 += CTRL) {
    uint32_t n = (uint32_t)frames - b0 < CTRL ? (uint32_t)frames - b0 : CTRL;
    for (int vi = 0; vi < MC_VOICES; vi++) {
      voice_t *v = &m->V[vi];
      if (!v->on) continue;
      int32_t gain = v->vel * 258;
      int32_t e0 = v->env, e1;
      switch (v->inst) {
        case MC_PIANO:
          if (v->t < 3 * CTRL) e1 = Q15;
          else e1 = (int32_t)(((int64_t)e0 * decay_ms(v->gate ? dms(m, 900 + (90 - v->note) * 12) : dms(m, 120))) >> 16);
          v->lpa = onepole_a((int32_t)((1200 + (e1 >> 4)) * m->toneF));
          break;
        case MC_BELLS: e1 = v->t < CTRL ? Q15 : (int32_t)(((int64_t)e0 * decay_ms(v->gate ? dms(m, 1800) : dms(m, 700))) >> 16); break;
        case MC_ORGAN:
          e1 = v->gate ? e0 + (Q15 - e0) / 3 : (int32_t)(((int64_t)e0 * decay_ms(dms(m, 60))) >> 16);
          v->lpa = onepole_a((int32_t)(3500 * m->toneF));
          break;
        case MC_BANJO: e1 = v->gate ? Q15 : (int32_t)(((int64_t)e0 * decay_ms(dms(m, 90))) >> 16); break;
        default: { /* the meow: its own path */
          int32_t u = (int32_t)(v->u >> 16);
          e1 = mpath(MA, u) * 327;
          int32_t cents = (mpath(MP, u) * m->swoopQ8) >> 8;
          set_inc(v, v->pq + 12 * 256 + cents * 256 / 100);
          int kt = (v->note - 66) * 3 + m->kittenIdx; /* the formants rise a little with the pitch */
          svf_coef(&v->fc[0], mpath(MF1, u) + kt, 9000u), svf_coef(&v->fc[1], mpath(MF2, u) + kt, 11000u),
              svf_coef(&v->fc[2], mpath(MF3, u) + kt, 14000u);
          v->u += v->urate * n;
          if (v->u >= (256u << 16)) e1 = 0, v->u = 256u << 16;
          int o = e1 >> 7;
          if (o > mouth) mouth = o;
          break;
        }
      }
      v->env = e1;
      int32_t de = (e1 - e0) / (int32_t)n;
      int32_t bells_idx = (int32_t)(m->toneF * 256.f);
      for (uint32_t i = 0; i < n; i++) {
        int32_t x = 0, env = e0 + de * (int32_t)i, b1, b2, b3;
        uint32_t ph = v->ph;
        switch (v->inst) {
          case MC_PIANO: {
            int32_t p1 = (ph < 0x40000000u ? 20000 : -6667) + ((blep(ph, v->inc, v->rcp) - blep(ph - 0x40000000u, v->inc, v->rcp)) * 13333 >> 15);
            int32_t s2 = (ph << 1) < 0x80000000u ? 7000 : -7000;
            x = p1 + s2;
            v->lp += ((x - v->lp) * v->lpa) >> 15, x = v->lp;
            break;
          }
          case MC_BELLS: {
            v->ph2 += v->inc2;
            int32_t idx = ((env >> 2) * bells_idx) >> 8; /* the brightness dies with the sound */
            x = sine(ph + (uint32_t)((int64_t)sine(v->ph2) * idx * 4)) >> 1;
            break;
          }
          case MC_ORGAN: {
            int32_t s1 = (ph < 0x80000000u ? 12000 : -12000) + ((blep(ph, v->inc, v->rcp) - blep(ph - 0x80000000u, v->inc, v->rcp)) * 12000 >> 15);
            int32_t s2 = (ph << 1) < 0x80000000u ? 6000 : -6000, s3 = (ph * 3u) < 0x80000000u ? 3000 : -3000;
            x = s1 + s2 + s3;
            v->lp += ((x - v->lp) * v->lpa) >> 15, x = v->lp;
            break;
          }
          case MC_BANJO: {
            uint32_t rp = (v->kw << 16) - v->klen;
            uint32_t i0 = (rp >> 16) & (KS_N - 1), i1 = (i0 + 1) & (KS_N - 1);
            int32_t fr = (int32_t)(rp & 0xFFFF) >> 1;
            int32_t y = v->ks[i0] + (((v->ks[i1] - v->ks[i0]) * fr) >> 15);
            int32_t nx = (int32_t)(((int64_t)(y + v->kprev) * v->kfb) >> 16);
            v->kprev = y;
            v->ks[v->kw & (KS_N - 1)] = (int16_t)clampi(nx, -32767, 32767);
            v->kw++;
            x = y * 3;
            break;
          }
          default: {
            int32_t src = (int32_t)(ph >> 16) - 32768 - blep(ph, v->inc, v->rcp);
            src += (noise(&m->nz) * m->breathQ8) >> 11;
            svf_run(&v->fc[0], &v->fs[0], src >> 1, &b1), svf_run(&v->fc[1], &v->fs[1], src >> 1, &b2), svf_run(&v->fc[2], &v->fs[2], src >> 1, &b3);
            x = (b1 * 9 >> 4) + (b2 * 5 >> 4) + (b3 >> 3);
            break;
          }
        }
        v->ph = ph + v->inc;
        mix[b0 + i] += (int32_t)(((int64_t)x * env >> 15) * gain >> 15);
      }
      v->t += n;
      if ((v->inst == MC_MEOW && v->u >= (256u << 16)) || (v->t > 2 * CTRL && v->env < 40 && !(v->inst == MC_ORGAN && v->gate)) ||
          (v->inst == MC_BANJO && v->t > (uint32_t)(44100.f * 4.f * m->decayF)))
        v->on = 0, v->gate = 0;
    }
  }
  m->mouth = clampi(mouth, 0, 255);
  int sp = clampi(m->speaker, 0, 100);
  for (int i = 0; i < frames; i++) {
    int32_t x = mix[i] >> 1;
    /* the toy's speaker: no bass, a soft top, pushed, eight bits */
    int32_t t = x;
    m->sp_hp += (t - m->sp_hp) >> 6, t -= m->sp_hp;
    m->sp_lp += (t - m->sp_lp) >> 1, t = m->sp_lp;
    t = sat15(t * 3 / 2);
    t = clampi(t & ~0xFF, -32767, 32767);
    int32_t s = sat15(x);
    int32_t y = s + (((t - s) * sp) / 100);
    out[i] = (float)y * (1.f / 32768.f);
  }
}

int mc_mouth(const mc_engine *m) { return m->mouth; }
int mc_look(const mc_engine *m) { return m->look; }
int mc_lit(const mc_engine *m, int note) {
  for (int i = 0; i < MC_VOICES; i++)
    if (m->V[i].on && m->V[i].note == note && (m->V[i].gate || m->V[i].env > 2000)) return 1;
  return 0;
}
int mc_active(const mc_engine *m) {
  int n = 0;
  for (int i = 0; i < MC_VOICES; i++) n += m->V[i].on;
  return n;
}
