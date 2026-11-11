// SPDX-License-Identifier: GPL-3.0-only
// SWARM: the JIANT FM firmware's SWARM (a drone made of the devices heard around) as a VCV Rack voice, without a
// radio: virtual devices around it (VOICES, BUSY, MOVE, NEAR; ARRIVE and NEW), drawn as the firmware draws them. The
// engine runs unchanged in a private copy of the SWARM core (SwarmCore.hpp); this file is the panel, the jacks and the
// glue: knobs and CV into the part's parameters, V/OCT into its transpose, GATE as the instrument's gate in KEYS (the
// gate's length the sound's, its voltage the level, a short ramp against clicks), CLOCK / RESET into the lead's grid,
// its 44.1 kHz audio resampled to Rack's rate. After MATH (src/math/Math.cpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "../../genra/ui/Look.hpp"
#include "SwarmCore.hpp"
#include "SwarmLayout.hpp"
#include <atomic>
#include <array>
#include <cstring>

namespace genra_swarm {

#define SWARM_RGB 0x13, 0xb8, 0xa6   // keep in step with tools/jpanel.py COLORS

static const int NE = 6;                       // PLUCK FM IMAGE DRIFT OCTAVE CHAOS
static const char* PLAY_NAMES[2] = {"THRU", "KEYS"};
static const char* CLOCK_MODES[4] = {"16ths (4 PPQN)", "8ths (2 PPQN)", "Quarters (1 PPQN)", "24 PPQN"};
static const int CLOCK_PPQN[4] = {4, 2, 1, 24};
static const char* ROOT_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// ------------------------------------------------------------------------------------------------ the module
struct Swarm : Module, follow::Host {
	enum ParamId {
		ENUMS(E_PARAM, NE),
		DIST_PARAM, CHOR_PARAM, DLY_PARAM, REV_PARAM,
		LEVEL_PARAM,
		VOICES_PARAM, BUSY_PARAM, MOVE_PARAM, NEAR_PARAM,
		PLAY_PARAM,
		PREV_PARAM, NEXT_PARAM, NEW_PARAM,
		PARAMS_LEN
	};
	enum InputId { ENUMS(E_INPUT, NE), VOCT_INPUT, GATE_INPUT, VEL_INPUT, CLOCK_INPUT, RESET_INPUT,
		           NEAR_INPUT, ARRIVE_INPUT, NEW_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { NEW_LIGHT, LIGHTS_LEN };
	enum { BLOCK = 32 };

	SwarmCore core;
	std::string status;

	// settings
	int preset = 0;
	int clockMode = 0;
	int root = 0, scale = -1;                     // ROOT, SCALE (-1: the preset's)
	uint32_t seed = 0;                            // the swarm (0: a new one at power on)
	bool fromPatch = false;
	std::atomic<int> presetReq{-1};
	std::atomic<bool> newReq{false};

	// for the UI thread (written by the engine thread)
	std::array<std::array<char, 16>, NE> labels{};
	std::array<std::array<char, 16>, JSW_NP> values{};
	uint16_t screen[JSW_VIS_W * JSW_VIS_H] = {};
	std::atomic<uint32_t> screenSeq{0};
	std::atomic<int> shownPreset{0};
	std::atomic<int> shownDevices{0};
	std::atomic<int> shownNear{0};
	std::atomic<float> shownBpm{120.f};
	std::atomic<bool> clocked{false};
	std::atomic<float> shownGate{1.f};

