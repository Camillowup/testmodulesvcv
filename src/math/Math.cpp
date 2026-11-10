// SPDX-License-Identifier: GPL-3.0-only
// MATH: the JIANT FM firmware's MATH engine (bytebeat / floatbeat) as a VCV Rack voice, with its being drawn live.
// The engine runs unchanged in a private copy of the MATH core (MathCore.hpp); this file is the panel, the jacks and
// the glue: knobs and CV into the part's parameters, V/OCT + GATE into its notes (KEYS) or one note held with the
// formula running on (THRU), CLOCK / RESET into the song's position for MOTION SYNC, its 44.1 kHz audio resampled to
// Rack's rate. After GENRA Instruments' ANIMAL (instruments/src/jiant/Animal.cpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "MathCore.hpp"
#include "MathLayout.hpp"
#include <atomic>
#include <array>
#include <cstring>

namespace genra_math {

#define MATH_RGB 0xc0, 0x54, 0x9a   // keep in step with tools/math_panel.py

static const int NE = 6;                       // the engine's knobs: ALGO VAR MOTION GRIT/FEEDBK BEND LOOP (E2..E7)
static const char* VOICE_NAMES[3] = {"POLY", "MONO", "LEGATO"};
static const char* PLAY_NAMES[2] = {"KEYS", "THRU"};
static const char* CLOCK_MODES[4] = {"16ths (4 PPQN)", "8ths (2 PPQN)", "Quarters (1 PPQN)", "24 PPQN"};
static const int CLOCK_PPQN[4] = {4, 2, 1, 24};
enum { WAVE_BYTE = 5, WAVE_FLOAT = 6 };        // the firmware's WAVE numbers (eng_lofi.c RW_BYTE, RW_FLOAT)
enum { MOTION_SYNC = 128 };                    // MOTION 128..255: SYNC (eng_lofi.c lofi_motion)

// ------------------------------------------------------------------------------------------------ the module
struct Math : Module, follow::Host {
	enum ParamId {
		WAVE_PARAM, CHIP_PARAM,
		ENUMS(E_PARAM, NE),
		ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM,
		DIST_PARAM, CHOR_PARAM, DLY_PARAM, REV_PARAM,
		LEVEL_PARAM,
		VOICE_PARAM,
		PLAY_PARAM,
		PREV_PARAM, NEXT_PARAM, CALL_PARAM,
		PARAMS_LEN
	};
	enum InputId { ENUMS(E_INPUT, NE), VOCT_INPUT, GATE_INPUT, VEL_INPUT, CLOCK_INPUT, RESET_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { CALL_LIGHT, LIGHTS_LEN };
	enum { BLOCK = 32, CALL_NOTE = 60, THRU_NOTE = 60 };

	MathCore core;
	std::string status;

	// settings
	int preset = 0;
	int clockMode = 0;                            // CLOCK input: 16ths (GENRA's default clock out), 8ths, quarters, 24 PPQN
	bool fromPatch = false;
	std::atomic<int> presetReq{-1};

	// for the UI thread (written by the engine thread)
	std::array<std::array<char, 16>, NE> labels{};
	std::array<std::array<char, 16>, 16> values{};   // JMA_E2 .. JMA_LEVEL formatted (index j - JMA_E2)
	uint16_t screen[JMA_VIS_W * JMA_VIS_H] = {};
	std::atomic<uint32_t> screenSeq{0};
	std::atomic<int> shownPreset{0};
	std::atomic<int> voices{0};
	std::atomic<float> shownBpm{120.f};
	std::atomic<int> shownQuarter{0};
	std::atomic<bool> clocked{false};

