// SPDX-License-Identifier: GPL-3.0-only
// NEBEL – the sound engine of the cluster universe: every note (or GENRA's whole chord) becomes a cluster of up to
// 8 voices: the notes of a chord, spread over octaves (SPREAD), a little apart (DETUNE, CHAOS), wandering (DRIFT),
// one after the other (STRUM). Five generators (wavetable, analog, additive, modal resonators, string), five textures
// (shimmer, grain, dust, smear, fold), three reverbs (nebula, hall, void) and ORBIT, which turns the voices round in
// the stereo field in time. Plain C++ without Rack (test/neon_test.cpp).
#pragma once
#include "../neon/Wavetable.hpp"
#include "../../genra/mono/MonoDsp.hpp"
#include "../../genra/OutLimit.hpp"

namespace nebel {

using namespace neon;

enum Gen { G_WAVETABLE, G_ANALOG, G_ADDITIVE, G_MODAL, G_STRING, NUM_GENS };
static const char* const GEN_NAMES[NUM_GENS] = {"WAVETABLE", "ANALOG", "ADDITIVE", "MODAL", "STRING"};
static const char* const GEN_INFO[NUM_GENS] = {"4 TABLES", "SAW TO PULSE", "16 PARTIALS", "STRUCK BODIES", "BOWED STRING"};

enum Chord { C_MIN9, C_MAJ7, C_MIN7, C_MAJ9, C_SUS4, C_FIFTHS, C_OCTAVES, C_ADD9, C_MIN11, C_DOM9, C_QUARTAL,
             C_CLUSTER, C_WHOLE, C_CHAOS, NUM_CHORDS };
static const char* const CHORD_NAMES[NUM_CHORDS] = {"MINOR 9", "MAJOR 7", "MINOR 7", "MAJOR 9", "SUS 4", "FIFTHS",
                                                     "OCTAVES", "ADD 9", "MINOR 11", "DOMINANT 9", "QUARTAL", "CLUSTER",
                                                     "WHOLE TONE", "CHAOS"};
static const char* const CHORD_INFO[NUM_CHORDS] = {"DARK, SOFT", "BRIGHT", "DEEP", "LUSH", "OPEN", "POWER", "PURE",
                                                   "SWEET", "WIDE", "JAZZY", "MODERN", "DENSE", "DREAMY", "RANDOM"};
static const int CHORD_TONES[NUM_CHORDS][6] = {
	{0, 3, 7, 10, 14, -1}, {0, 4, 7, 11, -1, -1}, {0, 3, 7, 10, -1, -1}, {0, 4, 7, 11, 14, -1}, {0, 5, 7, -1, -1, -1},
	{0, 7, -1, -1, -1, -1}, {0, -1, -1, -1, -1, -1}, {0, 4, 7, 14, -1, -1}, {0, 3, 7, 10, 14, 17}, {0, 4, 7, 10, 14, -1},
	{0, 5, 10, 15, -1, -1}, {0, 2, 3, 5, 7, -1}, {0, 2, 4, 6, 8, 10}, {0, -1, -1, -1, -1, -1}};

enum Texture { T_SHIMMER, T_GRAIN, T_DUST, T_SMEAR, T_FOLD, NUM_TEXTURES };
static const char* const TEXT_NAMES[NUM_TEXTURES] = {"SHIMMER", "GRAIN", "DUST", "SMEAR", "FOLD"};
static const char* const TEXT_INFO[NUM_TEXTURES] = {"OCTAVE SPARKLE", "GRAIN CLOUD", "COSMIC DUST", "DIFFUSION", "WAVE FOLDER"};

enum ChordSrc { SRC_PANEL, SRC_GENRA, NUM_SRCS };

static const int MAXG = 4;    // note groups (PANEL: one per channel; GENRA: one)
static const int MAXVG = 8;   // voices per group
static const int MAXVOICES = 16;
static const int CTL = 16;  // samples between control steps

struct Params {
	int gen = G_WAVETABLE;
	float timbre = 0.5f, morph = 0.3f, oct = 0.f;
	int chord = C_MIN9;
	float spread = 0.4f;
	int voices = 6;
	float detune = 0.3f, chaos = 0.1f, drift = 0.3f, strum = 0.f, accent = 0.5f;
	int texture = T_SHIMMER;
	float amount = 0.3f, color = 0.5f, density = 0.5f, width = 0.7f;
	int space = V_NEBULA;
	float size = 0.7f, decay = 0.6f, smix = 0.4f, tone = 0.5f;
	float atk = 0.5f, dec = 0.6f, sus = 0.8f, rel = 0.6f;
	int orate = 4;
	float odepth = 0.4f, macro = 0.f, glide = 0.1f;
	int chordSrc = SRC_PANEL;
	float outGain = 1.f;
	bool freeze = false;
	uint32_t seed = 1;
};

struct Channel {
	float voct = 0.f, vel = 0.8f;
	bool gate = false, acc = false;
};

/** Smoothed random walk (DRIFT): a new target every period, followed slowly. */
struct Walk {
	float v = 0.f, target = 0.f, t = 0.f;
	float step(Rng& r, float dt, float period) {
		t += dt;
		if (t >= period) {
			t = 0.f;
			target = r.bi();
		}
		v += (target - v) * std::min(1.f, dt / (0.6f * period));
		return v;
	}
};

struct Voice {
	bool on = false;       // part of a sounding group
	int group = 0, k = 0;  // which group, which voice of it
	float pitch = 0.f, target = 0.f;
	float delay = 0.f;     // STRUM: seconds until this voice joins
	bool gate = false;
	gmono::Env env;
	float ph = 0.f, ph2 = 0.f;
	float add[16] = {};    // additive partial phases
	gmono::Osc analog;
	float lp = 0.f;
	// modal: 6 resonators
	float ry1[6] = {}, ry2[6] = {};
	float exc = 0.f;
	// string
	Ring line;
	float sLp = 0.f;
	int burst = 0;
	Walk walk;
	float detCents = 0.f, chaosSemi = 0.f, pan = 0.5f, flick = 1.f;
	// kept between the control steps (every 16 samples)
	float freq = 261.f, tm = 0.5f, vp = 0.f, panNow = 0.5f;
	float addInc[16] = {}, addAmp[16] = {};
	int addN = 0;
	float mc[6] = {}, mr2[6] = {}, mg[6] = {};
	float anaCut = 0.1f;
};

struct Group {
	bool gate = false, active = false, acc = false;
	float base = 0.f, vel = 0.8f;
	float notes[8] = {};
	int nNotes = 1;
};

struct Engine {
	float fs = 48000.f, dt = 1.f / 48000.f;
	Voice v[MAXVOICES];
	Group g[MAXG];
	bool lastGate[MAXG] = {};
	std::shared_ptr<Table> tabs[4];
	Rng rng{7};
	float orbitPh = 0.f;
	// texture state
	Shifter shift[2], shift5[2];
	Ring grainBuf[2];
	struct Grain {
		float pos = 0.f, len = 1.f, age = 1.f, rate = 1.f, pan = 0.5f;
		bool on = false;
	} grains[8];
	float grainClock = 0.f;
	float dustLp[2] = {}, dustEnv = 0.f, dustHold[2] = {}, dustPh = 0.f;
	Ring smear[6][2];
	float smearPh = 0.f, smearFb[2] = {};
	float texLp[2] = {};
	float kShim = 0.1f, kDust = 0.1f, kSmear = 0.1f, kFold = 0.1f, dustQ = 4096.f;
	Verb verb;
	DcBlock dcb[2];  // 2.1.2
	int ctl = 0;
	// display
	float showPitch[MAXVOICES] = {};
	bool showOn[MAXVOICES] = {};
	float showLevel[MAXVOICES] = {};
	float showEnv = 0.f, lastNote = 0.f;
	int activeVoices = 0;