	// engine thread
	int sent[JSW_NP];
	float devL[BLOCK], devR[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceVis = 0;
	bool visDue = true;
	jiant::ScreenClock screenClock;
	dsp::BooleanTrigger prevTrig, nextTrig, newTrig;
	dsp::SchmittTrigger clockTrig, resetTrig, arriveTrig, newInTrig;
	float gain = 1.f;                             // the gate's level now (KEYS), ramped
	float rampK = 0.f, rampDt = 0.f;              // its ramp's coefficient, for this sample time
	bool powered = false;                         // the core runs (powerOn): before that it is loaded but not started
	float newFlash = 0.f;
	int key = 0;
	// the song's position (the lead's grid): samples at the engine's rate since RESET
	double sincePulse = 1e9, period = 0.0;
	int64_t pulses = 0;
	double engPos = 0.0;
	float bpm = 120.f;

	Swarm() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "SWARM";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		static const char* EN[NE] = {"Pluck", "FM", "Image", "Drift", "Octave", "Chaos"};
		for (int k = 0; k < NE; k++) {
			configParam<EQuantity>(E_PARAM + k, 0.f, 1.f, 0.f, EN[k]);
			configInput(E_INPUT + k, string::f("%s CV (±5 V: ± half the range)", EN[k]));
		}
		const char* fxN[4] = {"Distortion", "Chorus send", "Delay send", "Reverb send"};
		for (int i = 0; i < 4; i++)
			configParam<RawQuantity>(DIST_PARAM + i, 0.f, 127.f, 0.f, fxN[i])->snapEnabled = true;
		configParam<RawQuantity>(LEVEL_PARAM, 0.f, 127.f, 110.f, "Level")->snapEnabled = true;
		configParam(VOICES_PARAM, 1.f, 8.f, 4.f, "Voices (the devices around)")->snapEnabled = true;
		configParam(BUSY_PARAM, 0.f, 1.f, 0.5f, "Busy (how often they send)", "%", 0.f, 100.f);
		configParam(MOVE_PARAM, 0.f, 1.f, 0.3f, "Move (they walk around, come and go)", "%", 0.f, 100.f);
		configParam(NEAR_PARAM, 0.f, 1.f, 0.5f, "Near (how close they are: FAR .. NEAR)", "%", 0.f, 100.f);
		configSwitch(PLAY_PARAM, 0.f, 1.f, 0.f, "Play",
		             {"THRU (it hums all the time)", "KEYS (played like an instrument: GATE, V/OCT transposes)"});
		configButton(PREV_PARAM, "Previous preset");
		configButton(NEXT_PARAM, "Next preset");
		configButton(NEW_PARAM, "New swarm (every device another)");
		configInput(VOCT_INPUT, "V/Oct (transposes the swarm; C4 as it is)");
		configInput(GATE_INPUT, "Gate (KEYS: the sound while it is high; its voltage the level, 10 V full)");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(CLOCK_INPUT, "Clock for the lead's grid (GENRA: 16ths, see the context menu)");
		configInput(RESET_INPUT, "Reset (the song's start)");
		configInput(NEAR_INPUT, "Near CV (added to the knob: 10 V the whole way)");
		configInput(ARRIVE_INPUT, "Arrive (trigger: a new device comes)");
		configInput(NEW_INPUT, "New (trigger: a new swarm)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		paramQuantities[PLAY_PARAM]->randomizeEnabled = false;
		paramQuantities[VOICES_PARAM]->randomizeEnabled = false;
		for (int& s : sent)
			s = INT32_MIN;
		core.load();                               // here (the UI thread), not in onAdd: the engine waits there
	}

	struct EQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Swarm* m = dynamic_cast<Swarm*>(module);
			if (m && m->core.loaded())
				return m->values[paramId - E_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
	};
	struct RawQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Swarm* m = dynamic_cast<Swarm*>(module);
			if (m && m->core.loaded())
				return m->values[JSW_DIST + paramId - DIST_PARAM].data();
			return ParamQuantity::getDisplayValueString();
		}
	};

	// ---------------------------------------------------------------------------------- power, presets
	void powerOn() {
		if (!core.loaded() && !core.load()) {
			status = core.error;
			WARN("SWARM: %s", status.c_str());
			return;
		}
		status.clear();
		if (!seed)
			seed = random::u32() | 1u;
		core.api->init(seed);
		for (int& s : sent)
			s = INT32_MIN;
		outBuf.clear();
		src.reset();
		int n = std::max(1, (int) core.api->npresets());
		preset = ((preset % n) + n) % n;
		if (!fromPatch)
			applyPreset(preset);
		else
			core.api->load_preset(preset);
		shownPreset = preset;
		powered = true;
	}

	void applyPreset(int i) {
		const jsw_api* a = core.api;
		if (!a)
			return;
		int n = (int) a->npresets();
		preset = ((i % n) + n) % n;
		a->load_preset((uint32_t) preset);
		for (int k = 0; k < NE; k++) {
			uint32_t j = JSW_PLUCK + k;
			int lo = a->min(j), hi = a->max(j), v = a->get(j);
			params[E_PARAM + k].setValue(hi > lo ? (float) (v - lo) / (hi - lo) : 0.f);
		}
		for (int i2 = 0; i2 < 4; i2++)
			params[DIST_PARAM + i2].setValue((float) a->get(JSW_DIST + i2));
		for (int& s : sent)
			s = INT32_MIN;
		shownPreset = preset;
	}

