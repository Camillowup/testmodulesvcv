// SPDX-License-Identifier: GPL-3.0-only
// FUMI: FuMi-1's shigin koto (after the Suiko ST-50) as a VCV Rack voice. The instrument runs unchanged in a private
// copy of the FUMI core (FumiCore.hpp); this file is the panel, the jacks and the glue: V/OCT + GATE (poly) into notes
// in cents with the ST-50's tunings (chromatic, or snapped to the scale), 本数 / tuning / scale, the sound's knobs and CV,
// the ornaments (buttons and gates), the sprung bend from BEND, its 44.1 kHz audio resampled to Rack's rate.
// After KURBEL (src/kurbel/Kurbel.cpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/OutLimit.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "../../genra/ui/Look.hpp"
#include "FumiCore.hpp"
#include "FumiLayout.hpp"
#include "FumiGlyphs.hpp"
#include "FumiLevels.hpp"
#include <atomic>
#include <array>
#include <cstring>
#include <cctype>

namespace genra_fumi {

#define FUMI_RGB 0x6f, 0x7c, 0xf5   // keep in step with tools/jpanel.py COLORS

static const int NS = 7, NH = 7, NCH = JFU_NCH;
static const uint32_t SOUND_P[NS] = {JFU_YOIN, JFU_TRILL_RATE, JFU_VIB_DEPTH, JFU_TRILL_FEEL, JFU_OROSHI, JFU_CHARACTER,
                                     JFU_REVERB};
static const char* SOUND_LAB[NS] = {"YOIN", "RATE", "VIB", "FEEL", "ORO", "CHAR", "REV"};
static const char* SOUND_TIP[NS] = {"Yoin (余韻: how long a note rings after its gate)", "Trill rate",
                                    "Vibrato depth", "Trill feel (0: the even machine, 100: pairs as a hand plays them)",
                                    "Oroshi (the trill gate's run from slow to the rate)", "Character (tilt and warmth)",
                                    "Reverb"};
static const char* HAND_LAB[NH] = {"VIB", "TRILL", "DAMP", "HARD", "OSHI", "HIKI", "ATO"};
static const char* HAND_TIP[NH] = {"Vibrato (latches; the gate: vibrato while high)",
                                   "Trill (latches; the gate: the ornament trill while high, with oroshi)",
                                   "Damp (every note let go at once)", "Hard (the next notes struck hard)",
                                   "Oshide 押し手 (the sounding notes rise a whole tone while held)",
                                   "Hikiiro 引き色 (each new note dips just after the pluck)",
                                   "Atooshi 後押し (each new note pushed up a semitone a moment after it sounds)"};
static const char* HON_ASCII[16] = {"S4", "S3", "S2", "S1", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
static const char* HON_FIELD[16] = {"\xe6\xb0\xb4" "4", "\xe6\xb0\xb4" "3", "\xe6\xb0\xb4" "2", "\xe6\xb0\xb4" "1",
                                    "1\xe6\x9c\xac", "2\xe6\x9c\xac", "3\xe6\x9c\xac", "4\xe6\x9c\xac", "5\xe6\x9c\xac",
                                    "6\xe6\x9c\xac", "7\xe6\x9c\xac", "8\xe6\x9c\xac", "9\xe6\x9c\xac", "10\xe6\x9c\xac",
                                    "11\xe6\x9c\xac", "12\xe6\x9c\xac"};
static const char* TUNE_NAMES[4] = {"EQUAL", "SUIKO", "KOTO", "USER"};
static const char* SCALE_NAMES[3] = {"IN", "YO", "MIN'YO"};
static const char* DEG_NAMES[3][5] = {{"mi", "fa", "la", "ti", "do"}, {"mi", "fa#", "la", "ti", "do#"},
                                      {"mi", "sol", "la", "ti", "re"}};
static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/** The reading of each of FuMi-1's own patches (its Japanese name in kana), by its English name. */
static const char* reading(const char* name) {
	static const char* R[6][2] = {{"Koto Pluck", "\xe3\x81\xa4\xe3\x82\x81\xe3\x81\x8a\xe3\x81\xa8"},
	                              {"Koto Ring", "\xe3\x81\xb2\xe3\x81\xb3\xe3\x81\x8d"},
	                              {"Koto Warm", "\xe3\x81\xaa\xe3\x81\x94\xe3\x82\x8a"},
	                              {"Harp", "\xe3\x81\x8f\xe3\x81\x94"},
	                              {"Hyoshigi", "\xe3\x81\xb2\xe3\x82\x87\xe3\x81\x86\xe3\x81\x97\xe3\x81\x8e"},
	                              {"Mokugyo", "\xe3\x82\x82\xe3\x81\x8f\xe3\x81\x8e\xe3\x82\x87"}};
	for (auto& r : R)
		if (name && !std::strcmp(name, r[0]))
			return r[1];
	return "";
}

// ------------------------------------------------------------------------------------------------ the module
struct Fumi : Module, follow::Host {
	enum ParamId {
		PREV_PARAM, NEXT_PARAM,
		HON_PARAM, TUNE_PARAM, SCALE_PARAM, VOICE_PARAM, LEVEL_PARAM,
		ENUMS(S_PARAM, NS),
		ENUMS(H_PARAM, NH),
		FINE_PARAM, A4_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(S_INPUT, NS),
		ENUMS(H_INPUT, NH),
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, BEND_INPUT, KEY_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(H_LIGHT, NH), LIGHTS_LEN };
	enum { BLOCK = 32 };

	FumiCore core;
	std::string status;

	// settings
	int patch = 0;
	bool snap = false;                            // (right-click) V/OCT snapped to the scale's degrees
	std::atomic<int> patchReq{-1};

	// for the UI thread
	std::array<std::array<char, 16>, NS> values{};
	std::atomic<int> shownPatch{0};
	std::atomic<uint32_t> shownVoices{0};
	std::atomic<uint32_t> litKeys{0};             // bit k: white key k sounds
	std::atomic<int> shownSan{57};
	std::atomic<uint32_t> shownOrn{0};            // bit 0 VIB, bit 1 TRILL, bit 2 oroshi set

	// engine thread
	int sent[JFU_NP];
	float devL[BLOCK], devR[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceUi = 0;
	bool gate[NCH] = {};                          // the channel's note sounds
	float cents[NCH] = {};
	// a channel of V/OCT + GATE: `high` the gate input; `arm` counts the samples from the gate's edge to the note
	// (NOTE_DELAY: V/OCT and VEL may come a sample after the gate, e.g. through a quantizer)
	enum { NOTE_DELAY = 2 };
	bool high[NCH] = {};
	int arm[NCH] = {};
	int chActive = 0;                             // the channels the last sample used (the loop ends there)
	// the pitch of a channel's voltage, kept while voltage, snap and tuning stay (the core is asked only on a change)
	float pMidi[NCH] = {}, pCents[NCH] = {};
	int pSnap[NCH] = {};
	uint32_t pGen[NCH] = {}, tuneGen = 1;
	float outGain = 1.f;                          // the patch's level (FumiLevels.hpp), slewed
	bool powered = false;                         // the core runs (powerOn): before that it is loaded but not started
	bool ornOn[NH] = {};
	bool bendUp = false, bendDown = false;
	dsp::BooleanTrigger prevTrig, nextTrig;

	Fumi() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "FUMI";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		configButton(PREV_PARAM, "Previous instrument");
		configButton(NEXT_PARAM, "Next instrument");
		configSwitch(HON_PARAM, 0.f, 15.f, 4.f, "Key (honsuu: S4 .. S1, 1 .. 12; at 1, san = A3)",
		             {HON_ASCII, HON_ASCII + 16});
		configSwitch(TUNE_PARAM, 0.f, 3.f, 1.f, "Tuning",
		             {"EQUAL (12-tone equal)", "SUIKO (the ST-50's just tuning, as measured)", "KOTO (pure fifths)",
		              "USER (equal, trimmed by Fine)"});
		configSwitch(SCALE_PARAM, 0.f, 2.f, 0.f, "Scale", {"IN (mi fa la ti do)", "YO (fa and do raised)",
		                                                   "MIN'YO (fa and do raised a whole tone)"});
		configSwitch(VOICE_PARAM, 0.f, 1.f, 1.f, "Voice", {"MONO (last note, sliding)", "POLY"});
		configParam(LEVEL_PARAM, 0.f, 100.f, 80.f, "Level")->snapEnabled = true;
		for (int k = 0; k < NS; k++) {
			configParam<SQuantity>(S_PARAM + k, 0.f, 1.f, 0.5f, SOUND_TIP[k]);
			configInput(S_INPUT + k, string::f("%s CV (±5 V: ± half the range)", SOUND_LAB[k]));
		}
		for (int k = 0; k < NH; k++) {
			if (k < 2)
				configSwitch(H_PARAM + k, 0.f, 1.f, 0.f, HAND_TIP[k], {"Off", "On"});
			else
				configButton(H_PARAM + k, HAND_TIP[k]);
			configInput(H_INPUT + k, string::f("%s gate", HAND_LAB[k]));
		}
		configParam(FINE_PARAM, -50.f, 50.f, 0.f, "Fine tuning", " cents")->snapEnabled = true;
		configParam(A4_PARAM, 430.f, 445.f, 440.f, "A4", " Hz")->snapEnabled = true;
		configInput(VOCT_INPUT, "V/Oct (polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic)");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(BEND_INPUT, "Bend (above +1 V: the sprung bend up; below -1 V: down)");
		configInput(KEY_INPUT, "Key CV (1 V/oct: each semitone one step of honsuu)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		for (int k = 0; k < NH; k++)
			paramQuantities[H_PARAM + k]->randomizeEnabled = false;
		paramQuantities[FINE_PARAM]->randomizeEnabled = false;
		paramQuantities[A4_PARAM]->randomizeEnabled = false;
		paramQuantities[VOICE_PARAM]->randomizeEnabled = false;
		for (int& s : sent)
			s = INT32_MIN;
		core.load();                               // here (the UI thread), not in onAdd: the engine waits there
	}

	/** The sound's knobs: 0..1 over the parameter's range, shown as FuMi-1 writes it. */
	struct SQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Fumi* m = dynamic_cast<Fumi*>(module);
			if (m && m->core.loaded())
				return m->values[paramId - S_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
	};

	// ---------------------------------------------------------------------------------- power, patches
	void powerOn() {
		if (!core.loaded() && !core.load()) {
			status = core.error;
			WARN("FUMI: %s", status.c_str());
			return;
		}
		status.clear();
		const jfu_api* a = core.api;
		a->init(random::u32() | 1u);
		for (int& s : sent)
			s = INT32_MIN;
		outBuf.clear();
		src.reset();
		for (int c = 0; c < NCH; c++)
			gate[c] = high[c] = false, arm[c] = 0, pGen[c] = 0;
		chActive = NCH;
		for (bool& o : ornOn)
			o = false;
		bendUp = bendDown = false;
		int n = std::max(1, (int) a->npatches());
		patch = ((patch % n) + n) % n;
		shownPatch = patch;
		outGain = patchGain();
		powered = true;
	}

	/** The patch's output level (FumiLevels.hpp: every patch as loud as the others, a chord just under the limiter). */
	float patchGain() const { return patch >= 0 && patch < fumilevels::NUM ? fumilevels::PATCH_GAIN[patch] : 1.f; }

	/** The knobs' defaults from the instrument (the first time, or Initialize). */
	void knobDefaults() {
		const jfu_api* a = core.api;
		if (!a)
			return;
		for (int k = 0; k < NS; k++) {
			uint32_t p = SOUND_P[k];
			int lo = a->min(p), hi = a->max(p);
			float v = hi > lo ? (float) (a->def(p) - lo) / (hi - lo) : 0.f;
			paramQuantities[S_PARAM + k]->defaultValue = v;
		}
	}

	void onAdd(const AddEvent& e) override {
		params[PREV_PARAM].setValue(0.f);
		params[NEXT_PARAM].setValue(0.f);
		bool fresh = !fromPatch;
		powerOn();
		knobDefaults();
		if (fresh)
			for (int k = 0; k < NS; k++)
				params[S_PARAM + k].setValue(paramQuantities[S_PARAM + k]->defaultValue);
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); powered = false; core.unload(); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		knobDefaults();
		for (int k = 0; k < NS; k++)
			params[S_PARAM + k].setValue(paramQuantities[S_PARAM + k]->defaultValue);
		snap = false;
		patchReq = 0;
	}

	bool fromPatch = false;
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "patch", json_integer(patch));
		json_object_set_new(r, "snap", json_boolean(snap));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		fromPatch = true;
		if (json_t* j = json_object_get(r, "patch"))
			patch = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "snap"))
			snap = json_boolean_value(j);
		if (powered && core.api) {
			int n = std::max(1, (int) core.api->npatches());
			patch = ((patch % n) + n) % n;
			for (int& s : sent)
				s = INT32_MIN;
			shownPatch = patch;
		}
	}

	// ---------------------------------------------------------------------------------- the engine
	void push(const jfu_api* a, uint32_t p, int v) {
		if (sent[p] != v) {
			a->set(p, v);
			sent[p] = v;
			if (p == JFU_HON || p == JFU_SCALE || p == JFU_TUNING || p == JFU_FINE || p == JFU_A4)
				tuneGen++;                          // (the tuning moved: the channels' pitches are asked again)
		}
	}

	/** The pitch (cents) of channel c's V/OCT now; the core works it out only when the voltage, snap or tuning changed. */
	float pitchOf(const jfu_api* a, int c) {
		float midi = 60.f + inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f;
		int sn = snap ? 1 : 0;
		if (pGen[c] != tuneGen || midi != pMidi[c] || sn != pSnap[c]) {
			pCents[c] = a->pitch(midi, sn);
			pMidi[c] = midi;
			pSnap[c] = sn;
			pGen[c] = tuneGen;
		}
		return pCents[c];
	}
	int velOf(int c) {
		if (!inputs[VEL_INPUT].isConnected())
			return 100;
		return clamp((int) std::round(inputs[VEL_INPUT].getPolyVoltage(c) / 10.f * 127.f), 1, 127);
	}

	void processKnobs(const jfu_api* a) {
		push(a, JFU_VOICE, patch);
		int hon = (int) std::round(params[HON_PARAM].getValue());
		if (inputs[KEY_INPUT].isConnected())
			hon += (int) std::round(inputs[KEY_INPUT].getVoltage() * 12.f);
		push(a, JFU_HON, clamp(hon, 0, 15));
		push(a, JFU_TUNING, (int) std::round(params[TUNE_PARAM].getValue()));
		push(a, JFU_SCALE, (int) std::round(params[SCALE_PARAM].getValue()));
		push(a, JFU_FINE, (int) std::round(params[FINE_PARAM].getValue()));
		push(a, JFU_A4, (int) std::round(params[A4_PARAM].getValue()));
		bool mono = params[VOICE_PARAM].getValue() < 0.5f;
		push(a, JFU_MONO, mono ? 1 : 0);
		push(a, JFU_SLIDE, mono ? 1 : 0);
		push(a, JFU_LEVEL, (int) std::round(params[LEVEL_PARAM].getValue()));
		for (int k = 0; k < NS; k++) {
			float x = params[S_PARAM + k].getValue();
			if (inputs[S_INPUT + k].isConnected())
				x += inputs[S_INPUT + k].getVoltage() / 10.f;
			x = clamp(x, 0.f, 1.f);
			uint32_t p = SOUND_P[k];
			int lo = a->min(p), hi = a->max(p);
			push(a, p, lo + (int) std::round(x * (hi - lo)));
		}
	}

	/** The ornaments: VIB and TRILL latch (FuMi-1's ビブラート and トリラー buttons); every gate, and the other buttons while
	 * pressed, hold an ornament on the sounding notes. */
	void processHands(const jfu_api* a) {
		bool vibLatch = params[H_PARAM + 0].getValue() > 0.5f, trillLatch = params[H_PARAM + 1].getValue() > 0.5f;
		push(a, JFU_VIB_ON, vibLatch ? 1 : 0);
		push(a, JFU_TRILL_ON, trillLatch ? 1 : 0);
		static const uint32_t ORN[NH] = {JFU_ORN_VIB, JFU_ORN_TRILL, JFU_ORN_DAMP, JFU_ORN_STRONG, JFU_ORN_OSHIDE,
		                                 JFU_ORN_HIKIIRO, JFU_ORN_ATOOSHI};
		for (int k = 0; k < NH; k++) {
			bool g = false;
			if (inputs[H_INPUT + k].isConnected()) {
				float v = inputs[H_INPUT + k].getVoltage();
				g = ornOn[k] ? v > 0.5f : v >= 1.f;
			}
			bool on = g || (k >= 2 && params[H_PARAM + k].getValue() > 0.5f);
			if (on != ornOn[k]) {
				a->ornament(ORN[k], on ? 1 : 0);
				ornOn[k] = on;
			}
			lights[H_LIGHT + k].setBrightness(on || (k < 2 && params[H_PARAM + k].getValue() > 0.5f) ? 1.f : 0.f);
		}
	}

	/** V/OCT and GATE, one note per channel; a new pitch under a held gate plays the note again (mono: slides). */
	void processNotes(const jfu_api* a) {
		int nch = clamp(inputs[GATE_INPUT].getChannels(), 0, NCH);
		int last = std::max(nch, chActive);        // (channels that went away still let go of their note)
		bool anyHeld = false;
		for (int c = 0; c < last; c++) {
			bool on = false;
			if (c < nch) {
				float g = inputs[GATE_INPUT].getVoltage(c);
				on = high[c] ? g > 0.5f : g >= 1.f;
			}
			bool rise = on && !high[c] && !gate[c];
			high[c] = on;
			if (rise) {
				arm[c] = NOTE_DELAY;                // the gate's edge: the note follows in NOTE_DELAY samples
			} else if (arm[c] > 0) {
				if (--arm[c] == 0 || !on) {         // (a gate shorter than the wait still plays)
					arm[c] = 0;
					float ct = pitchOf(a, c);
					a->note((uint32_t) c, 1, ct, velOf(c));
					cents[c] = ct;
					gate[c] = true;
				}
			} else if (!on && gate[c]) {
				a->note((uint32_t) c, 0, 0.f, 0);
				gate[c] = false;
			} else if (on && gate[c]) {
				float ct = pitchOf(a, c);
				if (std::fabs(ct - cents[c]) > 30.f) {
					a->note((uint32_t) c, 1, ct, velOf(c));
					cents[c] = ct;
				}
			}
			anyHeld = anyHeld || on;
		}
		chActive = nch;
		// the sprung bend: BEND above +1 V up, below -1 V down (hysteresis 0.5 V)
		float b = inputs[BEND_INPUT].getVoltage();
		bool up = bendUp ? b > 0.5f : b >= 1.f, down = bendDown ? b < -0.5f : b <= -1.f;
		if (up != bendUp || down != bendDown)
			a->bend(up ? 1 : 0, down ? 1 : 0, anyHeld ? 1 : 0);
		bendUp = up;
		bendDown = down;
	}

	void renderBlock(const jfu_api* a, float sampleRate) {
		processKnobs(a);
		int devRate = (int) a->sample_rate();
		a->render(BLOCK, devL, devR);
		dsp::Frame<2> in[BLOCK];
		for (int i = 0; i < BLOCK; i++) {
			in[i].samples[0] = devL[i];
			in[i].samples[1] = devR[i];
		}
		src.process(in, BLOCK, devRate, (int) sampleRate, outBuf);
		framesSinceUi += BLOCK;
		if (framesSinceUi >= (uint32_t) devRate / 30) {
			framesSinceUi = 0;
			for (int k = 0; k < NS; k++)
				a->format(SOUND_P[k], a->get(SOUND_P[k]), values[k].data(), (uint32_t) values[k].size());
			shownVoices = a->active_voices();
			shownSan = a->san_midi();
			uint32_t lit = 0;
			for (int c = 0; c < NCH; c++) {
				if (!gate[c])
					continue;
				for (uint32_t w = 0; w < 16; w++)
					if (std::fabs(a->white_cents(w) - cents[c]) < 50.f)
						lit |= 1u << w;
			}
			litKeys = lit;
			shownOrn = (params[H_PARAM].getValue() > 0.5f || ornOn[0] ? 1u : 0u) |
			           (params[H_PARAM + 1].getValue() > 0.5f || ornOn[1] ? 2u : 0u) |
			           (a->get(JFU_OROSHI) > 0 ? 4u : 0u);
		}
	}

	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jfu_api* a = core.api;
		if (!a) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		int req = patchReq.load(std::memory_order_relaxed) >= 0 ? patchReq.exchange(-1) : -1;
		bool want = req >= 0;
		if (prevTrig.process(params[PREV_PARAM].getValue() > 0.5f)) {
			req = patch - 1;
			want = true;
		}
		if (nextTrig.process(params[NEXT_PARAM].getValue() > 0.5f)) {
			req = patch + 1;
			want = true;
		}
		if (want) {
			int n = std::max(1, (int) a->npatches());
			patch = ((req % n) + n) % n;
			shownPatch = patch;
		}
		processHands(a);
		processNotes(a);
		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(a, args.sampleRate);
		dsp::Frame<2> f = outBuf.empty() ? dsp::Frame<2>{} : outBuf.shift();
		// the patch's level (FumiLevels.hpp; a new patch's comes in over a few ms), then the output limiter
		outGain += (patchGain() - outGain) * 0.005f;
		float l = genra_out::limit(f.samples[0] * 5.f * outGain), r = genra_out::limit(f.samples[1] * 5.f * outGain);
		if (outputs[R_OUTPUT].isConnected() || pulled(args.frame)) {
			outputs[L_OUTPUT].setVoltage(l);
			outputs[R_OUTPUT].setVoltage(r);
		} else {
			outputs[L_OUTPUT].setVoltage(0.5f * (l + r));
			outputs[R_OUTPUT].setVoltage(0.f);
		}
	}
};