	Engine() {
		static const int T[4] = {B_CHOIR, B_GLASS, B_STRINGS, B_AIR};
		for (int i = 0; i < 4; i++)
			tabs[i] = makeBuiltin(T[i]);
		setSampleRate(48000.f);
	}
	void setSampleRate(float sr) {
		fs = sr, dt = 1.f / sr;
		for (int c = 0; c < 2; c++) {
			shift[c].setSampleRate(sr);
			shift5[c].setSampleRate(sr);
			grainBuf[c].resize((int) (2.f * sr));
			for (int i = 0; i < 6; i++)
				smear[i][c].resize((int) (0.06f * sr));
		}
		for (auto& x : v)
			x.line.resize((int) (sr / 18.f) + 8);
		verb.setSampleRate(sr);
		dcb[0].setSampleRate(sr), dcb[1].setSampleRate(sr);
	}
	void allOff() {
		for (auto& x : v) {
			x.on = x.gate = false;
			x.env.v = 0.f;
			x.env.stage = 0;
		}
		for (auto& y : g)
			y.gate = y.active = false;
		for (bool& b : lastGate)
			b = false;
		verb.clear();
	}
	void resetOrbit() { orbitPh = 0.f; }
	/** While the output is muted for a preset change: generators, textures and the space start clean (notes go on). */
	void softReset() {
		for (auto& x : v) {
			x.line.clear();
			x.sLp = x.lp = 0.f;
			for (int i = 0; i < 6; i++)
				x.ry1[i] = x.ry2[i] = 0.f;
			// silent until restrike() plays the held notes again with the new sound
			x.env.v = 0.f;
			x.env.stage = 0;
			x.env.gate = false;
			x.burst = 0;
		}
		for (auto& gn : grains)
			gn.on = false;
		for (int c = 0; c < 2; c++) {
			grainBuf[c].clear();
			shift[c].buf.clear();
			shift5[c].buf.clear();
			for (int i = 0; i < 6; i++)
				smear[i][c].clear();
			texLp[c] = smearFb[c] = dustLp[c] = 0.f;
		}
		verb.clear();
	}