	void onAdd(const AddEvent& e) override {
		params[PREV_PARAM].setValue(0.f);
		params[NEXT_PARAM].setValue(0.f);
		params[NEW_PARAM].setValue(0.f);
		powerOn();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); powered = false; core.unload(); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		fromPatch = false;
		clockMode = 0;
		root = 0;
		scale = -1;
		presetReq = 0;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "preset", json_integer(preset));
		json_object_set_new(r, "clockMode", json_integer(clockMode));
		json_object_set_new(r, "root", json_integer(root));
		json_object_set_new(r, "scale", json_integer(scale));
		json_object_set_new(r, "seed", json_integer(powered && core.api ? core.api->seed() : seed));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* j = json_object_get(r, "preset"))
			preset = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "clockMode"))
			clockMode = clamp((int) json_integer_value(j), 0, 3);
		if (json_t* j = json_object_get(r, "root"))
			root = clamp((int) json_integer_value(j), 0, 11);
		if (json_t* j = json_object_get(r, "scale"))
			scale = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "seed"))
			seed = (uint32_t) json_integer_value(j);
		fromPatch = true;
		if (powered && core.api) {
			core.api->renew(seed ? seed : 1u);
			int n = std::max(1, (int) core.api->npresets());
			preset = ((preset % n) + n) % n;
			core.api->load_preset(preset);
			for (int& s : sent)
				s = INT32_MIN;
			shownPreset = preset;
		}
	}

	// ---------------------------------------------------------------------------------- the engine
	void push(const jsw_api* a, uint32_t j, int v) {
		if (sent[j] != v) {
			a->set(j, v);
			sent[j] = v;
		}
	}

	bool keys() { return params[PLAY_PARAM].getValue() > 0.5f; }

	void processKnobs(const jsw_api* a) {
		for (int k = 0; k < NE; k++) {
			uint32_t j = JSW_PLUCK + k;
			float x = params[E_PARAM + k].getValue();
			if (inputs[E_INPUT + k].isConnected())
				x += inputs[E_INPUT + k].getVoltage() / 10.f;
			x = clamp(x, 0.f, 1.f);
			int lo = a->min(j), hi = a->max(j);
			push(a, j, lo + (int) std::round(x * (hi - lo)));
		}
		for (int i = 0; i < 4; i++)
			push(a, JSW_DIST + i, (int) std::round(params[DIST_PARAM + i].getValue()));
		push(a, JSW_LEVEL, (int) std::round(params[LEVEL_PARAM].getValue()));
		push(a, JSW_ROOT, root);
		if (scale >= 0)
			push(a, JSW_SCALE, clamp(scale, a->min(JSW_SCALE), a->max(JSW_SCALE)));
		float nearv = params[NEAR_PARAM].getValue();
		if (inputs[NEAR_INPUT].isConnected())
			nearv += inputs[NEAR_INPUT].getVoltage() / 10.f;
		a->air((uint32_t) std::round(params[VOICES_PARAM].getValue()), params[BUSY_PARAM].getValue(),
		       params[MOVE_PARAM].getValue(), clamp(nearv, 0.f, 1.f));
		a->set_key(key);
	}

	/** CLOCK and RESET into the lead's grid; without a clock it runs on at the last tempo. */
	void processClock(float sampleRate, int devRate) {
		int ppqn = CLOCK_PPQN[clamp(clockMode, 0, 3)];
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			pulses = 0;
			engPos = 0.0;
			sincePulse = 0.0;
			clockTrig.reset();
		}
		sincePulse += 1.0;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sincePulse < sampleRate * 4.0) {
				period = period > 0.0 ? period + (sincePulse - period) * 0.5 : sincePulse;
				bpm = clamp((float) (60.0 * sampleRate / (period * ppqn)), 20.f, 300.f);
			}
			sincePulse = 0.0;
			pulses++;
		}
		bool alive = period > 0.0 && sincePulse < std::min(period * 2.5, (double) sampleRate * 4.0);
		double beat = (double) devRate * 60.0 / bpm;
		if (alive) {
			double frac = std::min(sincePulse / period, 0.999);
			engPos = std::max(0.0, ((double) (pulses - 1) + frac) / ppqn) * beat;
		} else
			engPos += (double) devRate / sampleRate;
		if (alive != clocked.load(std::memory_order_relaxed))
			clocked.store(alive, std::memory_order_relaxed);
	}

	void renderBlock(const jsw_api* a, float sampleRate) {
		processKnobs(a);
		int devRate = (int) a->sample_rate();
		a->set_clock((uint32_t) std::round(bpm), (uint32_t) std::fmod(engPos, 4294967296.0));
		shownBpm = bpm;
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
			std::memcpy(screen, a->vis(JSW_VIS_W, JSW_VIS_H), sizeof screen);
			screenSeq++;
		}
		if (framesSinceVis >= (uint32_t) devRate / 30) {
			framesSinceVis = 0;
			visDue = true;
			for (int k = 0; k < NE; k++)
				std::snprintf(labels[k].data(), labels[k].size(), "%s", a->label(JSW_PLUCK + k));
			for (uint32_t j = 0; j < JSW_NP; j++)
				a->format(j, a->get(j), values[j].data(), (uint32_t) values[j].size());
			shownDevices = (int) a->devices();
			shownNear = (int) a->closeness();
		}
	}

	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jsw_api* a = core.api;
		if (!a) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		int req = presetReq.load(std::memory_order_relaxed) >= 0 ? presetReq.exchange(-1) : -1;
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
		bool renew = newReq.load(std::memory_order_relaxed) && newReq.exchange(false);
		renew |= newTrig.process(params[NEW_PARAM].getValue() > 0.5f);
		renew |= newInTrig.process(inputs[NEW_INPUT].getVoltage(), 0.1f, 1.f);
		if (renew) {
			seed = random::u32() | 1u;
			a->renew(seed);
			newFlash = 0.15f;
		}
		if (arriveTrig.process(inputs[ARRIVE_INPUT].getVoltage(), 0.1f, 1.f)) {
			a->arrive();
			newFlash = std::max(newFlash, 0.08f);
		}
		newFlash = std::max(0.f, newFlash - args.sampleTime);
		lights[NEW_LIGHT].setBrightness(newFlash > 0.f || params[NEW_PARAM].getValue() > 0.5f ? 1.f : 0.f);
		processClock(args.sampleRate, (int) a->sample_rate());

		// V/OCT transposes (THRU and KEYS); KEYS: the gate is the sound (its voltage the level)
		if (inputs[VOCT_INPUT].isConnected())
			key = clamp((int) std::round(inputs[VOCT_INPUT].getVoltage() * 12.f), -36, 36);
		else
			key = 0;
		float target = 1.f;
		if (keys()) {
			target = 0.f;
			if (inputs[GATE_INPUT].isConnected())
				target = clamp(inputs[GATE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
			if (inputs[VEL_INPUT].isConnected())
				target *= clamp(inputs[VEL_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		}
		// a short ramp against clicks (~3 ms)
		if (args.sampleTime != rampDt) {            // (the same value as before, worked out once per sample rate)
			rampDt = args.sampleTime;
			rampK = 1.f - std::exp(-args.sampleTime / 0.003f);
		}
		gain += (target - gain) * rampK;
		shownGate.store(gain, std::memory_order_relaxed);

		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(a, args.sampleRate);
		dsp::Frame<2> f = outBuf.empty() ? dsp::Frame<2>{} : outBuf.shift();
		float l = f.samples[0] * 5.f * gain, r = f.samples[1] * 5.f * gain;
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
namespace L = slayout::swarm;
static const NVGcolor ACCENT = nvgRGB(SWARM_RGB);

/** The swarm (the firmware's own drawing), drawn in the light layer. */
struct ScreenWidget : Widget {
	Swarm* module = nullptr;
	jiant::Canvas565 canvas{JSW_VIS_W, JSW_VIS_H};
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		if (!module) {
			jiant::text(args.vg, jiant::boldFont(), box.size.x / 2, box.size.y / 2 + 5, "SWARM", 14.f, ACCENT,
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

/** Right of the swarm: the preset, PLAY · devices · ROOT SCALE, the closeness, the tempo, the first knobs. */
struct InfoWidget : Widget {
	Swarm* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont();
		float w = box.size.x, h = box.size.y;
		const jsw_api* a = module ? module->core.api : nullptr;
		int p = module ? (int) module->shownPreset : 0;
		int play = module && module->params[Swarm::PLAY_PARAM].getValue() > 0.5f ? 1 : 0;
		std::string sc = module && module->core.loaded() ? module->values[JSW_SCALE].data() : "CHR";
		std::string rt = module && module->core.loaded() ? module->values[JSW_ROOT].data() : "C";
		int nd = module ? (int) module->shownDevices : 4;
		jiant::text(vg, bold, 0, 7.f, string::f("PRESET %d / %u", p + 1, a ? a->npresets() : 3u), 5.2f, jiant::DIM,
		            NVG_ALIGN_LEFT, 1.f);
		jiant::text(vg, bold, 0, h * 0.5f, a ? a->preset_name((uint32_t) p) : "SWARM", 13.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.6f);
		jiant::text(vg, bold, 0, h * 0.5f + 9.f, string::f("%s \xc2\xb7 %d DEVICE%s \xc2\xb7 %s %s", PLAY_NAMES[play], nd,
		                                                  nd == 1 ? "" : "S", rt.c_str(), sc.c_str()),
		            5.2f, ACCENT, NVG_ALIGN_LEFT, 1.f);
		// FAR .. NEAR: the morph
		int mq = module ? (int) module->shownNear : 64;
		float bx = 16.f, bw = w * 0.45f;
		jiant::text(vg, bold, 0, h - 1.f, "FAR", 5.f, jiant::DIM, NVG_ALIGN_LEFT, 0.8f);
		jiant::roundRect(vg, bx, h - 5.f, bw, 4.f, 2.f, nvgRGB(0x2a, 0x2c, 0x33));
		jiant::roundRect(vg, bx, h - 5.f, bw * clamp(mq / 128.f, 0.f, 1.f), 4.f, 2.f, ACCENT);
		jiant::text(vg, bold, bx + bw + 3.f, h - 1.f, "NEAR", 5.f, jiant::DIM, NVG_ALIGN_LEFT, 0.8f);
		if (module) {
			bool c = module->clocked;
			jiant::text(vg, bold, w, 7.f, string::f("%d BPM%s", (int) std::round((float) module->shownBpm), c ? "" : " FREE"),
			            5.2f, jiant::DIM, NVG_ALIGN_RIGHT, 1.f);
			if (play)
				jiant::text(vg, bold, w, h * 0.5f, string::f("GATE %d%%", (int) std::round(module->shownGate * 100.f)), 5.2f,
				            jiant::DIM, NVG_ALIGN_RIGHT, 1.f);
		}
		if (module && module->core.loaded()) {
			std::string s;
			for (int k = 0; k < 3; k++)
				s += string::f("%s%s %s", k ? "  " : "", module->labels[k].data(), module->values[k].data());
			jiant::text(vg, bold, w, h - 1.f, s, 5.2f, jiant::DIM, NVG_ALIGN_RIGHT, 0.6f);
		}
	}
};

struct SwarmWidget : ModuleWidget {
	SwarmWidget(Swarm* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Swarm.svg")));
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

		genra_look::PickField* pf = createWidget<genra_look::PickField>(Vec(L::PRESETF[0], L::PRESETF[1]));
		pf->box.size = Vec(L::PRESETF[2], L::PRESETF[3]);
		pf->accent = ACCENT;
		pf->count = [=]() { return module && module->core.api ? (int) module->core.api->npresets() : 1; };
		pf->name = [=](int i) -> std::string {
			return module && module->core.api ? module->core.api->preset_name((uint32_t) i) : "SWARM";
		};
		pf->current = [=]() { return module ? (int) module->shownPreset : 0; };
		pf->choose = [=](int i) {
			if (module)
				module->presetReq = i;
		};
		addChild(pf);

		addParam(createLightParamCentered<VCVLightBezel<jiant::AccentLight<SWARM_RGB>>>(
		    jiant::at(L::NEW), module, Swarm::NEW_PARAM, Swarm::NEW_LIGHT));
		jiant::LampRow* pl = createParam<jiant::LampRow>(Vec(0, 0), module, Swarm::PLAY_PARAM);
		pl->place(L::PLAY, 2);
		pl->accent = ACCENT;
		addParam(pl);
		addParam(createParamCentered<GKnob>(jiant::at(L::VOICES), module, Swarm::VOICES_PARAM));
		addParam(createParamCentered<GKnob>(jiant::at(L::LEVEL), module, Swarm::LEVEL_PARAM));

		static const char* defLab[NE] = {"PLUCK", "FM", "IMAGE", "DRIFT", "OCTAVE", "CHAOS"};
		for (int k = 0; k < NE; k++) {
			Vec c = jiant::at(L::E[k]);
			if (k == 0)
				addParam(createParamCentered<GLargeKnob>(c, module, Swarm::E_PARAM + k));
			else if (k < 4)
				addParam(createParamCentered<GKnob>(c, module, Swarm::E_PARAM + k));
			else
				addParam(createParamCentered<GSmallKnob>(c, module, Swarm::E_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::E_IN[k]), module, Swarm::E_INPUT + k));
			auto label = [=]() -> std::string {
				if (module && module->labels[k][0])
					return module->labels[k].data();
				return defLab[k];
			};
			genra_look::PanelText* t = new genra_look::PanelText;
			t->box = jiant::area(L::E_TEXT[k]);
			t->text = label;
			addChild(t);
			genra_look::PanelText* j = new genra_look::PanelText;
			j->box = jiant::area(L::E_INL[k]);
			j->text = label;
			j->color = jiant::DIM;
			j->size = 4.4f;
			j->spacing = 0.7f;
			addChild(j);
		}
		const float* air[3] = {L::BUSY, L::MOVE, L::NEAR};
		for (int i = 0; i < 3; i++)
			addParam(createParamCentered<GSmallKnob>(jiant::at(air[i]), module, Swarm::BUSY_PARAM + i));
		const float* fx[4] = {L::DIST, L::CHOR, L::DLY, L::REV};
		for (int i = 0; i < 4; i++)
			addParam(createParamCentered<GSmallKnob>(jiant::at(fx[i]), module, Swarm::DIST_PARAM + i));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Swarm::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Swarm::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Swarm::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::CLOCK), module, Swarm::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::RESET), module, Swarm::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::NEAR_IN), module, Swarm::NEAR_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::ARRIVE_IN), module, Swarm::ARRIVE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::NEW_IN), module, Swarm::NEW_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Swarm::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Swarm::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Swarm* m = getModule<Swarm>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {CLOCK_MODES[0], CLOCK_MODES[1], CLOCK_MODES[2], CLOCK_MODES[3]},
		                                      [=]() { return (size_t) m->clockMode; },
		                                      [=](size_t i) { m->clockMode = (int) i; }));
		std::vector<std::string> roots(ROOT_NAMES, ROOT_NAMES + 12);
		menu->addChild(createIndexSubmenuItem("Root (the devices' notes)", roots, [=]() { return (size_t) m->root; },
		                                      [=](size_t i) { m->root = (int) i; }));
		if (!m->core.loaded())
			return;
		const jsw_api* a = m->core.api;
		std::vector<std::string> scales;
		for (int v = a->min(JSW_SCALE); v <= a->max(JSW_SCALE); v++) {
			char b[24];
			a->format(JSW_SCALE, v, b, sizeof b);
			scales.push_back(b);
		}
		int lo = a->min(JSW_SCALE);
		menu->addChild(createIndexSubmenuItem("Scale (the devices' notes)", scales,
		                                      [=]() { return (size_t) std::max(0, a->get(JSW_SCALE) - lo); },
		                                      [=](size_t i) { m->scale = (int) i + lo; }));
		std::vector<std::string> names;
		for (uint32_t i = 0; i < a->npresets(); i++)
			names.push_back(a->preset_name(i));
		menu->addChild(createIndexSubmenuItem("Preset", names, [=]() { return (size_t) m->shownPreset; },
		                                      [=](size_t i) { m->presetReq = (int) i; }));
		menu->addChild(createMenuItem("New swarm", "", [=]() { m->newReq = true; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("No radio: the devices are virtual (their addresses, makers, bytes, clocks)"));
		menu->addChild(createMenuLabel("Engine: SWARM from JIANT FM by Juan Jiant (a Felucca fork), GPL-3.0"));
	}
};

}  // namespace genra_swarm

Model* modelSwarm = createModel<genra_swarm::Swarm, genra_swarm::SwarmWidget>("Swarm");