// ------------------------------------------------------------------------------------------------ Japanese text
// The kanji and kana FUMI shows are drawn from outlines (FumiGlyphs.hpp, Noto Sans CJK JP); the rest in the panel's fonts.
static const fumi_glyphs::Glyph* findGlyph(uint32_t cp) {
	for (int i = 0; i < fumi_glyphs::NGLYPHS; i++)
		if (fumi_glyphs::GLYPHS[i].cp == cp)
			return &fumi_glyphs::GLYPHS[i];
	return nullptr;
}

/** The next code point of a UTF-8 string (advances i). */
static uint32_t nextCp(const std::string& s, size_t& i) {
	unsigned char c = (unsigned char) s[i];
	int n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
	uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
	for (int k = 1; k < n && i + k < s.size(); k++)
		cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3f);
	i += n;
	return cp;
}

/** One glyph, its baseline at y, size px per em. */
static void drawGlyph(NVGcontext* vg, const fumi_glyphs::Glyph* g, float x, float y, float size, NVGcolor color) {
	const float* d = g->d;
	int k = 0;
	nvgBeginPath(vg);
	auto X = [&](float v) { return x + v * size; };
	auto Y = [&](float v) { return y - v * size; };
	while (k < g->n) {
		int op = (int) d[k++];
		switch (op) {
			case 0: nvgMoveTo(vg, X(d[k]), Y(d[k + 1])); k += 2; break;
			case 1: nvgLineTo(vg, X(d[k]), Y(d[k + 1])); k += 2; break;
			case 2: nvgBezierTo(vg, X(d[k]), Y(d[k + 1]), X(d[k + 2]), Y(d[k + 3]), X(d[k + 4]), Y(d[k + 5])); k += 6; break;
			case 3: nvgQuadTo(vg, X(d[k]), Y(d[k + 1]), X(d[k + 2]), Y(d[k + 3])); k += 4; break;
			case 4: nvgClosePath(vg); break;
			case 5: nvgPathWinding(vg, NVG_HOLE); break;
			default: k = g->n; break;
		}
	}
	nvgFillColor(vg, color);
	nvgFill(vg);
}