	/** The octave a voice of a cluster moves to with SPREAD (a fixed pattern, widening). */
	static float spreadOct(int k, float spread) {
		static const float pat[8] = {0.f, 1.f, -1.f, 1.f, 2.f, -1.f, 2.f, 0.f};
		return std::round(pat[k & 7] * spread * 1.4f);
	}

	/** The pitch (V/OCT) of voice k of group gi. */
	float voicePitch(const Params& p, const Group& gr, int k, float chaos, float spread) {
		float semis;
		if (p.chordSrc == SRC_GENRA) {
			int m = std::max(1, gr.nNotes);
			float up = k >= m ? ((k / m) % 2 ? 1.f : -1.f) : 0.f;  // more voices than notes: doubled an octave up / down
			return gr.notes[k % m] + up + spreadOct(k, spread) + p.oct;
		}
		int tones[6], nt = 0;
		if (p.chord == C_CHAOS) {
			Rng r(p.seed * 31u + 5u);
			nt = 5;
			for (int i = 0; i < nt; i++)
				tones[i] = i == 0 ? 0 : (int) (r.uni() * 14.f);
		} else
			for (int i = 0; i < 6 && CHORD_TONES[p.chord][i] >= 0; i++)
				tones[nt++] = CHORD_TONES[p.chord][i];
		semis = (float) tones[k % nt] + 12.f * (float) (k / nt);
		(void) chaos;
		return gr.base + semis / 12.f + spreadOct(k, spread) + p.oct;
	}

	/** The kept values of a voice (every control step): partials, resonators, filter. */
	void prepare(const Params& p, Voice& x) {
		float freq = x.freq, timbre = x.tm, morph = p.morph;
		float inc = std::min(freq * dt, 0.45f);
		switch (p.gen) {
			case G_ANALOG: x.anaCut = 1.f - std::exp(-2.f * PI * (300.f * std::pow(60.f, morph)) * dt); break;
			case G_ADDITIVE: {
				float tilt = 2.4f - 2.1f * timbre, odd = clampf(morph * 2.f, 0.f, 1.f);
				float stretch = std::max(0.f, morph - 0.5f) * 0.04f;
				float norm = 0.f;
				x.addN = 0;
				for (int h = 0; h < 16; h++) {
					float n = (float) (h + 1);
					float hi = n * (1.f + stretch * n) * inc;
					if (hi >= 0.48f)
						break;
					float a = std::pow(n, -tilt) * ((h & 1) ? 1.f - odd : 1.f);
					x.addInc[h] = hi, x.addAmp[h] = a;
					norm += a;
					x.addN = h + 1;
				}
				float k = 1.1f / std::max(0.5f, std::sqrt(norm));
				for (int h = 0; h < x.addN; h++)
					x.addAmp[h] *= k;
				break;
			}
			case G_MODAL: {
				// ratios: wood (harmonic-ish), bell, metal by TIMBRE; MORPH: how long they ring
				static const float R[3][6] = {{1.f, 2.f, 3.f, 4.f, 5.f, 6.f},
				                              {1.f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f},
				                              {1.f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f}};
				float m = clampf(timbre, 0.f, 1.f) * 2.f;
				int a = std::min((int) m, 1);
				float f = m - a;
				float decay = 0.2f + 3.f * morph * morph;
				for (int i = 0; i < 6; i++) {
					float ratio = R[a][i] + (R[a + 1][i] - R[a][i]) * f;
					float w = 2.f * PI * freq * ratio * dt;
					if (w >= 2.9f) {
						x.mc[i] = x.mr2[i] = x.mg[i] = 0.f;
						continue;
					}
					float r = std::exp(-dt / (decay / (1.f + 0.5f * i)));
					x.mc[i] = 2.f * r * std::cos(w);
					x.mr2[i] = r * r;
					x.mg[i] = (1.f - r) * 4.f;
				}
				break;
			}
			default: break;
		}
	}

