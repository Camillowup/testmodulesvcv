// SPDX-License-Identifier: GPL-3.0-only
// GLANZ (GENRA Neon Punk, until 2.1.x its own plugin GENRA GLANZ): music visuals. It follows GENRA (right-click › Follow GENRA: GENRA, GENRA CHOP or GENRA TECHNO), so
// every track lights in its colour exactly with its notes (drums violet, bass green, melody cyan, pads pink); the
// audio inputs make it livelier (L/R: the sum, DRUMS … PADS: the tracks) and drive it on their own without a GENRA.
// Nine scenes (the preset field, SCENE CV, NEXT, AUTO BARS). Scenes only ever get appended (patches keep the index).
//
// Threads: the audio thread only listens (envelopes, a ring buffer for the spectrum and the scope, GENRA's notes as
// counters); everything that moves is computed and drawn on the UI thread (the display), at the frame rate.
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/link/GenraPorts.hpp"
#include "../../genra/ui/Look.hpp"
#include "../../genra/ui/MapOverlay.hpp"
#include "../neon/ModuleKit.hpp"
#include "GlanzLayout.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace L = gz;

namespace glanz {

static const int NT = 4; // the tracks: DRUMS, BASS, MELODY, PADS
static const char* const TRACK[NT] = {"DRUMS", "BASS", "MELODY", "PADS"};
static const NVGcolor TCOL[NT] = {nvgRGB(0x7b, 0x6c, 0xff), nvgRGB(0xb8, 0xf2, 0x3a), nvgRGB(0x3d, 0xc9, 0xff), nvgRGB(0xe0, 0x77, 0x9e)};
static const NVGcolor ACC = nvgRGB(0x8f, 0xd3, 0xff);
enum Scene { S_RING, S_BARS, S_SCOPE, S_TUNNEL, S_STARS, S_PLASMA, S_FALL, S_SPARKS, S_DANCE, NUM_SCENES };
static const char* const SCENE[NUM_SCENES] = {"SPECTRUM RING", "BARS", "SCOPE", "TUNNEL", "STARFIELD", "PLASMA", "WATERFALL",
                                              "PARTICLES", "DANCE FLOOR"};
static const int RING = 2048; // the audio ring buffer (samples)

static float frand() { return random::uniform(); }

/** A colour of a track, turned by COLOR (0: the track colours; up: the hue goes round). */
static NVGcolor tcol(int t, float hue, float a = 1.f) {
	NVGcolor c = TCOL[t & 3];
	if (hue > 0.001f) {
		float h, s, l;
		// to HSL and back with the hue turned
		float r = c.r, g = c.g, b = c.b, mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
		l = (mx + mn) / 2;
		float d = mx - mn;
		s = d < 1e-5f ? 0.f : d / (1 - std::fabs(2 * l - 1));
		if (d < 1e-5f)
			h = 0;
		else if (mx == r)
			h = std::fmod((g - b) / d, 6.f) / 6.f;
		else if (mx == g)
			h = ((b - r) / d + 2) / 6.f;
		else
			h = ((r - g) / d + 4) / 6.f;
		c = nvgHSL(std::fmod(h + hue + 1.f, 1.f), s, l);
	}
	c.a = a;
	return c;
}

// (the module and its widgets stay in namespace glanz: other modules of this plugin have a Display too)

struct Glanz : Module {
	enum ParamId {
		AMOUNT_PARAM,
		SMOOTH_PARAM,
		DECAY_PARAM,
		SPEED_PARAM,
		COLOR_PARAM,
		TRAILS_PARAM,
		ZOOM_PARAM,
		GLOW_PARAM,
		AUTO_PARAM,
		ENUMS(TRACK_PARAM, NT),
		FREEZE_PARAM,
		NEXT_PARAM,
		SCENESEL_PARAM, // the scene field, for MIDI-MAP
		UNDO_PARAM,
		INIT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		L_INPUT,
		R_INPUT,
		ENUMS(TRACK_INPUT, NT),
		SCENE_INPUT,
		COLOR_INPUT,
		SPEED_INPUT,
		ZOOM_INPUT,
		AMOUNT_INPUT,
		NEXT_INPUT,
		FREEZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { OUTPUTS_LEN };
	enum LightId { ENUMS(TRACK_LIGHT, NT), FREEZE_LIGHT, NEXT_LIGHT, LIGHTS_LEN };

	// ---- GENRA (UI thread finds it; the audio thread reads its outputs)
	follow::Ref genra;
	std::atomic<int64_t> genraId{-1}; // -1: automatic
	float findT = 0.f;
	bool followOn = true;
	// ---- what the audio thread hears (counters and envelopes; the UI reads them)
	std::atomic<uint32_t> hits[NT];      // note-ons per track
	std::atomic<uint32_t> drumHits[8];   // each drum voice
	std::atomic<float> pitch[NT];        // the last note (V/OCT) per track
	std::atomic<float> vel[NT];          // its velocity 0..1
	std::atomic<int> gates[NT];          // gates high now (pads: up to 4)
	std::atomic<float> padPitch[4];      // the chord
	std::atomic<uint32_t> clocks{0}, resets{0};
	std::atomic<float> bpm{120.f};
	std::atomic<bool> genraLive{false};  // GENRA found and running (its clock ticks)
	std::atomic<float> env{0.f}, band[3], tenv[NT];
	std::atomic<bool> audioIn{false}, trackIn[NT];
	float ring[RING] = {}, ringL[RING] = {}, ringR[RING] = {};
	std::atomic<int> ringPos{0};
	std::atomic<uint32_t> nextReq{0};
	// ---- audio-thread state
	bool lastGate[NT][4] = {};
	bool lastDrum[8] = {};
	bool lastClk = false, lastRst = false;
	int64_t lastClockFrame = 0;
	float lp1 = 0, lp2 = 0, hp1 = 0, envs = 0, bands[3] = {}, tenvs[NT] = {};
	dsp::SchmittTrigger nextTrig, freezeTrig;
	dsp::PulseGenerator nextPulse;
	uint32_t mapCode = 0; // bumped by dataFromJson (the scene field's MIDI-MAP)
	int scene = 0;        // the scene picked (field, NEXT, AUTO); SCENE CV can override it

	Glanz() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(AMOUNT_PARAM, 0.f, 2.f, 1.f, "Amount (how strongly the picture reacts)", "%", 0.f, 100.f);
		configParam(SMOOTH_PARAM, 0.f, 1.f, 0.35f, "Smooth (how calm it moves)", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay (how fast a flash fades)", "%", 0.f, 100.f);
		configParam(SPEED_PARAM, 0.f, 2.f, 1.f, "Speed", "%", 0.f, 100.f);
		configParam(COLOR_PARAM, 0.f, 1.f, 0.f, "Color (0: the track colours, up: the hue turns)", "%", 0.f, 100.f);
		configParam(TRAILS_PARAM, 0.f, 1.f, 0.4f, "Trails (how long things glow on)", "%", 0.f, 100.f);
		configParam(ZOOM_PARAM, 0.5f, 2.f, 1.f, "Zoom", "%", 0.f, 100.f);
		configParam(GLOW_PARAM, 0.f, 1.f, 0.5f, "Glow", "%", 0.f, 100.f);
		configParam(AUTO_PARAM, 0.f, 32.f, 0.f, "Auto: a new scene every … bars (0: never)");
		paramQuantities[AUTO_PARAM]->snapEnabled = true;
		for (int t = 0; t < NT; t++) {
			configSwitch(TRACK_PARAM + t, 0.f, 1.f, 1.f, std::string(TRACK[t]) + " in the picture", {"Off", "On"});
			configInput(TRACK_INPUT + t, std::string(TRACK[t]) + " audio (this track's colour)");
			hits[t] = 0;
			pitch[t] = 0.f;
			vel[t] = 0.f;
			gates[t] = 0;
			tenv[t] = 0.f;
			trackIn[t] = false;
		}
		for (int k = 0; k < 8; k++)
			drumHits[k] = 0;
		for (int k = 0; k < 4; k++)
			padPitch[k] = 0.f;
		for (int b = 0; b < 3; b++)
			band[b] = 0.f;
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze (the picture stands still)", {"Off", "On"});
		configButton(NEXT_PARAM, "Next scene");
		genra_map::configSelect(this, SCENESEL_PARAM, "Scene");
		configButton(UNDO_PARAM, "Undo the last INIT / scene change");
		configButton(INIT_PARAM, "Init: all knobs back, the first scene");
		configInput(L_INPUT, "Audio L (the sum)");
		configInput(R_INPUT, "Audio R");
		configInput(SCENE_INPUT, "Scene CV (0..10 V over the scenes)");
		configInput(COLOR_INPUT, "Color CV (±5 V)");
		configInput(SPEED_INPUT, "Speed CV (±5 V)");
		configInput(ZOOM_INPUT, "Zoom CV (±5 V)");
		configInput(AMOUNT_INPUT, "Amount CV (±5 V)");
		configInput(NEXT_INPUT, "Next scene (trigger)");
		configInput(FREEZE_INPUT, "Freeze (trigger: on / off)");
		for (int i = AMOUNT_PARAM; i < PARAMS_LEN; i++)
			if (i != AUTO_PARAM)
				paramQuantities[i]->randomizeEnabled = false;
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }

	/** The GENRA to follow (UI thread, twice a second): the one picked, else the first GENRA / CHOP / TECHNO. */
	void uiFind(float dt) {
		findT -= dt;
		if (findT > 0.f)
			return;
		findT = 0.5f;
		Module* want = nullptr;
		if (followOn) {
			int64_t id = genraId.load();
			if (id >= 0) {
				Module* m = APP->engine->getModule(id);
				if (follow::isSource(m))
					want = m;
			}
			if (!want) {
				for (int pass = 0; pass < 3 && !want; pass++)
					for (int64_t mid : APP->engine->getModuleIds()) {
						Module* m = APP->engine->getModule(mid);
						if (pass == 0 ? follow::isGenra(m) : pass == 1 ? follow::isGenraChop(m) : follow::isGenraTechno(m)) {
							want = m;
							break;
						}
					}
			}
		}
		if (want != genra.to.load())
			follow::point(genra, this, want);
	}

	void process(const ProcessArgs& args) override {
		// ---- GENRA's notes (as of the last frame)
		bool live = false;
		if (genra.to.load()) {
			int64_t fr = args.frame;
			auto out = [&](int track, int sig) -> const float* {
				follow::Src s = follow::genraOut(track, sig);
				return follow::genraFrame(genra, s.id, fr);
			};
			// drums: the 8 trigger outputs
			const float* d0 = follow::genraFrame(genra, genraports::DRUM_OUTPUT, fr);
			if (d0) {
				for (int k = 0; k < 8; k++) {
					const float* dk = follow::genraFrame(genra, genraports::DRUM_OUTPUT + k, fr);
					bool on = dk && dk[0] > 1.f;
					if (on && !lastDrum[k]) {
						drumHits[k]++;
						hits[0]++;
						vel[0] = clamp(dk[0] / 10.f, 0.3f, 1.f);
						pitch[0] = k / 8.f;
					}
					lastDrum[k] = on;
				}
			}
			// bass, melody: mono; pads: 4 voices
			static const int FT[3] = {follow::T_BASS, follow::T_MEL, follow::T_PAD};
			for (int i = 0; i < 3; i++) {
				int t = i + 1;
				const float* g = out(FT[i], follow::GATE);
				const float* v = out(FT[i], follow::VOCT);
				const float* ve = out(FT[i], follow::VEL);
				if (!g)
					continue;
				int nv = t == 3 ? 4 : 1, high = 0;
				for (int c = 0; c < nv; c++) {
					bool on = g[c] > 1.f;
					high += on;
					if (on && !lastGate[t][c]) {
						hits[t]++;
						if (v)
							pitch[t] = v[c];
						vel[t] = ve ? clamp(ve[0] / 10.f, 0.2f, 1.f) : 0.8f;
					}
					if (t == 3 && v && on)
						padPitch[c] = v[c];
					lastGate[t][c] = on;
				}
				gates[t] = high;
			}
			// the clock (16ths) and reset
			follow::Src cs = follow::genraOut(0, follow::CLOCK), rs = follow::genraOut(0, follow::RESET);
			const float* ck = follow::genraFrame(genra, cs.id, fr);
			const float* rt = follow::genraFrame(genra, rs.id, fr);
			bool c = ck && ck[0] > 1.f, r = rt && rt[0] > 1.f;
			if (c && !lastClk) {
				clocks++;
				lastClockFrame = fr;
			}
			if (r && !lastRst)
				resets++;
			lastClk = c;
			lastRst = r;
			live = fr - lastClockFrame < (int64_t) (args.sampleRate * 1.0f);
			bpm = follow::genraBpm(genra, args.sampleRate);
		}
		genraLive = live;
		// ---- audio: the sum (an envelope, three bands, the ring buffer) and the tracks
		bool aIn = inputs[L_INPUT].isConnected() || inputs[R_INPUT].isConnected();
		audioIn = aIn;
		float l = inputs[L_INPUT].getVoltageSum() / 5.f;
		float rr = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() / 5.f : l;
		float m = 0.5f * (l + rr);
		float kl = 1.f - std::exp(-2.f * M_PI * 150.f * args.sampleTime), kh = 1.f - std::exp(-2.f * M_PI * 2000.f * args.sampleTime);
		lp1 += kl * (m - lp1);
		lp2 += kh * (m - lp2);
		float lo = lp1, mid = lp2 - lp1, hi = m - lp2;
		auto follower = [&](float& e, float x, float att, float rel) {
			x = std::fabs(x);
			e += (x > e ? att : rel) * (x - e);
		};
		float att = 1.f - std::exp(-args.sampleTime / 0.004f), rel = 1.f - std::exp(-args.sampleTime / 0.15f);
		follower(envs, m, att, rel);
		follower(bands[0], lo, att, rel);
		follower(bands[1], mid, att, rel);
		follower(bands[2], hi, att, rel);
		env = envs;
		for (int b = 0; b < 3; b++)
			band[b] = bands[b];
		for (int t = 0; t < NT; t++) {
			bool c = inputs[TRACK_INPUT + t].isConnected();
			trackIn[t] = c;
			if (c)
				follower(tenvs[t], inputs[TRACK_INPUT + t].getVoltageSum() / 5.f, att, rel);
			else
				tenvs[t] = 0.f;
			tenv[t] = tenvs[t];
		}
		int p = ringPos.load(std::memory_order_relaxed);
		ring[p] = m;
		ringL[p] = l;
		ringR[p] = rr;
		ringPos.store((p + 1) & (RING - 1), std::memory_order_relaxed);
		// ---- buttons and triggers
		if (nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
			nextReq++;
		if (freezeTrig.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		for (int t = 0; t < NT; t++)
			lights[TRACK_LIGHT + t].setBrightness(params[TRACK_PARAM + t].getValue() > 0.5f ? 1.f : 0.f);
		lights[FREEZE_LIGHT].setBrightness(params[FREEZE_PARAM].getValue() > 0.5f ? 1.f : 0.f);
		lights[NEXT_LIGHT].setBrightnessSmooth(nextPulse.process(args.sampleTime) ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "scene", json_integer(scene));
		json_object_set_new(r, "follow", json_boolean(followOn));
		json_object_set_new(r, "genraId", json_integer(genraId.load()));
		return r;
	}
	void dataFromJson(json_t* r) override {
		mapCode++;
		if (json_t* j = json_object_get(r, "scene"))
			scene = clamp((int) json_integer_value(j), 0, NUM_SCENES - 1);
		followOn = !json_is_false(json_object_get(r, "follow"));
		if (json_t* j = json_object_get(r, "genraId"))
			genraId = json_integer_value(j);
		findT = 0.f;
	}
	void onReset() override {
		scene = 0;
		findT = 0.f;
	}
};

// ================================================================ the display: everything that moves
struct Display : widget::Widget {
	Glanz* m = nullptr;
	// what is seen (UI thread)
	uint32_t seenHits[NT] = {}, seenDrum[8] = {}, seenClk = 0, seenRst = 0, seenNext = 0;
	float flash[NT] = {};   // 0..1, the last note of each track
	float level[NT] = {};   // how much each track plays now (smoothed)
	float pitchPos[NT] = {}; // the last note's height 0..1
	float drumFlash[8] = {};
	float kick = 0.f;       // the kick's punch (drives pulses)
	float t = 0.f;          // the picture's time (SPEED; FREEZE stops it)
	int step16 = 0;         // GENRA's 16ths since reset
	int barsInScene = 0, lastBar = -1;
	float spec[64] = {};    // the spectrum (smoothed)
	float specPeak[48] = {};
	// scene state
	struct Star {
		float x, y, z;
		int col; // -1 white
	};
	std::vector<Star> stars;
	struct Spark {
		float x, y, vx, vy, life;
		int col;
	};
	std::vector<Spark> sparks;
	struct Note {
		int track;
		float x, age, len;
	};
	std::vector<Note> notes;
	float tunnelZ = 0.f;
	struct Dancer {
		int pose = 0, last = 0;
		float blend = 1.f, jump = 0.f;
	};
	Dancer dancers[NT];
	float spot = 0.f;
	int tileSeed = 1;

	int sceneNow() {
		if (!m)
			return 0;
		if (m->inputs[Glanz::SCENE_INPUT].isConnected())
			return clamp((int) (m->inputs[Glanz::SCENE_INPUT].getVoltage() / 10.f * NUM_SCENES), 0, NUM_SCENES - 1);
		return m->scene;
	}
	float cv(int in) { return m && m->inputs[in].isConnected() ? m->inputs[in].getVoltage() / 5.f : 0.f; }
	float amount() { return m ? clamp(m->params[Glanz::AMOUNT_PARAM].getValue() + cv(Glanz::AMOUNT_INPUT), 0.f, 3.f) : 1.f; }
	float speed() { return m ? clamp(m->params[Glanz::SPEED_PARAM].getValue() + cv(Glanz::SPEED_INPUT), 0.f, 3.f) : 1.f; }
	float zoom() { return m ? clamp(m->params[Glanz::ZOOM_PARAM].getValue() + 0.5f * cv(Glanz::ZOOM_INPUT), 0.3f, 3.f) : 1.f; }
	float hue() { return m ? clamp(m->params[Glanz::COLOR_PARAM].getValue() + 0.5f * cv(Glanz::COLOR_INPUT), 0.f, 1.f) : 0.f; }
	float glow() { return m ? m->params[Glanz::GLOW_PARAM].getValue() : 0.5f; }
	float trails() { return m ? m->params[Glanz::TRAILS_PARAM].getValue() : 0.4f; }
	bool on(int k) { return !m || m->params[Glanz::TRACK_PARAM + k].getValue() > 0.5f; }
	NVGcolor col(int k, float a = 1.f) { return tcol(k, hue(), a); }

	/** A small FFT (radix 2) of the newest 512 samples into 64 log-spaced bins. */
	void spectrum() {
		static const int N = 512;
		std::vector<std::complex<float>> x(N);
		int p = m->ringPos.load(std::memory_order_relaxed);
		for (int i = 0; i < N; i++) {
			float w = 0.5f - 0.5f * std::cos(2 * M_PI * i / (N - 1));
			x[i] = m->ring[(p - N + i + RING) & (RING - 1)] * w;
		}
		for (int i = 1, j = 0; i < N; i++) { // bit reversal
			int bit = N >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(x[i], x[j]);
		}
		for (int len = 2; len <= N; len <<= 1) {
			std::complex<float> wl = std::polar(1.f, (float) (-2 * M_PI / len));
			for (int i = 0; i < N; i += len) {
				std::complex<float> w(1.f, 0.f);
				for (int k = 0; k < len / 2; k++) {
					std::complex<float> u = x[i + k], v = x[i + k + len / 2] * w;
					x[i + k] = u + v;
					x[i + k + len / 2] = u - v;
					w *= wl;
				}
			}
		}
		float sm = m->params[Glanz::SMOOTH_PARAM].getValue();
		for (int b = 0; b < 64; b++) {
			int a = (int) (2 * std::pow(N / 2 / 2.f, b / 64.f)), e = std::max(a + 1, (int) (2 * std::pow(N / 2 / 2.f, (b + 1) / 64.f)));
			float s = 0.f;
			for (int k = a; k < e && k < N / 2; k++)
				s = std::max(s, std::abs(x[k]));
			float v = clamp(std::log10(1.f + 9.f * s / 8.f), 0.f, 1.f);
			spec[b] += (v - spec[b]) * (v > spec[b] ? 0.6f : 0.08f + 0.3f * (1 - sm));
		}
	}

	void step() override {
		Widget::step();
		if (!m)
			return;
		float dt = APP->window->getLastFrameDuration();
		dt = clamp(dt, 0.f, 0.1f);
		m->uiFind(dt);
		bool frozen = m->params[Glanz::FREEZE_PARAM].getValue() > 0.5f;
		float dec = m->params[Glanz::DECAY_PARAM].getValue();
		float fall = std::exp(-dt * (1.5f + 10.f * (1.f - dec)));
		float am = amount();
		bool live = m->genraLive.load();
		// GENRA's notes
		for (int k = 0; k < NT; k++) {
			uint32_t h = m->hits[k].load();
			int nNew = (int) (h - seenHits[k]);
			seenHits[k] = h;
			float p = m->pitch[k].load();
			pitchPos[k] = k == 0 ? p : clamp((p + 3.f) / 6.f, 0.f, 1.f);
			if (nNew > 0 && on(k)) {
				flash[k] = std::max(flash[k], m->vel[k].load());
				onNote(k);
			}
			flash[k] *= fall;
		}
		for (int d = 0; d < 8; d++) {
			uint32_t h = m->drumHits[d].load();
			if (h != seenDrum[d] && on(0)) {
				drumFlash[d] = 1.f;
				if (d == 0)
					kick = 1.f;
			}
			seenDrum[d] = h;
			drumFlash[d] *= fall;
		}
		kick *= std::exp(-dt * 8.f);
		// the levels: GENRA's flashes, the track inputs, else the bands of the sum
		float sm = m->params[Glanz::SMOOTH_PARAM].getValue();
		float k1 = 1.f - std::exp(-dt * (4.f + 30.f * (1.f - sm)));
		for (int k = 0; k < NT; k++) {
			float v = flash[k];
			if (m->trackIn[k].load())
				v = std::max(v, clamp(m->tenv[k].load() * 1.6f, 0.f, 1.f));
			else if (!live && m->audioIn.load()) {
				float b = k == 0 ? m->band[2].load() * 3.f + m->band[0].load() : k == 1 ? m->band[0].load() * 2.f
				                                                                    : k == 2 ? m->band[1].load() * 2.5f : m->band[1].load() * 1.5f;
				v = std::max(v, clamp(b, 0.f, 1.f));
				if (k == 0 && b > 0.6f && kick < 0.2f)
					kick = 1.f; // a beat from the sum
			}
			if (!on(k))
				v = 0.f;
			level[k] += (clamp(v * am, 0.f, 1.5f) - level[k]) * k1;
		}
		if (m->audioIn.load())
			spectrum();
		else
			for (float& s : spec)
				s *= 0.9f;
		// bars, the clock, AUTO
		uint32_t rs = m->resets.load();
		if (rs != seenRst) {
			seenRst = rs;
			step16 = 0;
		}
		uint32_t ck = m->clocks.load();
		step16 += (int) (ck - seenClk);
		seenClk = ck;
		int bar = step16 / 16;
		if (bar != lastBar) {
			if (lastBar >= 0) {
				barsInScene++;
				spot += 1.f;
				tileSeed++;
				int autoBars = (int) m->params[Glanz::AUTO_PARAM].getValue();
				if (autoBars > 0 && barsInScene >= autoBars && !frozen)
					nextScene();
			}
			lastBar = bar;
		}
		uint32_t nx = m->nextReq.load();
		if (nx != seenNext) {
			seenNext = nx;
			nextScene();
		}
		if (frozen)
			return;
		float sp = speed();
		float bps = (live ? m->bpm.load() : 120.f) / 60.f;
		t += dt * sp;
		tunnelZ += dt * sp * (0.6f + 2.5f * kick) * bps * 0.5f;
		// particles live on
		float life = 0.4f + 2.6f * trails();
		for (Spark& s : sparks) {
			s.x += s.vx * dt * sp;
			s.y += s.vy * dt * sp;
			s.vy += 0.35f * dt * sp; // gravity (in heights per second²)
			s.life -= dt / life;
		}
		sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0.f; }), sparks.end());
		for (Star& s : stars)
			s.z -= dt * sp * (0.25f + 0.15f * bps + 0.8f * kick);
		stars.erase(std::remove_if(stars.begin(), stars.end(), [](const Star& s) { return s.z <= 0.02f; }), stars.end());
		while (stars.size() < 160)
			stars.push_back({frand() * 2 - 1, frand() * 2 - 1, 0.3f + frand() * 0.7f, -1});
		float fallSpeed = 0.12f * sp * bps / 2.f;
		for (Note& n : notes)
			n.age += dt * fallSpeed;
		notes.erase(std::remove_if(notes.begin(), notes.end(), [](const Note& n) { return n.age > 1.2f; }), notes.end());
		for (Dancer& d : dancers) {
			d.blend = std::min(1.f, d.blend + dt * sp * 9.f);
			d.jump *= std::exp(-dt * 5.f);
		}
		if (!live && m->audioIn.load() && kick > 0.95f) // without GENRA: the sum's beats make sparks and moves too
			for (int k = 0; k < NT; k++)
				if (level[k] > 0.3f && frand() < 0.5f)
					onNote(k);
	}
	void nextScene() {
		if (!m)
			return;
		m->scene = (m->scene + 1) % NUM_SCENES;
		m->nextPulse.trigger(0.2f);
		barsInScene = 0;
	}
	/** A note of track k began: stars, sparks, a note in the waterfall, a dancer's move. */
	void onNote(int k) {
		float v = std::max(0.3f, flash[k]);
		if (stars.size() < 400)
			for (int i = 0; i < 2 + (int) (4 * v); i++)
				stars.push_back({(frand() * 2 - 1) * 0.6f, (frand() * 2 - 1) * 0.6f, 0.9f + frand() * 0.1f, k});
		float bx = 0.15f + 0.7f * frand(), by = 0.25f + 0.45f * frand();
		if (k == 1)
			by = 0.75f - 0.5f * pitchPos[k];
		if (sparks.size() < 1200)
			for (int i = 0; i < 14 + (int) (30 * v); i++) {
				float a = frand() * 2 * M_PI, s = (0.05f + 0.35f * frand()) * (0.6f + v);
				sparks.push_back({bx, by, s * std::cos(a) * 0.6f, s * std::sin(a) - 0.15f, 1.f, k});
			}
		if (notes.size() < 600)
			notes.push_back({k, pitchPos[k] * 0.85f + 0.05f * frand(), 0.f, k == 3 ? 0.06f : k == 0 ? 0.015f : 0.03f});
		Dancer& d = dancers[k];
		d.last = d.pose;
		d.pose = (d.pose + 1 + (int) (frand() * 3)) % 4;
		d.blend = 0.f;
		if (k == 2)
			d.jump = 0.4f + 0.6f * pitchPos[k];
	}