/** Text with Japanese in it: ASCII runs in `font`, the rest from the outlines. Returns the width (draw = false: only
 * measures). */
static float mixedText(NVGcontext* vg, const std::shared_ptr<window::Font>& font, float x, float y, const std::string& s,
                       float size, NVGcolor color, bool draw = true) {
	float cx = x;
	size_t i = 0;
	std::string run;
	auto flush = [&]() {
		if (run.empty())
			return;
		float b[4];
		nvgFontSize(vg, size);
		if (font)
			nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, 0.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float w = nvgTextBounds(vg, cx, y, run.c_str(), nullptr, b);
		if (draw) {
			nvgFillColor(vg, color);
			nvgText(vg, cx, y, run.c_str(), nullptr);
		}
		cx += w;
		run.clear();
	};
	while (i < s.size()) {
		size_t at = i;
		uint32_t cp = nextCp(s, i);
		const fumi_glyphs::Glyph* g = cp >= 0x3000 ? findGlyph(cp) : nullptr;
		if (!g) {
			run += s.substr(at, i - at);
			continue;
		}
		flush();
		if (draw)
			drawGlyph(vg, g, cx, y + size * 0.12f, size, color);
		cx += g->adv * size;
	}
	flush();
	return cx - x;
}

// ------------------------------------------------------------------------------------------------ the panel (GENRA look)
namespace L = flayout::fumi;
static const NVGcolor ACCENT = nvgRGB(FUMI_RGB);