	float gen(const Params& p, Voice& x) {
		float freq = x.freq, timbre = x.tm, morph = p.morph;
		float inc = std::min(freq * dt, 0.45f);
		switch (p.gen) {
			default:
			case G_WAVETABLE: {
				x.ph = wrap(x.ph + inc);
				float m = clampf(morph, 0.f, 1.f) * 3.f;
				int a = std::min((int) m, 2);
				float f = m - a;
				int lvl = Table::level(inc);
				const Table* A = tabs[a].get();
				const Table* B = tabs[a + 1].get();
				float pa = timbre * (A->frames - 1), pb = timbre * (B->frames - 1);
				return A->read(lvl, pa, x.ph) * (1.f - f) + B->read(lvl, pb, x.ph) * f;
			}
			case G_ANALOG: {
				float y = x.analog.process(inc, 0.33f + 0.6f * timbre);
				x.lp += (y - x.lp) * x.anaCut;
				return x.lp * 1.2f;
			}
			case G_ADDITIVE: {
				float y = 0.f;
				for (int h = 0; h < x.addN; h++) {
					x.add[h] += x.addInc[h];
					if (x.add[h] >= 1.f)
						x.add[h] -= 1.f;
					y += x.addAmp[h] * sin2pi(x.add[h]);
				}
				return y;
			}
			case G_MODAL: {
				float noise = rng.bi();
				float ex = (x.burst > 0 ? noise * 0.8f : 0.f) + noise * 0.06f * (1.f - 0.7f * morph);
				if (x.burst > 0)
					x.burst--;
				float y = 0.f;
				for (int i = 0; i < 6; i++) {
					float o = x.mc[i] * x.ry1[i] - x.mr2[i] * x.ry2[i] + ex * x.mg[i];
					x.ry2[i] = x.ry1[i], x.ry1[i] = o;
					y += o / (1.f + 0.6f * i);
				}
				return y * 22.f;
			}
			case G_STRING: {
				float len = clampf(fs / std::max(freq, 20.f), 2.f, (float) x.line.mask - 4.f);
				float y = x.line.read(len);
				float bright = 0.15f + 0.8f * timbre;
				x.sLp += (y - x.sLp) * bright;
				float noise = rng.bi();
				float ex = (x.burst > 0 ? noise * 0.45f : 0.f) + noise * 0.03f * morph;
				if (x.burst > 0)
					x.burst--;
				x.line.push(x.sLp * 0.9985f + ex);
				return y * 8.f;
			}
		}
	}