	// engine thread
	int sent[JMA_NP];
	float devL[BLOCK], devR[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceVis = 0;
	bool visDue = true;
	jiant::ScreenClock screenClock;
	struct CvVoice { bool gate = false; int note = 60; };
	CvVoice cv[16];
	bool callOn = false;
	int playing = -1;                             // the PLAY mode the notes are in (-1: none yet)
	bool thruOn = false;                          // THRU: the held note sounds
	dsp::BooleanTrigger prevTrig, nextTrig;
	dsp::SchmittTrigger clockTrig, resetTrig;
	// the song's position (MOTION SYNC): quarter notes since RESET
	int64_t pulses = 0;                           // clock pulses since RESET
	double sincePulse = 1e9;                      // Rack samples since the last pulse
	double period = 0.0;                          // Rack samples between pulses (0: none measured)
	double freeQ = 0.0;                           // the position while no clock comes (runs on at the last tempo)
	float bpm = 120.f;
	bool resetDue = false;                        // THRU: the formula from its start (RESET, CALL)

	Math() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "MATH";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		configSwitch(WAVE_PARAM, 0.f, 1.f, 0.f, "Wave", {"BYTE (bytebeat: 8-bit formulas of the time)",
		                                                  "FLOAT (floatbeat: the formulas in floats, at the note)"});
		configSwitch(CHIP_PARAM, 0.f, 3.f, 2.f, "Chip", {"4BIT", "4B/2 (4 bit, half rate)", "8BIT / FULL", "1BIT"});
		for (int k = 0; k < NE; k++) {
			configParam<EQuantity>(E_PARAM + k, 0.f, 1.f, 0.f, string::f("E%d", k + 3));
			configInput(E_INPUT + k, string::f("E%d CV (±5 V: ± half the range)", k + 3));
		}
		const char* envN[4] = {"Attack", "Decay", "Sustain (THRU: full)", "Release"};
		const char* fxN[4] = {"Distortion", "Chorus send", "Delay send", "Reverb send"};
		for (int i = 0; i < 4; i++) {
			configParam<RawQuantity>(ATK_PARAM + i, 0.f, 127.f, 0.f, envN[i])->snapEnabled = true;
			configParam<RawQuantity>(DIST_PARAM + i, 0.f, 127.f, 0.f, fxN[i])->snapEnabled = true;
		}
		configParam<RawQuantity>(LEVEL_PARAM, 0.f, 127.f, 110.f, "Level")->snapEnabled = true;
		configSwitch(VOICE_PARAM, 0.f, 2.f, 0.f, "Voice", {"POLY", "MONO", "LEGATO"});
		configSwitch(PLAY_PARAM, 0.f, 1.f, 0.f, "Play",
		             {"KEYS (played like an instrument: V/OCT and GATE)",
		              "THRU (the formula runs on by itself: V/OCT for the pitch, no gate)"});
		configButton(PREV_PARAM, "Previous preset");
		configButton(NEXT_PARAM, "Next preset");
		configButton(CALL_PARAM, "Call (KEYS: plays C4; THRU: the formula from its start)");
		configInput(VOCT_INPUT, "V/Oct (polyphonic; THRU: the first channel)");
		configInput(GATE_INPUT, "Gate (polyphonic; not used in THRU)");
		configInput(VEL_INPUT, "Velocity (0..10 V, polyphonic)");
		configInput(CLOCK_INPUT, "Clock for MOTION SYNC (GENRA: 16ths, see the context menu)");
		configInput(RESET_INPUT, "Reset (the song's start; THRU: the formula from its start)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		for (int j = ATK_PARAM; j <= VOICE_PARAM; j++)
			paramQuantities[j]->randomizeEnabled = j < DIST_PARAM;
		paramQuantities[WAVE_PARAM]->randomizeEnabled = false;
		paramQuantities[PLAY_PARAM]->randomizeEnabled = false;
		for (int& s : sent)
			s = INT32_MIN;
	}

	/** E3..E8: the knob 0..1 over the range the firmware gives it, shown as the FM-1 writes it. */
	struct EQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Math* m = dynamic_cast<Math*>(module);
			if (m && m->core.loaded())
				return m->values[paramId - E_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
		std::string getLabel() override {
			Math* m = dynamic_cast<Math*>(module);
			if (m && m->labels[paramId - E_PARAM][0])
				return m->labels[paramId - E_PARAM].data();
			return ParamQuantity::getLabel();
		}
	};
	/** ATK .. LEVEL: the FM-1's 0..127, shown as it writes them (ms, %, dB). */
	struct RawQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Math* m = dynamic_cast<Math*>(module);
			if (m && m->core.loaded())
				return m->values[JMA_ATK - JMA_E2 + paramId - ATK_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
	};

	// ---------------------------------------------------------------------------------- power, presets
	void powerOn() {
		if (!core.load()) {
			status = core.error;
			WARN("MATH: %s", status.c_str());
			return;
		}
		status.clear();
		core.api->init(random::u32() | 1u);
		for (int& s : sent)
			s = INT32_MIN;
		outBuf.clear();
		src.reset();
		for (auto& v : cv)
			v.gate = false;
		callOn = false;
		playing = -1;
		thruOn = false;
		{
			int n = std::max(1, (int) core.api->npresets());
			preset = ((preset % n) + n) % n;
		}
		if (!fromPatch)
			applyPreset(preset);
		else
			core.api->load_preset(preset);
		shownPreset = preset;
	}

	/** The factory sound into the knobs (and the engine). */
	void applyPreset(int i) {
		const jma_api* a = core.api;
		if (!a)
			return;
		int n = (int) a->npresets();
		preset = ((i % n) + n) % n;
		a->load_preset((uint32_t) preset);
		params[WAVE_PARAM].setValue(a->get(JMA_WAVE) >= WAVE_FLOAT ? 1.f : 0.f);
		params[CHIP_PARAM].setValue((float) a->get(JMA_CHIP));
		for (int k = 0; k < NE; k++) {
			uint32_t j = JMA_E2 + k;
			int lo = a->min(j), hi = a->max(j), v = a->get(j);
			params[E_PARAM + k].setValue(hi > lo ? (float) (v - lo) / (hi - lo) : 0.f);
		}
		for (int i2 = 0; i2 < 4; i2++) {
			params[ATK_PARAM + i2].setValue((float) a->get(JMA_ATK + i2));
			params[DIST_PARAM + i2].setValue((float) a->get(JMA_DIST + i2));
		}
		for (int& s : sent)
			s = INT32_MIN;
		shownPreset = preset;
	}

	void onAdd(const AddEvent& e) override {
		params[PREV_PARAM].setValue(0.f);
		params[NEXT_PARAM].setValue(0.f);
		params[CALL_PARAM].setValue(0.f);
		powerOn();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); core.unload(); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		fromPatch = false;
		clockMode = 0;
		presetReq = 0;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "preset", json_integer(preset));
		json_object_set_new(r, "clockMode", json_integer(clockMode));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* j = json_object_get(r, "preset"))
			preset = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "clockMode"))
			clockMode = clamp((int) json_integer_value(j), 0, 3);
		fromPatch = true;
		if (core.api) {
			int n = std::max(1, (int) core.api->npresets());
			preset = ((preset % n) + n) % n;
			core.api->load_preset(preset);
			for (int& s : sent)
				s = INT32_MIN;
			shownPreset = preset;
		}
	}

	// ---------------------------------------------------------------------------------- the engine
	void push(const jma_api* a, uint32_t j, int v) {
		if (sent[j] != v) {
			a->set(j, v);
			sent[j] = v;
		}
	}

	bool thru() { return params[PLAY_PARAM].getValue() > 0.5f; }

	void processKnobs(const jma_api* a) {
		int wave = params[WAVE_PARAM].getValue() > 0.5f ? WAVE_FLOAT : WAVE_BYTE;
		if (sent[JMA_WAVE] != INT32_MIN && sent[JMA_WAVE] != wave)
			for (uint32_t j = JMA_E2; j <= JMA_E7; j++)
				sent[j] = INT32_MIN;               // (another wave: other ranges for the same knobs)
		push(a, JMA_WAVE, wave);
		push(a, JMA_CHIP, (int) std::round(params[CHIP_PARAM].getValue()));
		for (int k = 0; k < NE; k++) {
			uint32_t j = JMA_E2 + k;
			float x = params[E_PARAM + k].getValue();
			if (inputs[E_INPUT + k].isConnected())
				x += inputs[E_INPUT + k].getVoltage() / 10.f;
			x = clamp(x, 0.f, 1.f);
			int lo = a->min(j), hi = a->max(j);
			push(a, j, lo + (int) std::round(x * (hi - lo)));
		}
		bool th = thru();
		for (int i = 0; i < 4; i++) {
			int v = (int) std::round(params[ATK_PARAM + i].getValue());
			if (th && ATK_PARAM + i == SUS_PARAM)
				v = 127;                           // THRU: the held note stays at full level
			push(a, JMA_ATK + i, v);
			push(a, JMA_DIST + i, (int) std::round(params[DIST_PARAM + i].getValue()));
		}
		push(a, JMA_LEVEL, (int) std::round(params[LEVEL_PARAM].getValue()));
		int vo = (int) std::round(params[VOICE_PARAM].getValue());
		if (sent[JMA_VOICE] != INT32_MIN && sent[JMA_VOICE] != vo) {
			a->all_off();
			for (auto& v : cv)
				v.gate = false;
			thruOn = false;
		}
		push(a, JMA_VOICE, vo);
		push(a, JMA_RTRG, 1);                      // a note: the formula from its start (THRU holds one note)
	}

	/** KEYS: V/OCT + GATE (polyphonic) into notes, as ANIMAL. */
	void processKeys(const jma_api* a) {
		int n = inputs[GATE_INPUT].isConnected() ? std::max(1, inputs[GATE_INPUT].getChannels()) : 0;
		for (int c = 0; c < 16; c++) {
			CvVoice& v = cv[c];
			bool on = false;
			if (c < n) {
				float g = inputs[GATE_INPUT].getPolyVoltage(c);
				on = v.gate ? g > 0.5f : g >= 1.f;
			}
			int note = clamp((int) std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f) + 60, 0, 127);
			if (on && !v.gate) {
				int vel = 100;
				if (inputs[VEL_INPUT].isConnected())
					vel = clamp((int) std::round(inputs[VEL_INPUT].getPolyVoltage(c) / 10.f * 127.f), 1, 127);
				a->note_on(note, vel);
				v.note = note;
			} else if (!on && v.gate) {
				a->note_off(v.note);
			} else if (on && note != v.note) {
				a->note_on(note, 100);
				a->note_off(v.note);
				v.note = note;
			}
			v.gate = on;
		}
		bool call = params[CALL_PARAM].getValue() > 0.5f;
		if (call && !callOn)
			a->note_on(CALL_NOTE, 100);
		else if (!call && callOn)
			a->note_off(CALL_NOTE);
		callOn = call;
		lights[CALL_LIGHT].setBrightness(call ? 1.f : 0.f);
	}

	/** THRU: one note held all the time, the formula's time running on; V/OCT moves its pitch, no new note. */
	void processThru(const jma_api* a) {
		bool call = params[CALL_PARAM].getValue() > 0.5f;
		if (call && !callOn)
			resetDue = true;
		callOn = call;
		lights[CALL_LIGHT].setBrightness(call ? 1.f : 0.f);
		if (!thruOn || resetDue) {
			a->all_off();
			int vel = 100;
			if (inputs[VEL_INPUT].isConnected())
				vel = clamp((int) std::round(inputs[VEL_INPUT].getVoltage() / 10.f * 127.f), 1, 127);
			a->note_on(THRU_NOTE, vel);
			thruOn = true;
			resetDue = false;
		}
	}

	void processPlay(const jma_api* a) {
		int mode = thru() ? 1 : 0;
		if (mode != playing) {                    // KEYS <-> THRU: everything off, then the new way
			a->all_off();
			for (auto& v : cv)
				v.gate = false;
			callOn = false;
			thruOn = false;
			playing = mode;
		}
		if (mode)
			processThru(a);
		else
			processKeys(a);
	}

	/** CLOCK and RESET into the song's position (quarter notes since RESET), for MOTION SYNC. Without a clock the
	 * position runs on at the last tempo (120 BPM at first). */
	void processClock(float sampleRate) {
		int ppqn = CLOCK_PPQN[clamp(clockMode, 0, 3)];
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			pulses = 0;
			freeQ = 0.0;
			sincePulse = 0.0;
			if (thru())
				resetDue = true;
			clockTrig.reset();
		}
		sincePulse += 1.0;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sincePulse < sampleRate * 4.0) {  // a period from two pulses (smoothed, as a tempo does not jump)
				period = period > 0.0 ? period + (sincePulse - period) * 0.5 : sincePulse;
				bpm = clamp((float) (60.0 * sampleRate / (period * ppqn)), 20.f, 300.f);
			}
			sincePulse = 0.0;
			pulses++;
		}
		bool alive = period > 0.0 && sincePulse < std::min(period * 2.5, (double) sampleRate * 4.0);
		if (alive) {
			double frac = std::min(sincePulse / period, 0.999);
			freeQ = ((double) (pulses - 1) + frac) / ppqn;
			if (freeQ < 0.0)
				freeQ = 0.0;
		} else {
			freeQ += bpm / 60.0 / sampleRate;
		}
		clocked = alive;
	}

	void renderBlock(const jma_api* a, float sampleRate) {
		processKnobs(a);
		int devRate = (int) a->sample_rate();
		{   // the song's position at the block's start, in the engine's terms
			double q = std::max(0.0, freeQ);
			uint32_t quarter = (uint32_t) q;
			uint32_t beatLen = (uint32_t) devRate * 60u / (uint32_t) std::max(20, (int) std::round(bpm));
			uint32_t pos = std::min(beatLen - 1u, (uint32_t) ((q - quarter) * beatLen));
			a->set_clock((uint32_t) std::round(bpm), quarter, pos);
			shownBpm = bpm;
			shownQuarter = (int) quarter;
		}
		if (thru() && thruOn) {
			float voct = inputs[VOCT_INPUT].getVoltage();
			a->set_pitch((int32_t) std::round((THRU_NOTE + clamp(voct, -5.f, 5.f) * 12.f) * 16.f));
		}
		a->render(BLOCK, devL, devR);
		dsp::Frame<2> in[BLOCK];
		for (int i = 0; i < BLOCK; i++) {
			in[i].samples[0] = devL[i];
			in[i].samples[1] = devR[i];
		}
		src.process(in, BLOCK, devRate, (int) sampleRate, outBuf);
		framesSinceVis += BLOCK;
		if (visDue && screenClock.seen()) {
			visDue = false;
			std::memcpy(screen, a->vis(JMA_VIS_W, JMA_VIS_H), sizeof screen);
			screenSeq++;
		}
		if (framesSinceVis >= (uint32_t) devRate / 30) {
			framesSinceVis = 0;
			visDue = true;
			for (int k = 0; k < NE; k++)
				std::snprintf(labels[k].data(), labels[k].size(), "%s", a->label(JMA_E2 + k));
			for (uint32_t j = JMA_E2; j <= JMA_LEVEL; j++)
				a->format(j, a->get(j), values[j - JMA_E2].data(), (uint32_t) values[j - JMA_E2].size());
			int mo = a->get(JMA_E4);                 // MOTION: 128..255 is SYNC
			if (mo >= MOTION_SYNC)
				std::snprintf(values[JMA_E4 - JMA_E2].data(), 16, "SYNC %d", mo - MOTION_SYNC);
			voices = (int) a->active_voices();
		}
	}

	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jma_api* a = core.api;
		if (!a) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		int req = presetReq.exchange(-1);
		bool want = req >= 0;
		if (prevTrig.process(params[PREV_PARAM].getValue() > 0.5f)) {
			req = preset - 1;
			want = true;
		}
		if (nextTrig.process(params[NEXT_PARAM].getValue() > 0.5f)) {
			req = preset + 1;
			want = true;
		}
		if (want) {
			fromPatch = false;
			applyPreset(req);
		}
		processClock(args.sampleRate);
		processPlay(a);
		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(a, args.sampleRate);
		dsp::Frame<2> f = outBuf.empty() ? dsp::Frame<2>{} : outBuf.shift();
		float l = f.samples[0] * 5.f, r = f.samples[1] * 5.f;
		if (outputs[R_OUTPUT].isConnected() || pulled(args.frame)) {
			outputs[L_OUTPUT].setVoltage(l);
			outputs[R_OUTPUT].setVoltage(r);
		} else {
			outputs[L_OUTPUT].setVoltage(0.5f * (l + r));
			outputs[R_OUTPUT].setVoltage(0.f);
		}
	}
};