/** The instrument's Japanese name, large, and its reading beneath. */
struct ScreenWidget : Widget {
	Fumi* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		if (module && !module->status.empty()) {
			jiant::drawStatus(vg, module->status, box.size.x);
			return;
		}
		const jfu_api* a = module ? module->core.api : nullptr;
		int p = module ? (int) module->shownPatch : 0;
		std::string ja = a ? a->patch_ja((uint32_t) p) : "\xe7\x88\xaa\xe9\x9f\xb3";
		std::string rd = reading(a ? a->patch_name((uint32_t) p) : "Koto Pluck");
		float w = box.size.x, h = box.size.y;
		float size = std::min(46.f, h * 0.6f);
		float tw = mixedText(vg, nullptr, 0, 0, ja, size, ACCENT, false);
		if (tw > w - 12.f) {
			size *= (w - 12.f) / tw;
			tw = w - 12.f;
		}
		NVGcolor glow = ACCENT;
		glow.a = module && module->shownVoices > 0 ? 1.f : 0.8f;
		mixedText(vg, nullptr, (w - tw) / 2, h * 0.58f, ja, size, glow);
		float rs = 6.5f, rw = mixedText(vg, nullptr, 0, 0, rd, rs, jiant::DIM, false);
		mixedText(vg, nullptr, (w - rw) / 2, h - 3.f, rd, rs, jiant::DIM);
	}
};