	/** One stereo sample. bpm: the tempo; inL/inR: the audio inputs (into the texture and space). */
	void process(const Params& p, const Channel* ch, int n, float bpm, float inL, float inR, float& L, float& R,
	             float& env, float& orbitOut) {
		float mac = clampf(p.macro, 0.f, 1.f);
		float timbre = clampf(p.timbre + 0.5f * mac, 0.f, 1.f);
		float spread = clampf(p.spread + 0.5f * mac, 0.f, 1.f);
		float chaos = clampf(p.chaos + 0.2f * mac, 0.f, 1.f);
		float amount = clampf(p.amount + 0.5f * mac, 0.f, 1.f);
		float smix = clampf(p.smix + 0.4f * mac, 0.f, 1.f);
		if (!p.freeze)
			orbitPh = wrap(orbitPh + bpm / 60.f / RATE_BEATS[clamp(p.orate, 0, NUM_RATES - 1)] * dt);
		orbitOut = sin2pi(orbitPh);

		// ---- groups from the channels
		int ng;
		if (p.chordSrc == SRC_GENRA) {
			ng = 1;
			Group& gr = g[0];
			bool any = false;
			int m = 0;
			float notes[8];
			for (int c = 0; c < std::min(n, 8); c++)
				if (ch[c].gate) {
					any = true;
					notes[m++] = ch[c].voct;
				}
			if (m > 0 && !p.freeze) {
				std::sort(notes, notes + m);
				for (int i = 0; i < m; i++)
					gr.notes[i] = notes[i];
				gr.nNotes = m;
				gr.base = notes[0];
			}
			bool trig = any && !lastGate[0];
			if (any) {
				gr.vel = ch[0].vel;
				gr.acc = ch[0].acc;
			}
			gr.gate = p.freeze ? (gr.gate || any) : any;
			if (trig)
				startGroup(p, 0, chaos, spread);
			lastGate[0] = any;
			for (int i = 1; i < MAXG; i++)
				g[i].gate = false;
		} else {
			ng = std::max(1, std::min(n, MAXG));
			for (int c = 0; c < MAXG; c++) {
				bool gt = c < n && ch[c].gate;
				Group& gr = g[c];
				if (gt && !p.freeze) {
					gr.base = ch[c].voct;
					gr.vel = ch[c].vel;
					gr.acc = ch[c].acc;
				}
				bool trig = gt && !lastGate[c];
				gr.gate = p.freeze ? (gr.gate || gt) : gt;
				if (trig)
					startGroup(p, c, chaos, spread);
				lastGate[c] = gt;
			}
		}
		int perGroup = std::max(1, std::min(clamp(p.voices, 1, MAXVG), MAXVOICES / ng));

		// ---- voices
		float aT = envTime(p.atk), dT = envTime(p.dec), rT = envTime(p.rel);
		float glideK = 1.f - std::exp(-dt * CTL / std::max(0.002f, 0.6f * p.glide * p.glide * 0.35f + 0.002f));
		float sumL = 0.f, sumR = 0.f;
		float driftPeriod = 4.f - 3.f * p.drift;
		int act = 0;
		bool doCtl = --ctl <= 0;
		if (doCtl)
			ctl = CTL;
		for (int i = 0; i < MAXVOICES; i++) {
			Voice& x = v[i];
			if (!x.on)
				continue;
			Group& gr = g[x.group];
			bool waiting = false;  // STRUM: this voice joins later
			if (x.k >= perGroup) {
				x.gate = false;
			} else if (x.delay > 0.f) {
				x.delay -= dt;
				x.gate = false;
				waiting = gr.gate;
			} else
				x.gate = gr.gate;
			float e = x.env.process(x.gate, aT, dT, p.sus, rT, dt);
			if (!x.gate && !waiting && x.env.stage == 0) {
				x.on = false;
				showOn[i] = false;
				continue;
			}
			act++;
			if (doCtl) {
				if (!p.freeze)
					x.target = voicePitch(p, gr, x.k, chaos, spread);
				x.pitch += (x.target - x.pitch) * glideK;
				float dr = p.freeze ? x.walk.v : x.walk.step(rng, dt * CTL, driftPeriod);
				float cents = x.detCents * p.detune + dr * p.drift * 30.f + 8.f * p.odepth * sin2pi(orbitPh + 0.13f * x.k);
				float semis = x.chaosSemi * chaos * chaos;
				x.vp = x.pitch + (cents / 100.f + semis) / 12.f;
				x.freq = C4 * std::pow(2.f, x.vp);
				x.tm = clampf(timbre + 0.15f * p.odepth * sin2pi(orbitPh + 0.25f * x.k), 0.f, 1.f);
				x.panNow = clampf(x.pan + 0.4f * p.odepth * sin2pi(orbitPh + (float) x.k / std::max(1, perGroup)), 0.f, 1.f);
				prepare(p, x);
			}
			float vp = x.vp;
			float y = gen(p, x);
			float lev = e * (0.5f + 0.5f * gr.vel) * (gr.acc ? 1.f + 0.6f * p.accent : 1.f);
			lev *= 1.f - chaos * 0.5f * (1.f - x.flick);
			float pan = x.panNow;  // the voices sit apart and turn with ORBIT
			sumL += y * lev * std::sqrt(1.f - pan);
			sumR += y * lev * std::sqrt(pan);
			showPitch[i] = vp;
			showOn[i] = true;
			showLevel[i] = e;
			if (x.k == 0) {
				showEnv = e;
				lastNote = x.pitch;
			}
		}
		activeVoices = act;
		float norm = 1.f / std::sqrt((float) std::max(1, std::min(act, perGroup * ng)));
		float l = sumL * norm * 0.9f + inL * 0.2f, r = sumR * norm * 0.9f + inR * 0.2f;
		env = showEnv;

		// ---- texture
		if (doCtl) {
			float col = clampf(p.color, 0.f, 1.f), den = clampf(p.density, 0.f, 1.f);
			kShim = 1.f - std::exp(-2.f * PI * (800.f + 9000.f * col) * dt);
			kDust = 1.f - std::exp(-2.f * PI * (400.f + 8000.f * col) * dt);
			kSmear = 1.f - std::exp(-2.f * PI * (600.f + 12000.f * col) * dt);
			kFold = 1.f - std::exp(-2.f * PI * (1500.f + 14000.f * (1.f - den)) * dt);
			dustQ = std::pow(2.f, 12.f - 8.f * amount);
		}
		texture(p, amount, l, r);
		// ---- space
		if (doCtl)
			verb.set(p.space, p.size, p.decay, p.tone);
		float wl, wr;
		verb.process(l, r, wl, wr);
		l = l * (1.f - 0.45f * smix) + wl * smix * 1.3f;
		r = r * (1.f - 0.45f * smix) + wr * smix * 1.3f;
		// 2.1.2: FOLD (and anything else) must not leave a DC offset on the outputs
		l = dcb[0].process(l), r = dcb[1].process(r);
		L = genra_out::limit(l * 5.f * p.outGain);
		R = genra_out::limit(r * 5.f * p.outGain);
	}