// ------------------------------------------------------------------------------------------------ the panel (GENRA look)
namespace L = mlayout::math;
static const NVGcolor ACCENT = nvgRGB(MATH_RGB);

/** The being (the firmware's own drawing), drawn in the light layer. */
struct ScreenWidget : Widget {
	Math* module = nullptr;
	jiant::Canvas565 canvas{JMA_VIS_W, JMA_VIS_H};
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		if (!module) {
			jiant::text(args.vg, jiant::boldFont(), box.size.x / 2, box.size.y / 2 + 5, "MATH", 14.f, ACCENT,
			            NVG_ALIGN_CENTER, 2.f);
			return;
		}
		if (!module->status.empty()) {
			jiant::drawStatus(args.vg, module->status, box.size.x);
			return;
		}
		module->screenClock.mark();
		canvas.draw(args.vg, module->screen, module->screenSeq, box.size.x, box.size.y);
	}
};

/** Right of the being: the preset, WAVE · CHIP · PLAY · VOICE, the clock, the first three knobs' values. */
struct InfoWidget : Widget {
	Math* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x;
		const jma_api* a = module ? module->core.api : nullptr;
		int p = module ? (int) module->shownPreset : 0;
		bool flt = module && module->params[Math::WAVE_PARAM].getValue() > 0.5f;
		int chip = module ? clamp((int) std::round(module->params[Math::CHIP_PARAM].getValue()), 0, 3) : 2;
		int play = module && module->params[Math::PLAY_PARAM].getValue() > 0.5f ? 1 : 0;
		int vo = module ? clamp((int) std::round(module->params[Math::VOICE_PARAM].getValue()), 0, 2) : 0;
		static const char* CHIPS[2][4] = {{"4BIT", "4B/2", "8BIT", "1BIT"}, {"4BIT", "4B/2", "FULL", "1BIT"}};
		jiant::text(vg, bold, 0, 20, a ? a->preset_name((uint32_t) p) : "BYTEBEAT", 15.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.6f);
		jiant::text(vg, bold, w, 20, string::f("\xc2\xb7 %d/%u", p + 1, a ? a->npresets() : 12u), 6.5f, jiant::DIM,
		            NVG_ALIGN_RIGHT, 0.8f);
		jiant::text(vg, mono, 0, 33, string::f("%s \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 %s", flt ? "FLOAT" : "BYTE",
		                                       CHIPS[flt][chip], PLAY_NAMES[play], VOICE_NAMES[vo]),
		            7.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		// (the WAVE and CHIP chips at y 41 are their own widgets)
		if (module) {
			bool c = module->clocked;
			jiant::text(vg, mono, 0, 71, string::f("TIME %s \xc2\xb7 %d BPM \xc2\xb7 Q %d", c ? "clock" : "free",
			                                       (int) std::round((float) module->shownBpm), (int) module->shownQuarter),
			            7.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		}
		if (module && module->core.loaded()) {
			std::string s;
			for (int k = 0; k < 3; k++)
				s += string::f("%s %s  ", module->labels[k].data(), module->values[k].data());
			jiant::text(vg, mono, 0, 92, s, 6.6f, ACCENT, NVG_ALIGN_LEFT, 0.f);
		}
	}
};

/** CHIP's chips: their names follow WAVE (8BIT for BYTE, FULL for FLOAT). */
struct ChipChips : jiant::ChipRow {
	void step() override {
		ParamWidget::step();
		Math* m = module ? dynamic_cast<Math*>(module) : nullptr;
		bool flt = m && m->params[Math::WAVE_PARAM].getValue() > 0.5f;
		names = {"4BIT", "4B/2", flt ? "FULL" : "8BIT", "1BIT"};
	}
};

struct MathWidget : ModuleWidget {
	MathWidget(Math* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Math.svg")));
		follow::addStrip(this, module, {jiant::at(L::VOCT), jiant::at(L::GATE), jiant::at(L::VEL), jiant::at(L::CLOCK),
		                                jiant::at(L::RESET)});
		jiant::addScrews(this);

		ScreenWidget* scr = new ScreenWidget;
		scr->module = module;
		scr->box = jiant::area(L::SCREEN);
		addChild(scr);
		InfoWidget* info = new InfoWidget;
		info->module = module;
		info->box = jiant::area(L::INFO);
		addChild(info);
		jiant::ChipRow* waves = createParam<jiant::ChipRow>(Vec(L::INFO[0], L::INFO[1] + 41), module, Math::WAVE_PARAM);
		waves->box.size = Vec(75, 14);
		waves->names = {"BYTE", "FLOAT"};
		waves->accent = ACCENT;
		addParam(waves);
		ChipChips* chips = createParam<ChipChips>(Vec(L::INFO[0] + 82, L::INFO[1] + 41), module, Math::CHIP_PARAM);
		chips->box.size = Vec(L::INFO[2] - 82, 14);
		chips->names = {"4BIT", "4B/2", "8BIT", "1BIT"};
		chips->accent = ACCENT;
		addParam(chips);

		addParam(createParamCentered<VCVBezel>(jiant::at(L::PREV), module, Math::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(jiant::at(L::NEXT), module, Math::NEXT_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<jiant::AccentLight<MATH_RGB>>>(
		    jiant::at(L::CALL), module, Math::CALL_PARAM, Math::CALL_LIGHT));
		jiant::LampRow* pl = createParam<jiant::LampRow>(Vec(0, 0), module, Math::PLAY_PARAM);
		pl->place(L::PLAY, 2);
		pl->accent = ACCENT;
		addParam(pl);
		jiant::LampRow* vs = createParam<jiant::LampRow>(Vec(0, 0), module, Math::VOICE_PARAM);
		vs->place(L::VOICE, 3);
		vs->accent = ACCENT;
		addParam(vs);
		addParam(createParamCentered<GSmallKnob>(jiant::at(L::LEVEL), module, Math::LEVEL_PARAM));

		static const char* defLab[NE] = {"ALGO", "VAR", "MOTION", "GRIT", "BEND", "LOOP"};
		for (int k = 0; k < NE; k++) {
			addParam(createParamCentered<GSmallKnob>(jiant::at(L::E[k]), module, Math::E_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::E_IN[k]), module, Math::E_INPUT + k));
			jiant::KnobText* t = new jiant::KnobText;
			t->box = jiant::area(L::E_TEXT[k]);
			t->accent = ACCENT;
			t->label = [=]() -> std::string {
				if (module && module->labels[k][0])
					return module->labels[k].data();
				return defLab[k];
			};
			t->value = [=]() -> std::string { return module && module->core.loaded() ? module->values[k].data() : ""; };
			addChild(t);
		}
		const float* env[4] = {L::ATK, L::DEC, L::SUS, L::REL};
		const float* fx[4] = {L::DIST, L::CHOR, L::DLY, L::REV};
		for (int i = 0; i < 4; i++) {
			addParam(createParamCentered<GTrimpot>(jiant::at(env[i]), module, Math::ATK_PARAM + i));
			addParam(createParamCentered<GTrimpot>(jiant::at(fx[i]), module, Math::DIST_PARAM + i));
		}
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Math::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Math::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Math::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::CLOCK), module, Math::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::RESET), module, Math::RESET_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Math::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Math::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Math* m = getModule<Math>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {CLOCK_MODES[0], CLOCK_MODES[1], CLOCK_MODES[2], CLOCK_MODES[3]},
		                                      [=]() { return (size_t) m->clockMode; },
		                                      [=](size_t i) { m->clockMode = (int) i; }));
		if (!m->core.loaded())
			return;
		std::vector<std::string> names;
		for (uint32_t i = 0; i < m->core.api->npresets(); i++)
			names.push_back(m->core.api->preset_name(i));
		menu->addChild(createIndexSubmenuItem("Preset", names, [=]() { return (size_t) m->shownPreset; },
		                                      [=](size_t i) { m->presetReq = (int) i; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MOTION past half: SYNC (the formula's rhythm and melody in time with the clock)"));
		menu->addChild(createMenuLabel("Engine: MATH from JIANT FM by Juan Jiant (a Felucca fork), GPL-3.0"));
	}
};

}  // namespace genra_math

Model* modelMath = createModel<genra_math::Math, genra_math::MathWidget>("Math");