/** Right of the name: the instrument, 本数 · 三 · tuning · scale, the sixteen keys of the scale (tonics marked, the
 * sounding ones lit), voices and the ornaments on. */
struct InfoWidget : Widget {
	Fumi* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x;
		const jfu_api* a = module ? module->core.api : nullptr;
		int p = module ? (int) module->shownPatch : 0;
		std::string name = a ? a->patch_name((uint32_t) p) : "Koto Pluck";
		for (char& c : name)
			c = (char) std::toupper((unsigned char) c);
		float h = box.size.y;
		jiant::text(vg, bold, 0, 7.f, string::f("KOTO %d / %u", p + 1, a ? a->npatches() : 6u), 5.2f, jiant::DIM,
		            NVG_ALIGN_LEFT, 1.f);
		jiant::text(vg, bold, 0, 19.f, name, 11.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.6f);
		int hon = 4, tune = 1, scale = 0;
		if (module) {
			hon = clamp((int) std::round(module->params[Fumi::HON_PARAM].getValue()), 0, 15);
			tune = clamp((int) std::round(module->params[Fumi::TUNE_PARAM].getValue()), 0, 3);
			scale = clamp((int) std::round(module->params[Fumi::SCALE_PARAM].getValue()), 0, 2);
		}
		int san = module ? (int) module->shownSan : 57;
		std::string line = string::f("%s \xc2\xb7 \xe4\xb8\x89 = %s%d \xc2\xb7 %s \xc2\xb7 %s", HON_FIELD[hon],
		                             NOTE_NAMES[((san % 12) + 12) % 12], san / 12 - 1, TUNE_NAMES[tune], SCALE_NAMES[scale]);
		mixedText(vg, bold, 0, 27.f, line, 5.4f, ACCENT);
		// voices and the ornaments on, top right
		uint32_t voices = module ? (uint32_t) module->shownVoices : 2u;
		uint32_t orn = module ? (uint32_t) module->shownOrn : 7u;
		std::string o = string::f("VOICES %u/8", voices);
		if (orn & 1u)
			o += " \xc2\xb7 VIB";
		if (orn & 2u)
			o += " \xc2\xb7 TRILL";
		if (orn & 4u)
			o += " \xe9\xa2\xaa";
		float ow = mixedText(vg, bold, 0, 0, o, 5.2f, jiant::DIM, false);
		mixedText(vg, bold, w - ow, 7.f, o, 5.2f, jiant::DIM);
		// the sixteen keys
		uint32_t lit = module ? (uint32_t) module->litKeys : (1u << 7);
		float kw = w / 16.f, ky = 31.f, kh = h - ky;
		for (int i = 0; i < 16; i++) {
			bool on = lit & (1u << i), tonic = i % 5 == 0;
			jiant::roundRect(vg, i * kw, ky, kw - 1.4f, kh, 1.2f,
			                 on ? ACCENT : tonic ? nvgRGB(0x2d, 0x31, 0x60) : nvgRGB(0x22, 0x23, 0x2a));
			jiant::text(vg, mono, i * kw + (kw - 1.4f) / 2, h - 3.f, DEG_NAMES[scale][i % 5], 4.4f,
			            on ? jiant::WHITE : jiant::DIM, NVG_ALIGN_CENTER, 0.f);
		}
	}
};