	/** After softReset(), with the new knobs: the held notes start again (attack, strum, spread of the new sound). */
	void restrike(const Params& p) {
		float mac = clampf(p.macro, 0.f, 1.f);
		float spread = clampf(p.spread + 0.5f * mac, 0.f, 1.f), chaos = clampf(p.chaos + 0.2f * mac, 0.f, 1.f);
		for (auto& x : v)
			if (!g[x.group].gate)
				x.on = false;
		for (int gi = 0; gi < MAXG; gi++)
			if (g[gi].gate)
				startGroup(p, gi, chaos, spread, true);
	}

	void startGroup(const Params& p, int gi, float chaos, float spread, bool snap = false) {
		Group& gr = g[gi];
		gr.active = true;
		int nv = clamp(p.voices, 1, MAXVG);
		Rng r(p.seed * 7919u + (uint32_t) gi * 104729u + 3u);
		for (int k = 0; k < MAXVG; k++) {
			// find this group's voice k, or a free one, or the quietest
			int slot = -1;
			for (int i = 0; i < MAXVOICES; i++)
				if (v[i].on && v[i].group == gi && v[i].k == k)
					slot = i;
			if (slot < 0 && k < nv) {
				for (int i = 0; i < MAXVOICES && slot < 0; i++)
					if (!v[i].on)
						slot = i;
				if (slot < 0) {
					float q = 1e9f;
					for (int i = 0; i < MAXVOICES; i++)
						if (v[i].env.v < q && v[i].group != gi)
							q = v[i].env.v, slot = i;
				}
			}
			if (slot < 0)
				continue;
			Voice& x = v[slot];
			bool fresh = !x.on;
			x.on = true;
			x.group = gi, x.k = k;
			x.target = voicePitch(p, gr, k, chaos, spread);
			if (fresh || snap || p.glide < 0.02f)
				x.pitch = x.target;  // (a restrike after a preset change: no glide from the old sound)
			x.delay = p.strum * 0.12f * k;
			x.env.gate = false;
			x.detCents = r.bi() * 25.f;
			x.chaosSemi = r.bi() * 7.f;
			x.flick = r.uni();
			x.pan = 0.5f + (k & 1 ? 1.f : -1.f) * (0.12f + 0.06f * k) * clampf(p.width + 0.3f, 0.f, 1.3f);
			x.pan = clampf(x.pan, 0.05f, 0.95f);
			x.burst = (int) (0.006f * fs);
			x.vp = x.pitch;
			x.freq = C4 * std::pow(2.f, x.pitch);
			x.tm = clampf(p.timbre, 0.f, 1.f);
			x.panNow = x.pan;
			prepare(p, x);
			if (fresh) {
				x.ph = r.uni();
				for (float& a : x.add)
					a = r.uni();
				x.line.clear();
			}
		}
	}