	// ---------------------------------------------------------------- drawing helpers
	void line(NVGcontext* vg, float x1, float y1, float x2, float y2, NVGcolor c, float w) {
		float g = glow();
		if (g > 0.02f) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, x1, y1);
			nvgLineTo(vg, x2, y2);
			NVGcolor cg = c;
			cg.a *= 0.25f * g;
			nvgStrokeColor(vg, cg);
			nvgStrokeWidth(vg, w * (2.5f + 3.f * g));
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
		}
		nvgBeginPath(vg);
		nvgMoveTo(vg, x1, y1);
		nvgLineTo(vg, x2, y2);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, w);
		nvgLineCap(vg, NVG_ROUND);
		nvgStroke(vg);
	}
	void dot(NVGcontext* vg, float x, float y, float r, NVGcolor c) {
		float g = glow();
		if (g > 0.02f) {
			NVGcolor a = c, b = c;
			a.a *= 0.5f * g;
			b.a = 0.f;
			nvgBeginPath(vg);
			nvgCircle(vg, x, y, r * (2.f + 3.f * g));
			nvgFillPaint(vg, nvgRadialGradient(vg, x, y, r * 0.5f, r * (2.f + 3.f * g), a, b));
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, r);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
	void haze(NVGcontext* vg, float x, float y, float r, NVGcolor c, float a) {
		NVGcolor i = c, o = c;
		i.a = a;
		o.a = 0.f;
		nvgBeginPath(vg);
		nvgRect(vg, x - r, y - r, 2 * r, 2 * r);
		nvgFillPaint(vg, nvgRadialGradient(vg, x, y, 0.f, r, i, o));
		nvgFill(vg);
	}
	/** Each track's bar k of n: its level, shaped round the height of its last note, with the spectrum as texture. */
	float barOf(int track, int k, int n) {
		float u = (k + 0.5f) / n;
		float shape = 0.35f + 0.65f * std::exp(-std::pow((u - pitchPos[track]) * 3.2f, 2.f));
		float tex = spec[clamp(track * 16 + k * 16 / std::max(1, n), 0, 63)];
		return clamp(level[track] * shape * (0.8f + 0.4f * std::sin(t * 3.f + k * 1.7f + track)) + 0.5f * tex, 0.f, 1.2f);
	}

	// ---------------------------------------------------------------- the scenes
	void drawRing(NVGcontext* vg, float W, float H) {
		float cx = W / 2, cy = H / 2, z = zoom(), r0 = H * 0.18f * z * (1.f + 0.12f * kick);
		haze(vg, cx, cy, H * 0.6f * z, ACC, 0.18f + 0.2f * kick);
		int N = 72;
		for (int i = 0; i < N; i++) {
			int tr = i * NT / N;
			float a = 2 * M_PI * i / N - M_PI / 2 + t * 0.1f;
			float v = (0.08f + 0.9f * barOf(tr, i % (N / NT), N / NT)) * H * 0.3f * z;
			line(vg, cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + (r0 + v) * std::cos(a), cy + (r0 + v) * std::sin(a), col(tr), 2.2f);
		}
		// the inner wave (the sum, or the bass)
		nvgBeginPath(vg);
		int p = m->ringPos.load(std::memory_order_relaxed);
		for (int i = 0; i <= 120; i++) {
			float a = 2 * M_PI * i / 120;
			float s = m->audioIn.load() ? m->ring[(p - 600 + i * 5 + RING) & (RING - 1)] : level[1] * std::sin(a * 6 + t * 4) * 0.5f;
			float rr = r0 * (0.8f + 0.25f * clamp(s, -1.f, 1.f));
			float x = cx + rr * std::cos(a), y = cy + rr * std::sin(a);
			if (i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.7f));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		dot(vg, cx, cy, r0 * (0.3f + 0.25f * kick + 0.2f * level[0]), col(0));
	}
	void drawBars(NVGcontext* vg, float W, float H) {
		int N = 48;
		float bw = (W - 16) / N, base = H - 8;
		for (int i = 0; i < N; i++) {
			int tr = i / (N / NT);
			float v = barOf(tr, i % (N / NT), N / NT) * (H - 30) * zoom();
			v = std::min(v, H - 22);
			specPeak[i] = std::max(specPeak[i] - (H * 0.004f), v);
			NVGcolor c = col(tr);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 8 + i * bw, base - v, bw * 0.72f, v, 1.f);
			nvgFillPaint(vg, nvgLinearGradient(vg, 0, base - v, 0, base, c, tcol(tr, hue(), 0.55f)));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, 8 + i * bw, base - specPeak[i] - 3, bw * 0.72f, 1.5f);
			nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.85f));
			nvgFill(vg);
			nvgBeginPath(vg); // the floor's reflection
			nvgRect(vg, 8 + i * bw, base + 1, bw * 0.72f, v * 0.12f);
			nvgFillColor(vg, tcol(tr, hue(), 0.15f));
			nvgFill(vg);
		}
	}
	void drawScope(NVGcontext* vg, float W, float H) {
		float cx = W / 2, cy = H / 2, z = zoom();
		float shake = 3.f * (kick + 0.5f * level[0]);
		if (m->audioIn.load()) { // the sum as XY
			nvgBeginPath(vg);
			int p = m->ringPos.load(std::memory_order_relaxed);
			for (int i = 0; i < 1024; i++) {
				int k = (p - 1024 + i + RING) & (RING - 1);
				float x = cx + m->ringL[k] * H * 0.35f * z, y = cy - m->ringR[k] * H * 0.35f * z;
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.35f));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
		}
		// figures: bass, melody and pads (their notes choose the ratios), the drums shake them
		static const int TR[3] = {1, 2, 3};
		for (int f = 0; f < 3; f++) {
			int tr = TR[f];
			if (!on(tr))
				continue;
			float a = 1 + (int) (pitchPos[tr] * 4), b = a + 1 + f;
			float s = H * (0.18f + 0.22f * level[tr]) * z;
			NVGcolor c = col(tr, 0.3f + 0.7f * clamp(level[tr] + 0.15f, 0.f, 1.f));
			for (int pass = 0; pass < 2; pass++) {
				nvgBeginPath(vg);
				for (int i = 0; i <= 300; i++) {
					float u = 2 * M_PI * i / 300;
					float x = cx + s * 1.5f * std::sin(a * u + t * (0.3f + 0.2f * f)) + shake * (frand() - 0.5f);
					float y = cy + s * std::sin(b * u) + shake * (frand() - 0.5f);
					if (i == 0)
						nvgMoveTo(vg, x, y);
					else
						nvgLineTo(vg, x, y);
				}
				NVGcolor cc = c;
				if (pass == 0)
					cc.a *= 0.25f * glow();
				nvgStrokeColor(vg, cc);
				nvgStrokeWidth(vg, pass == 0 ? 5.f : 1.3f);
				nvgStroke(vg);
			}
		}
		dot(vg, cx, cy, 3.f + 6.f * kick, col(0));
	}
	void drawTunnel(NVGcontext* vg, float W, float H) {
		float cx = W / 2, cy = H / 2, z = zoom();
		float pad = level[3];
		for (int j = 0; j < 8; j++) { // the rails
			float a = 2 * M_PI * j / 8 + t * 0.2f;
			line(vg, cx, cy, cx + W * std::cos(a), cy + H * std::sin(a), tcol(2, hue(), 0.25f), 0.6f);
		}
		for (int k = 18; k >= 1; k--) {
			float d = std::fmod(k - tunnelZ, 18.f);
			if (d < 0.2f)
				continue;
			float s = 6.f * z * (18.f / d) * (18.f / d) * 0.25f;
			int tr = ((int) std::floor(k - tunnelZ) % 4 + 4) % 4;
			float bright = clamp(0.25f + level[tr] + 0.3f * pad, 0.f, 1.f) * clamp(d / 3.f, 0.f, 1.f);
			nvgBeginPath(vg);
			for (int j = 0; j <= 8; j++) {
				float a = 2 * M_PI * j / 8 + d * 0.08f + t * 0.2f;
				float x = cx + s * 1.6f * std::cos(a), y = cy + s * std::sin(a);
				if (j == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			NVGcolor c = col(tr, bright);
			nvgStrokeColor(vg, c);
			nvgStrokeWidth(vg, 0.8f + 2.5f * (1.f / d) * 6.f);
			nvgStroke(vg);
		}
		dot(vg, cx, cy, 3.f + 5.f * kick, nvgRGB(255, 255, 255));
	}
	void drawStars(NVGcontext* vg, float W, float H) {
		float cx = W / 2, cy = H / 2, z = zoom();
		for (const Star& s : stars) {
			float k = 0.5f / s.z * z;
			float x = cx + s.x * W * k * 0.5f, y = cy + s.y * H * k * 0.5f;
			float k2 = 0.5f / (s.z + 0.05f) * z;
			float x2 = cx + s.x * W * k2 * 0.5f, y2 = cy + s.y * H * k2 * 0.5f;
			if (x < -10 || x > W + 10 || y < -10 || y > H + 10)
				continue;
			NVGcolor c = s.col < 0 ? nvgRGBAf(1, 1, 1, clamp(1.2f - s.z, 0.2f, 1.f)) : col(s.col, clamp(1.3f - s.z, 0.3f, 1.f));
			line(vg, x2, y2, x, y, c, s.col < 0 ? 0.8f : 1.6f);
		}
	}
	void drawPlasma(NVGcontext* vg, float W, float H) {
		static const int BL = 10;
		for (int i = 0; i < BL; i++) {
			int tr = i % 4 == 0 ? 3 : (i % 4 == 1 ? 2 : (i % 4 == 2 ? 0 : 1));
			if (i > 5)
				tr = 3;
			float x = W * (0.5f + 0.42f * std::sin(t * (0.13f + 0.03f * i) + i * 2.1f));
			float y = H * (0.5f + 0.4f * std::cos(t * (0.11f + 0.02f * i) + i * 1.3f));
			float r = H * zoom() * (0.3f + 0.35f * level[tr] + 0.1f * std::sin(t + i));
			haze(vg, x, y, r, col(tr), 0.35f + 0.4f * level[tr]);
		}
		float amp = H * 0.03f * (1.f + 3.f * level[1] + 2.f * kick);
		for (int k = 0; k < 5; k++) {
			nvgBeginPath(vg);
			for (int j = 0; j <= 80; j++) {
				float x = W * j / 80.f, y = H * (0.2f + 0.15f * k) + amp * std::sin(j * 0.25f + k + t * 2.f);
				if (j == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.12f + 0.25f * level[0]));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
		}
	}
	void drawFall(NVGcontext* vg, float W, float H) {
		static const float LANE[NT][2] = {{0.03f, 0.24f}, {0.26f, 0.45f}, {0.47f, 0.72f}, {0.74f, 0.97f}};
		for (int k = 0; k < NT; k++) {
			nvgBeginPath(vg);
			nvgRect(vg, W * LANE[k][0], 0, W * (LANE[k][1] - LANE[k][0]), H);
			nvgFillColor(vg, col(k, 0.04f + 0.08f * level[k]));
			nvgFill(vg);
		}
		for (const Note& n : notes) {
			float x = W * (LANE[n.track][0] + (LANE[n.track][1] - LANE[n.track][0] - 0.04f) * n.x);
			float y = H * n.age, w = W * (0.02f + n.len * 0.4f), h = 4.f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x, y, w, h, 1.5f);
			nvgFillColor(vg, col(n.track, clamp(1.f - n.age * 0.8f, 0.1f, 1.f)));
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, W, 3);
		nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.12f + 0.3f * kick));
		nvgFill(vg);
	}
	void drawSparks(NVGcontext* vg, float W, float H) {
		for (const Spark& s : sparks) {
			float x = s.x * W, y = s.y * H;
			NVGcolor c = col(s.col, clamp(s.life, 0.f, 1.f));
			line(vg, x - s.vx * W * 0.05f, y - s.vy * H * 0.05f, x, y, tcol(s.col, hue(), 0.35f * s.life), 1.f);
			nvgBeginPath(vg);
			nvgCircle(vg, x, y, 1.f + 1.5f * s.life);
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
	}
	// ---- the dance floor
	/** A pose: joint points (x right, y up, in units of a figure ~ 40 high): head, neck, hip, hands, elbows, knees, feet. */
	struct Pose {
		float p[11][2];
	};
	static const Pose& pose(int track, int i) {
		// for every track four moves (drums: headspin / freezes, bass: robot, melody: jumps, pads: windmill / freezes)
		static const Pose P[NT][4] = {
		    {{{{0, 3}, {0, 6}, {0, 15}, {-6, 1}, {-6, 5}, {6, 1}, {6, 6}, {-7, 21}, {-9, 27}, {6, 22}, {9, 28}}},
		     {{{0, 3}, {0, 6}, {0, 15}, {-7, 0}, {-6, 5}, {7, 0}, {6, 5}, {-8, 18}, {-14, 22}, {8, 18}, {14, 22}}},
		     {{{-3, 6}, {0, 8}, {9, 11}, {-2, 0}, {-2, 4}, {4, 19}, {4, 14}, {15, 17}, {20, 22}, {17, 9}, {23, 12}}},
		     {{{0, 3}, {0, 6}, {0, 15}, {-6, 1}, {-6, 5}, {6, 1}, {6, 6}, {-3, 22}, {4, 29}, {3, 22}, {-4, 29}}}},
		    {{{{0, 36}, {0, 32}, {0, 18}, {-8, 38}, {-8, 30}, {8, 24}, {8, 30}, {-3, 9}, {-3, 0}, {3, 9}, {5, 0}}},
		     {{{0, 36}, {0, 32}, {0, 18}, {-8, 24}, {-8, 30}, {8, 38}, {8, 30}, {-4, 9}, {-6, 0}, {3, 9}, {3, 0}}},
		     {{{1, 35}, {0, 31}, {0, 17}, {-12, 31}, {-6, 31}, {12, 31}, {6, 31}, {-4, 8}, {-4, 0}, {4, 8}, {4, 0}}},
		     {{{-1, 34}, {0, 30}, {0, 16}, {-8, 20}, {-7, 25}, {9, 36}, {8, 29}, {-6, 8}, {-8, 0}, {5, 9}, {7, 0}}}},
		    {{{{0, 40}, {0, 36}, {0, 22}, {-7, 48}, {-6, 42}, {7, 48}, {6, 42}, {-5, 14}, {-3, 6}, {5, 13}, {9, 9}}},
		     {{{0, 40}, {0, 36}, {0, 22}, {-10, 44}, {-7, 40}, {10, 44}, {7, 40}, {-6, 12}, {-8, 4}, {6, 12}, {8, 4}}},
		     {{{0, 38}, {0, 34}, {0, 20}, {-12, 38}, {-7, 36}, {9, 46}, {6, 40}, {-4, 10}, {-2, 2}, {6, 13}, {12, 10}}},
		     {{{0, 38}, {0, 34}, {0, 20}, {-6, 44}, {-6, 38}, {11, 36}, {7, 34}, {-3, 10}, {-5, 1}, {4, 10}, {6, 1}}}},
		    {{{{-3, 6}, {0, 8}, {9, 11}, {-2, 0}, {-2, 4}, {4, 19}, {4, 14}, {15, 17}, {20, 22}, {17, 9}, {23, 12}}},
		     {{{0, 4}, {2, 7}, {10, 14}, {-5, 0}, {-3, 4}, {-4, 14}, {-2, 10}, {16, 22}, {22, 30}, {18, 18}, {26, 22}}},
		     {{{3, 6}, {0, 8}, {-9, 11}, {2, 0}, {2, 4}, {-4, 19}, {-4, 14}, {-15, 17}, {-20, 22}, {-17, 9}, {-23, 12}}},
		     {{{0, 34}, {0, 30}, {0, 17}, {-8, 30}, {-6, 25}, {8, 30}, {6, 25}, {-6, 8}, {-9, 1}, {6, 8}, {9, 1}}}}};
		return P[track & 3][i & 3];
	}
	/** A figure between two poses (u 0..1), lifted; plain strokes in one colour (w: line width in figure units). */
	void figure(NVGcontext* vg, float x, float foot, float s, int tr, int pa, int pb, float u, float lift, NVGcolor c, float w,
	            float flip = 1.f, bool mirror = false) {
		const Pose& a = pose(tr, pa);
		const Pose& b = pose(tr, pb);
		auto P = [&](int i) {
			float px = a.p[i][0] + (b.p[i][0] - a.p[i][0]) * u, py = a.p[i][1] + (b.p[i][1] - a.p[i][1]) * u;
			float y = (py + lift) * s;
			return Vec(x + flip * px * s, mirror ? foot + y : foot - y);
		};
		static const int SEG[][2] = {{1, 2}, {1, 4}, {4, 3}, {1, 6}, {6, 5}, {2, 7}, {7, 8}, {2, 9}, {9, 10}};
		nvgBeginPath(vg);
		for (auto& sg : SEG) {
			Vec p1 = P(sg[0]), p2 = P(sg[1]);
			nvgMoveTo(vg, p1.x, p1.y);
			nvgLineTo(vg, p2.x, p2.y);
		}
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, w * s);
		nvgLineCap(vg, NVG_ROUND);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
		Vec h = P(0);
		nvgBeginPath(vg);
		nvgCircle(vg, h.x, h.y, (3.2f + 0.4f * (w - 2.6f)) * s);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
	/** A main dancer: shadow, trails (TRAILS), mirror image on the floor, dark outline, neon line, white core. */
	void drawFigure(NVGcontext* vg, float x, float foot, float s, int tr, const Dancer& d) {
		float u = d.blend * d.blend * (3 - 2 * d.blend);
		float lift = d.jump * 18.f;
		NVGcolor c = col(tr, clamp(0.55f + level[tr], 0.f, 1.f));
		// shadow on the floor
		nvgBeginPath(vg);
		nvgEllipse(vg, x, foot + 2 * s, 20 * s, 4 * s);
		nvgFillColor(vg, nvgRGBAf(0.02f, 0.01f, 0.05f, 0.7f));
		nvgFill(vg);
		// mirror image in the tiles
		figure(vg, x, foot, s, tr, d.last, d.pose, u, lift, alpha(c, 0.16f), 2.6f, 1.f, true);
		// trails: where it was a moment ago
		float tl = trails();
		if (tl > 0.02f && d.blend < 1.f)
			for (int k = 3; k >= 1; k--) {
				float uk = std::max(0.f, d.blend - 0.22f * k);
				uk = uk * uk * (3 - 2 * uk);
				figure(vg, x - k * 9.f * s * tl, foot, s, tr, d.last, d.pose, uk, lift, alpha(c, 0.13f * (4 - k) * tl), 2.6f);
			}
		// outline, neon, core
		figure(vg, x, foot, s, tr, d.last, d.pose, u, lift, nvgRGBAf(0.02f, 0.01f, 0.05f, 0.95f), 5.2f);
		float g = glow();
		if (g > 0.02f)
			figure(vg, x, foot, s, tr, d.last, d.pose, u, lift, alpha(c, 0.3f * g), 3.2f + 5.f * g);
		figure(vg, x, foot, s, tr, d.last, d.pose, u, lift, c, 3.2f);
		figure(vg, x, foot, s, tr, d.last, d.pose, u, lift, nvgRGBAf(1, 1, 1, 0.85f), 1.1f);
	}
	static NVGcolor alpha(NVGcolor c, float a) {
		c.a *= a;
		return c;
	}
	void drawDance(NVGcontext* vg, float W, float H) {
		float hz = H * 0.50f;
		const NVGcolor DARK = nvgRGB(0x08, 0x04, 0x1a), YEL = nvgRGB(0xff, 0xe1, 0x4d), PINKC = nvgRGB(0xff, 0x2b, 0xd6),
		               CYANC = nvgRGB(0x22, 0xe6, 0xff);
		int bar = step16 / 16, beat = step16 / 4;
		// ---- the LED wall: an equalizer in the colours of the tracks
		{
			const int COLS = 50, ROWS = 14;
			float cw = W / COLS, rs = hz * 0.86f / ROWS;
			for (int i = 0; i < COLS; i++) {
				int k = (i / 3) % NT;
				int lit = on(k) ? (int) (clamp(barOf(k, i, COLS), 0.f, 1.f) * ROWS) : 0;
				for (int pass = 0; pass < 2; pass++) {
					nvgBeginPath(vg);
					for (int j = 0; j < ROWS; j++)
						if ((j < lit) == (pass == 1))
							nvgRoundedRect(vg, i * cw + 1, hz - 4 - (j + 1) * rs, cw - 3, rs * 0.72f, 1.2f);
					nvgFillColor(vg, col(k, pass ? 0.42f : 0.05f));
					nvgFill(vg);
				}
			}
		}
		// ---- the speaker stacks (they pump with the kick)
		for (int side = 0; side < 2; side++) {
			float bw = W * 0.06f, bh = H * 0.19f, bx = side ? W * 0.94f - bw : W * 0.04f, by = hz - bh + H * 0.01f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, bx, by, bw, bh, 3);
			nvgFillColor(vg, nvgRGB(0x12, 0x0a, 0x26));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGB(0x8b, 0x5b, 0xff));
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
			for (int c = 0; c < 2; c++) {
				float r = bw * (c ? 0.3f : 0.22f) * (1.f + 0.14f * kick), cx = bx + bw / 2, cy = by + bh * (c ? 0.68f : 0.27f);
				nvgBeginPath(vg);
				nvgCircle(vg, cx, cy, r);
				nvgFillColor(vg, nvgRGB(0x05, 0x02, 0x0c));
				nvgFill(vg);
				nvgStrokeColor(vg, col(0));
				nvgStrokeWidth(vg, 2.f);
				nvgStroke(vg);
				dot(vg, cx, cy, r * 0.35f, col(0, 0.6f + 0.4f * kick));
			}
		}
		// ---- the DJ booth and the DJ
		{
			float bw = W * 0.18f, bh = H * 0.1f, bx = W / 2 - bw / 2, by = hz - bh;
			for (int dk = 0; dk < 2; dk++) { // the decks turn
				float cx = W / 2 + (dk ? 1 : -1) * bw * 0.3f, cy = by - H * 0.015f, r = bh * 0.22f, a = t * 3.f + dk;
				nvgBeginPath(vg);
				nvgCircle(vg, cx, cy, r);
				nvgFillColor(vg, nvgRGB(0x1d, 0x12, 0x38));
				nvgFill(vg);
				nvgStrokeColor(vg, col(1));
				nvgStrokeWidth(vg, 1.6f);
				nvgStroke(vg);
				nvgBeginPath(vg);
				nvgMoveTo(vg, cx, cy);
				nvgLineTo(vg, cx + r * std::cos(a), cy + r * std::sin(a));
				nvgStroke(vg);
			}
			// the DJ: the head nods with the kick, one hand goes up every other bar
			float s = H / 150.f, dx = W / 2, foot = by + bh * 0.2f, nod = 2.5f * kick;
			NVGcolor c = YEL;
			nvgBeginPath(vg);
			nvgMoveTo(vg, dx, foot - 26 * s);
			nvgLineTo(vg, dx, foot - 10 * s);
			nvgMoveTo(vg, dx, foot - 24 * s);
			nvgLineTo(vg, dx - 9 * s, foot - 16 * s);
			nvgLineTo(vg, dx - 14 * s, foot - 12 * s);
			nvgMoveTo(vg, dx, foot - 24 * s);
			bool up = bar % 2 == 1;
			nvgLineTo(vg, dx + 8 * s, up ? foot - 34 * s : foot - 16 * s);
			nvgLineTo(vg, dx + (up ? 14 : 15) * s, up ? foot - 44 * s : foot - 12 * s);
			nvgStrokeColor(vg, c);
			nvgStrokeWidth(vg, 2.6f * s);
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
			dot(vg, dx, foot - (31 - nod) * s, 4.2f * s, c);
			nvgBeginPath(vg); // headphones
			nvgArc(vg, dx, foot - (31 - nod) * s, 5.6f * s, M_PI, 2 * M_PI, NVG_CW);
			nvgStrokeColor(vg, PINKC);
			nvgStrokeWidth(vg, 1.6f * s);
			nvgStroke(vg);
			nvgBeginPath(vg);
			nvgRect(vg, bx, by, bw, bh);
			nvgFillColor(vg, nvgRGB(0x12, 0x0a, 0x26));
			nvgFill(vg);
			nvgStrokeColor(vg, PINKC);
			nvgStrokeWidth(vg, 1.6f);
			nvgStroke(vg);
			nvgBeginPath(vg);
			nvgRect(vg, bx + 3, by + 3, bw - 6, 3);
			nvgFillColor(vg, alpha(CYANC, 0.5f + 0.5f * level[2]));
			nvgFill(vg);
		}
		// ---- the floor: tiles in perspective, lit in the colours of what plays (a little calmer than the dancers)
		uint32_t seed = (uint32_t) tileSeed * 2654435761u;
		for (int r = 0; r < 7; r++) {
			float y0 = hz + std::pow((float) r, 1.6f) * H * 0.021f, y1 = hz + std::pow((float) r + 1, 1.6f) * H * 0.021f;
			for (int k = -10; k <= 10; k++) {
				auto px = [&](float yy, float kk) { return W / 2 + kk * W * 0.056f * (1 + (yy - hz) / H * 5.f); };
				int tr = ((k + r) % 4 + 4) % 4;
				seed = seed * 1664525u + 1013904223u;
				bool lit = (seed >> 24) / 255.f < 0.15f + 0.5f * level[tr];
				nvgBeginPath(vg);
				nvgMoveTo(vg, px(y0, k), y0);
				nvgLineTo(vg, px(y0, k + 1), y0);
				nvgLineTo(vg, px(y1, k + 1), y1);
				nvgLineTo(vg, px(y1, k), y1);
				nvgClosePath(vg);
				nvgFillColor(vg, col(tr, lit ? 0.55f : 0.14f));
				nvgFill(vg);
				nvgStrokeColor(vg, DARK);
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}
		}
		// ---- the crowd at the back (small, they bounce on the beat)
		{
			float s2 = H / 210.f * zoom();
			for (int n = 0; n < 14; n++) {
				int k = n % NT;
				if (!on(k))
					continue;
				float x = W * (0.06f + 0.066f * n), foot = hz + H * (0.08f + 0.015f * (n % 3));
				int pa = (beat + n) % 4, pb = (beat + n + 1) % 4;
				float u = clamp(1.f - kick, 0.f, 1.f);
				figure(vg, x, foot, s2, 1 + n % 2, pa, pb, u, 4.f * kick * (n % 2), col(k, 0.3f + 0.25f * level[k]), 2.4f,
				       n % 2 ? 1.f : -1.f);
			}
		}
		// ---- lasers from the corners: drums left, melody right (six each, they sweep)
		for (int side = 0; side < 2; side++) {
			int k = side ? 2 : 0;
			if (!on(k))
				continue;
			float a0 = side ? -100.f : -80.f, sw = 12.f * std::sin(t * 0.7f + side * 1.7f + spot);
			NVGcolor c = col(k, 0.3f + 0.6f * clamp(level[k] + flash[k], 0.f, 1.f));
			for (int j = 0; j < 6; j++) {
				float a = (a0 + (side ? -1.f : 1.f) * j * 13.f + sw) * (float) M_PI / 180.f;
				float x0 = side ? W : 0.f;
				line(vg, x0, H, x0 + 2.f * W * std::cos(a), H + 2.f * W * std::sin(a), c, 1.4f);
			}
		}
		// ---- a spotlight per track (they swing with the bars)
		for (int i = 0; i < NT; i++) {
			static const int ORDER[NT] = {0, 2, 1, 3};
			int k = ORDER[i];
			if (!on(k))
				continue;
			float sx = W * (0.14f + 0.24f * i) + W * 0.06f * std::sin(spot * 1.3f + i * 2.f + t * 0.5f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, sx - 3, 0);
			nvgLineTo(vg, sx + 3, 0);
			nvgLineTo(vg, sx + W * 0.07f, H);
			nvgLineTo(vg, sx - W * 0.07f, H);
			nvgClosePath(vg);
			nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, H, col(k, 0.12f + 0.3f * level[k]), col(k, 0.f)));
			nvgFill(vg);
		}
		// ---- the disco ball and its light dots everywhere (they turn, the kick makes them flash)
		{
			float bx = W / 2, by = H * 0.14f, br = H * 0.075f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, bx, 0);
			nvgLineTo(vg, bx, by - br);
			nvgStrokeColor(vg, nvgRGB(0x8a, 0x8d, 0x99));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			nvgBeginPath(vg);
			nvgCircle(vg, bx, by, br);
			nvgFillPaint(vg, nvgRadialGradient(vg, bx - br * 0.3f, by - br * 0.3f, br * 0.1f, br * 1.1f, nvgRGB(0xff, 0xff, 0xff),
			                                   nvgRGB(0x55, 0x55, 0x6e)));
			nvgFill(vg);
			nvgBeginPath(vg);
			for (int j = 1; j < 7; j++) { // facets: rows, and columns that turn
				float yy = by - br + j * 2 * br / 7, hw = br * std::sqrt(std::max(0.f, 1.f - std::pow((yy - by) / br, 2.f)));
				nvgMoveTo(vg, bx - hw, yy);
				nvgLineTo(vg, bx + hw, yy);
				float ph = std::fmod(t * 0.6f + j / 7.f, 1.f);
				float xx = bx + br * std::cos((float) M_PI * ph);
				nvgMoveTo(vg, xx, by - br * 0.95f);
				nvgQuadTo(vg, bx + (xx - bx) * 1.25f, by, xx, by + br * 0.95f);
			}
			nvgStrokeColor(vg, nvgRGBA(0x6b, 0x6b, 0x88, 200));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);
			float fl = 0.45f + 0.55f * kick;
			dot(vg, bx - br * 0.35f, by - br * 0.4f, br * 0.12f, nvgRGBAf(1, 1, 1, fl));
			uint32_t sd = 0x9e3779b9u;
			auto rnd = [&]() {
				sd ^= sd << 13, sd ^= sd >> 17, sd ^= sd << 5;
				return (sd >> 8) / 16777216.f;
			};
			float turn = t * 0.25f;
			for (int n = 0; n < 130; n++) {
				float a = rnd() * 2 * (float) M_PI + turn, d = br * 1.6f + rnd() * W * 0.55f, r = 1.2f + rnd() * 2.f;
				float x = bx + d * std::cos(a), y = by + std::fabs(d * std::sin(a)) * 0.75f;
				if (x < 0 || x > W || y > H)
					continue;
				int k = n % NT;
				NVGcolor c = n % 3 ? col(k, 0.5f + 0.4f * fl) : nvgRGBAf(1, 1, 1, 0.5f + 0.4f * fl);
				nvgBeginPath(vg);
				nvgCircle(vg, x, y, r);
				nvgFillColor(vg, c);
				nvgFill(vg);
			}
		}
		// ---- fog on the floor (pads)
		if (on(3)) {
			nvgBeginPath(vg);
			nvgRect(vg, 0, H * 0.7f, W, H * 0.3f);
			nvgFillPaint(vg, nvgLinearGradient(vg, 0, H * 0.7f, 0, H, col(3, 0.f), col(3, 0.15f + 0.45f * level[3])));
			nvgFill(vg);
			for (int n = 0; n < 7; n++)
				haze(vg, W * (n + 0.5f) / 7 + W * 0.03f * std::sin(t * 0.3f + n), H * 0.9f, H * 0.16f, col(3), 0.1f + 0.2f * level[3]);
		}
		// ---- the four dancers
		float s = H / 110.f * zoom();
		static const float X[NT] = {0.18f, 0.4f, 0.62f, 0.82f};
		for (int k = 0; k < NT; k++) {
			if (!on(k))
				continue;
			drawFigure(vg, W * X[k], H * 0.86f, s, k, dancers[k]);
		}
		// ---- the crowd in front: heads, some hands up (they bounce on the kick)
		{
			float hr = H * 0.033f;
			nvgBeginPath(vg);
			for (int n = 0; n < 22; n++) {
				float x = (n + 0.25f) * W / 21.f, y = H - hr * 0.2f + (n % 2) * hr * 0.4f - (n % 2 ? 0.f : hr * 0.5f * kick);
				nvgCircle(vg, x, y - hr * 1.4f, hr);
				nvgRoundedRect(vg, x - hr * 1.45f, y - hr * 0.6f, hr * 2.9f, hr * 2.4f, hr * 0.9f);
			}
			nvgFillColor(vg, nvgRGB(0x05, 0x02, 0x0c));
			nvgFill(vg);
			nvgBeginPath(vg);
			for (int n = 0; n < 22; n += 3) {
				float x = (n + 0.25f) * W / 21.f, y = H - hr * 0.2f, wv = 0.25f * std::sin(t * 4.f + n);
				float hx = x + hr * (0.9f + 3.f * wv), hy = y - hr * (4.2f + 0.5f * kick);
				nvgMoveTo(vg, x + hr * 0.7f, y - hr * 0.4f);
				nvgLineTo(vg, hx, hy);
			}
			nvgStrokeColor(vg, nvgRGB(0x05, 0x02, 0x0c));
			nvgStrokeWidth(vg, hr * 0.55f);
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		nvgSave(vg);
		nvgScissor(vg, 0, 0, W, H);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, W, H, 4);
		nvgFillColor(vg, sceneNow() == S_DANCE ? nvgRGB(0x0b, 0x07, 0x16) : nvgRGB(0x07, 0x08, 0x0a));
		nvgFill(vg);
		if (m) {
			switch (sceneNow()) {
				case S_RING: drawRing(vg, W, H); break;
				case S_BARS: drawBars(vg, W, H); break;
				case S_SCOPE: drawScope(vg, W, H); break;
				case S_TUNNEL: drawTunnel(vg, W, H); break;
				case S_STARS: drawStars(vg, W, H); break;
				case S_PLASMA: drawPlasma(vg, W, H); break;
				case S_FALL: drawFall(vg, W, H); break;
				case S_SPARKS: drawSparks(vg, W, H); break;
				case S_DANCE: drawDance(vg, W, H); break;
				default: break;
			}
		}
		// the head line: the scene, what it follows
		auto f = genra_ui::boldFont();
		std::string sc = SCENE[sceneNow()];
		genra_ui::text(vg, f, 10, 16, sc, 11.f, ACC, NVG_ALIGN_LEFT);
		std::string info;
		if (!m)
			info = "FOLLOW GENRA";
		else {
			bool live = m->genraLive.load(), found = m->genra.to.load() != nullptr;
			if (live)
				info = string::f("FOLLOW GENRA · %.0f BPM · BAR %d", m->bpm.load(), step16 / 16 % 4 + 1);
			else if (found)
				info = "GENRA FOUND · STOPPED";
			else if (m->audioIn.load())
				info = "AUDIO";
			else
				info = "NO SIGNAL · FOLLOW GENRA OR PATCH AUDIO";
			if (live && m->audioIn.load())
				info += " · AUDIO";
			if (m->params[Glanz::FREEZE_PARAM].getValue() > 0.5f)
				info = "FREEZE · " + info;
		}
		genra_ui::text(vg, f, W - 10, 16, info, 9.f, nvgRGB(0x8a, 0x8d, 0x99), NVG_ALIGN_RIGHT);
		nvgResetScissor(vg);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct GlanzWidget : ModuleWidget {
	Display* disp = nullptr;
	neonkit::Undo undo;
	dsp::BooleanTrigger undoBtn, initBtn;
	GlanzWidget(Glanz* module) {
		using namespace neonui;
		using namespace neonkit;
		using M = Glanz;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Glanz.svg")));

		Display* d = new Display;
		d->m = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0] + 0.6f, L::DISPLAY[1] + 0.6f));
		d->box.size = mm2px(Vec(L::DISPLAY[2] - 1.2f, L::DISPLAY[3] - 1.2f));
		addChild(d);
		disp = d;

		// the scene field (it shows what plays: SCENE CV may override the pick)
		genra_look::PickField* pf = createWidget<genra_look::PickField>(mm2px(Vec(L::PRESETF[0], L::PRESETF[1])));
		pf->box.size = mm2px(Vec(L::PRESETF[2], L::PRESETF[3]));
		pf->accent = PINK;
		pf->count = []() { return (int) NUM_SCENES; };
		pf->name = [](int i) { return string::f("%02d  %s", i + 1, SCENE[clamp(i, 0, NUM_SCENES - 1)]); };
		pf->current = [=]() { return d->sceneNow(); };
		pf->choose = [=](int i) {
			if (module) {
				undo.push(module);
				module->scene = clamp(i, 0, NUM_SCENES - 1);
				d->barsInScene = 0;
			}
		};
		addChild(pf);
		genra_map::select(this, pf, module, M::SCENESEL_PARAM, module ? &module->mapCode : nullptr, pf->count, pf->current,
		                  pf->choose); // MIDI-MAP: a knob goes through the scenes

		knob(this, module, L::AMOUNT, M::AMOUNT_PARAM);
		knob(this, module, L::SMOOTH, M::SMOOTH_PARAM);
		knob(this, module, L::DECAY, M::DECAY_PARAM);
		knob(this, module, L::SPEED, M::SPEED_PARAM);
		knob(this, module, L::COLOR, M::COLOR_PARAM);
		knob(this, module, L::ZOOM, M::ZOOM_PARAM);
		knob(this, module, L::TRAILS, M::TRAILS_PARAM);
		knob(this, module, L::GLOW, M::GLOW_PARAM);
		knob(this, module, L::AUTO, M::AUTO_PARAM);
		button(this, module, L::FREEZE, M::FREEZE_PARAM, true, CYAN);
		NButton* nb = button(this, module, L::NEXT, M::NEXT_PARAM, false, PINK);
		nb->lit = [=]() { return module && module->lights[M::NEXT_LIGHT].getBrightness() > 0.1f; };
		const float* tr[NT] = {L::TR0, L::TR1, L::TR2, L::TR3};
		for (int t = 0; t < NT; t++)
			button(this, module, tr[t], M::TRACK_PARAM + t, true, TCOL[t]);
		button(this, module, L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(this, module, L::INIT, M::INIT_PARAM, false, VIOLET);
		for (int i = 0; i < M::INPUTS_LEN; i++)
			addPort<NPort>(this, L::JACK_IN[i], module, i, false, hex(L::JACK_IN_COL[i]));
	}
	void step() override {
		ModuleWidget::step();
		Glanz* m = dynamic_cast<Glanz*>(module);
		if (m && m->params[Glanz::NEXT_PARAM].getValue() > 0.5f && !nextDown) {
			undo.push(m);
			m->nextReq++; // the NEXT button (the display picks it up)
		}
		if (m && initBtn.process(m->params[Glanz::INIT_PARAM].getValue() > 0.5f)) {
			undo.push(m);
			for (int i = 0; i < Glanz::PARAMS_LEN; i++)
				if (i != Glanz::INIT_PARAM && i != Glanz::UNDO_PARAM)
					m->params[i].setValue(m->paramQuantities[i]->getDefaultValue());
			m->scene = 0;
			if (disp)
				disp->barsInScene = 0;
		}
		if (m && undoBtn.process(m->params[Glanz::UNDO_PARAM].getValue() > 0.5f))
			undo.pop(m);
		nextDown = m && m->params[Glanz::NEXT_PARAM].getValue() > 0.5f;
	}
	bool nextDown = false;
	void appendContextMenu(Menu* menu) override {
		Glanz* m = dynamic_cast<Glanz*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolMenuItem("Follow GENRA", "", [=]() { return m->followOn; }, [=](bool v) {
			m->followOn = v;
			m->findT = 0.f;
		}));
		// which one (when there are several)
		std::vector<std::pair<int64_t, std::string>> srcs;
		for (int64_t mid : APP->engine->getModuleIds()) {
			Module* x = APP->engine->getModule(mid);
			if (follow::isSource(x))
				srcs.push_back({mid, x->model ? x->model->name : std::string("GENRA")});
		}
		if (srcs.size() > 1) {
			menu->addChild(createMenuLabel("Follow which one"));
			menu->addChild(createCheckMenuItem("Automatic (the first)", "", [=]() { return m->genraId.load() < 0; }, [=]() {
				m->genraId = -1;
				m->findT = 0.f;
			}));
			for (auto& s : srcs) {
				int64_t id = s.first;
				menu->addChild(createCheckMenuItem(string::f("%s #%lld", s.second.c_str(), (long long) (id % 10000)), "",
				                                   [=]() { return m->genraId.load() == id; }, [=]() {
					                                   m->genraId = id;
					                                   m->findT = 0.f;
				                                   }));
			}
		}
	}
};

}  // namespace glanz

Model* modelGlanz = createModel<glanz::Glanz, glanz::GlanzWidget>("Glanz");