struct FumiWidget : ModuleWidget {
	FumiWidget(Fumi* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Fumi.svg")));
		follow::addStrip(this, module, {jiant::at(L::VOCT), jiant::at(L::GATE), jiant::at(L::VEL)});
		jiant::addScrews(this);

		ScreenWidget* scr = new ScreenWidget;
		scr->module = module;
		scr->box = jiant::area(L::SCREEN);
		addChild(scr);
		InfoWidget* info = new InfoWidget;
		info->module = module;
		info->box = jiant::area(L::INFO);
		addChild(info);

		genra_look::PickField* pf = createWidget<genra_look::PickField>(Vec(L::PRESETF[0], L::PRESETF[1]));
		pf->box.size = Vec(L::PRESETF[2], L::PRESETF[3]);
		pf->accent = ACCENT;
		pf->count = [=]() { return module && module->core.api ? (int) module->core.api->npatches() : 1; };
		pf->name = [=](int i) -> std::string {
			std::string n = module && module->core.api ? module->core.api->patch_name((uint32_t) i) : "Koto Pluck";
			for (char& c : n)
				c = (char) std::toupper((unsigned char) c);
			return n;
		};
		pf->current = [=]() { return module ? (int) module->shownPatch : 0; };
		pf->choose = [=](int i) {
			if (module)
				module->patchReq = i;
		};
		addChild(pf);

		genra_look::ParamField* tune = createParam<genra_look::ParamField>(Vec(L::TUNEF[0], L::TUNEF[1]), module, Fumi::TUNE_PARAM);
		tune->box.size = Vec(L::TUNEF[2], L::TUNEF[3]);
		tune->count = 4;
		tune->nameOf = [](int i) -> std::string { return TUNE_NAMES[clamp(i, 0, 3)]; };
		tune->title = "Tuning";
		tune->undo = "change tuning";
		addParam(tune);
		genra_look::ParamField* hon = createParam<genra_look::ParamField>(Vec(L::KEYF[0], L::KEYF[1]), module, Fumi::HON_PARAM);
		hon->box.size = Vec(L::KEYF[2], L::KEYF[3]);
		hon->count = 16;
		hon->nameOf = [](int i) -> std::string { return HON_ASCII[clamp(i, 0, 15)]; };
		hon->infoOf = [](int i) -> std::string { return i < 4 ? "mizu" : i == 4 ? "san = A3" : ""; };
		hon->title = "Key (honsuu)";
		hon->undo = "change key";
		addParam(hon);
		jiant::LampRow* sc = createParam<jiant::LampRow>(Vec(0, 0), module, Fumi::SCALE_PARAM);
		sc->place(L::SCALE, 3);
		sc->accent = ACCENT;
		addParam(sc);
		jiant::LampRow* vs = createParam<jiant::LampRow>(Vec(0, 0), module, Fumi::VOICE_PARAM);
		vs->place(L::VOICE, 2);
		vs->accent = ACCENT;
		addParam(vs);
		addParam(createParamCentered<GKnob>(jiant::at(L::LEVEL), module, Fumi::LEVEL_PARAM));

		for (int k = 0; k < NS; k++) {
			Vec c = jiant::at(L::S[k]);
			if (k == 0)
				addParam(createParamCentered<GLargeKnob>(c, module, Fumi::S_PARAM + k));
			else if (k < 4)
				addParam(createParamCentered<GKnob>(c, module, Fumi::S_PARAM + k));
			else
				addParam(createParamCentered<GSmallKnob>(c, module, Fumi::S_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::S_IN[k]), module, Fumi::S_INPUT + k));
		}
		for (int k = 0; k < NH; k++) {
			if (k < 2)
				addParam(createLightParamCentered<VCVLightLatch<jiant::AccentLight<FUMI_RGB>>>(
				    jiant::at(L::H[k]), module, Fumi::H_PARAM + k, Fumi::H_LIGHT + k));
			else
				addParam(createLightParamCentered<VCVLightBezel<jiant::AccentLight<FUMI_RGB>>>(
				    jiant::at(L::H[k]), module, Fumi::H_PARAM + k, Fumi::H_LIGHT + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::H_IN[k]), module, Fumi::H_INPUT + k));
		}
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Fumi::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Fumi::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Fumi::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::BEND_IN), module, Fumi::BEND_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::KEY_IN), module, Fumi::KEY_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Fumi::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Fumi::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Fumi* m = getModule<Fumi>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Snap V/OCT to the scale", "", &m->snap));
		ui::Slider* fine = new ui::Slider;
		fine->quantity = m->paramQuantities[Fumi::FINE_PARAM];
		fine->box.size.x = 200.f;
		menu->addChild(fine);
		ui::Slider* a4 = new ui::Slider;
		a4->quantity = m->paramQuantities[Fumi::A4_PARAM];
		a4->box.size.x = 200.f;
		menu->addChild(a4);
		if (!m->core.loaded())
			return;
		std::vector<std::string> names;
		for (uint32_t i = 0; i < m->core.api->npatches(); i++)
			names.push_back(m->core.api->patch_name(i));
		menu->addChild(createIndexSubmenuItem("Instrument", names, [=]() { return (size_t) m->shownPatch; },
		                                      [=](size_t i) { m->patchReq = (int) i; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("FuMi-1 by cartesive (GPL-3.0), after the Suiko ST-50; FM: msfa (Apache-2.0)"));
	}
};

}  // namespace genra_fumi

Model* modelFumi = createModel<genra_fumi::Fumi, genra_fumi::FumiWidget>("Fumi");