	void texture(const Params& p, float amount, float& l, float& r) {
		float in[2] = {l, r}, out[2] = {l, r};
		float col = clampf(p.color, 0.f, 1.f), den = clampf(p.density, 0.f, 1.f);
		switch (p.texture) {
			case T_SHIMMER: {
				for (int c = 0; c < 2; c++) {
					float up = shift[c].process(in[c], 2.f);
					float fifth = shift5[c].process(in[c], 1.5f);
					float s = up + fifth * den * 0.7f;
					texLp[c] += (s - texLp[c]) * kShim;
					out[c] = in[c] + texLp[c] * amount * 0.9f;
				}
				break;
			}
			case T_GRAIN: {
				for (int c = 0; c < 2; c++)
					grainBuf[c].push(in[c]);
				grainClock -= dt;
				if (grainClock <= 0.f) {
					grainClock = 0.12f - 0.1f * den;
					for (auto& gn : grains)
						if (!gn.on) {
							gn.on = true;
							gn.age = 0.f;
							gn.len = (0.04f + 0.3f * col) * fs;
							gn.pos = (0.05f + rng.uni() * (0.2f + 1.5f * den)) * fs;
							gn.rate = rng.uni() < 0.2f * col ? 2.f : 1.f;
							gn.pan = 0.5f + 0.45f * rng.bi();
							break;
						}
				}
				float gl = 0.f, gr = 0.f;
				for (auto& gn : grains) {
					if (!gn.on)
						continue;
					float ph = gn.age / gn.len;
					if (ph >= 1.f) {
						gn.on = false;
						continue;
					}
					float w = sin2pi(0.5f * ph);
					float d = std::max(2.f, gn.pos - gn.age * (gn.rate - 1.f));
					float s = 0.5f * (grainBuf[0].read(d) + grainBuf[1].read(d)) * w * w;
					gl += s * (1.f - gn.pan), gr += s * gn.pan;
					gn.age += 1.f;
				}
				out[0] = in[0] + gl * amount * 1.2f;
				out[1] = in[1] + gr * amount * 1.2f;
				break;
			}
			case T_DUST: {
				// crackles and fewer bits
				if (rng.uni() < (2.f + 80.f * den) * dt)
					dustEnv = 1.f;
				dustEnv *= 1.f - 300.f * dt;
				for (int c = 0; c < 2; c++) {
					float n = rng.bi() * dustEnv;
					dustLp[c] += (n - dustLp[c]) * kDust;
					float q = dustQ;
					float crushed = std::round(in[c] * q) / q;
					out[c] = in[c] + (crushed - in[c]) * amount * 0.7f + dustLp[c] * amount * 0.6f;
				}
				break;
			}
			case T_SMEAR: {
				smearPh = wrap(smearPh + 0.3f * dt);
				static const float D[6] = {0.0071f, 0.0113f, 0.0167f, 0.0233f, 0.0311f, 0.0419f};
				for (int c = 0; c < 2; c++) {
					float x = in[c] + smearFb[c] * den * 0.6f;
					for (int i = 0; i < 6; i++) {
						float d = (D[i] * (c ? 1.07f : 1.f) + 0.0008f * sin2pi(smearPh + 0.17f * i)) * fs;
						float y = smear[i][c].read(d);
						float vv = x + 0.6f * y;
						smear[i][c].push(vv);
						x = y - 0.6f * vv;
					}
					texLp[c] += (x - texLp[c]) * kSmear;
					smearFb[c] = texLp[c];
					out[c] = in[c] * (1.f - 0.6f * amount) + texLp[c] * amount;
				}
				break;
			}
			case T_FOLD: {
				for (int c = 0; c < 2; c++) {
					float y = fold(in[c] * (1.f + 6.f * amount) + (col - 0.5f) * 0.6f);
					texLp[c] += (y - texLp[c]) * kFold;
					out[c] = in[c] * (1.f - amount) + texLp[c] * amount * 0.6f;
				}
				break;
			}
			default: break;
		}
		// WIDTH: mid/side
		float m = 0.5f * (out[0] + out[1]), s = 0.5f * (out[0] - out[1]) * (2.f * clampf(p.width, 0.f, 1.f));
		l = m + s, r = m - s;
	}

	template <typename T>
	static T clamp(T x, T a, T b) {
		return std::min(std::max(x, a), b);
	}
};

}  // namespace nebel
