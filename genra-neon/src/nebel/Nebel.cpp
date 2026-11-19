// SPDX-License-Identifier: GPL-3.0-only
// NEBEL – the cluster universe of GENRA NEON (after the idea of a cluster oscillator): every note becomes a cloud of
// voices; the display is a galaxy, the voices its stars on an orbit, joined like a constellation. With CHORD on GENRA
// it plays GENRA's chord (the poly V/OCT of the pad track) instead of the chord of the field.
// Engine: NebelDsp.hpp; panel: tools/neon_panel.py (res/Nebel.svg + NebelLayout.hpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/ui/PresetBank.hpp"
#include "../neon/NeonUI.hpp"
#include "NebelDsp.hpp"
#include "NebelPresets.hpp"
#include "NebelLayout.hpp"
#include <memory>

namespace nebel {

namespace L = nl;
using namespace neonui;
static const int PPQN_CHOICES[4] = {1, 2, 4, 24};
static const char* const SRC_NAMES[NUM_SRCS] = {"Panel (the chord field)", "GENRA (the notes that come in)"};
static const char* const CHORD_SHORT[NUM_CHORDS] = {"MIN9", "MAJ7", "MIN7", "MAJ9", "SUS4", "5", "OCT", "ADD9", "MIN11",
                                                    "9", "QUART", "CLUST", "WHOLE", "CHAOS"};

struct Nebel : Module, follow::Host {
	enum ParamId {
		GEN_PARAM, TIMBRE_PARAM, MORPH_PARAM, OCT_PARAM, CHORD_PARAM, SPREAD_PARAM, VOICES_PARAM, DETUNE_PARAM, CHAOS_PARAM,
		DRIFT_PARAM, STRUM_PARAM, ACCENT_PARAM,
		TEXT_PARAM, AMOUNT_PARAM, COLOR_PARAM, DENSITY_PARAM, WIDTH_PARAM,
		SPACE_PARAM, SIZE_PARAM, DECAY_PARAM, SMIX_PARAM, TONE_PARAM,
		ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM,
		ORATE_PARAM, ODEPTH_PARAM, MACRO_PARAM, GLIDE_PARAM, CSRC_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,
		BANG_PARAM, FREEZE_PARAM, UNDO_PARAM, INIT_PARAM,
		LEVEL_PARAM,  // the output level (not in a preset)
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, CLOCK_INPUT, RESET_INPUT, TBANG_INPUT, TFREEZE_INPUT,
		TIMBRE_INPUT, MORPH_INPUT, GEN_INPUT, CHORD_INPUT, SPREAD_INPUT, VOICES_INPUT, DETUNE_INPUT, CHAOS_INPUT,
		INL_INPUT, INR_INPUT, STRUM_INPUT, TEXT_INPUT, AMOUNT_INPUT, COLOR_INPUT, SIZE_INPUT, DECAY_INPUT, SMIX_INPUT,
		ORBIT_INPUT, DEPTH_INPUT, MACRO_INPUT, ATK_INPUT, REL_INPUT, GLIDE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENV_OUTPUT, ORBIT_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };
	static_assert(NUM_PARAMS == NUM_PRESET_PARAMS, "presets must cover every knob");
	static_assert(INPUTS_LEN == L::NUM_JACK_IN, "the panel's jacks");

	struct TimeQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			float t = neon::envTime(getValue());
			return t < 1.f ? string::f("%.0f", t * 1000.f) : string::f("%.2f", t);
		}
		std::string getUnit() override { return neon::envTime(getValue()) < 1.f ? " ms" : " s"; }
	};
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return neon::RATE_NAMES[clamp((int) std::round(getValue()), 0, neon::NUM_RATES - 1)]; }
	};

	std::unique_ptr<Engine> engine{new Engine};
	Params p;
	Channel ch[8];
	int ctl = 0;
	int ppqn = 4;
	uint32_t seed = 1;
	dsp::SchmittTrigger clockTrig, resetTrig, bangTrig, freezeTrig, bangBtn, undoBtn, initBtn;
	float sinceClock = 0.f, bpm = 120.f;
	bool clockSeen = false;
	std::atomic<uint32_t> bangReq{0}, undoReq{0}, initReq{0};
	uint32_t doneB = 0, doneU = 0, doneI = 0;
	float flash = 0.f;
	// jumps (a preset, generator, texture, space) happen while the output is muted for a moment (no clicks)
	neon::SwitchFade sfade;
	float appliedGain = -1.f, lastBankGain = -1.f;
	int lastPreset = -2, lastGen = -1, lastText = -1, lastSpace = -1;
	bool restrikeDue = false;

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return GEN_NAMES[PRESETS[i].gen]; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraNeon/NebelPresets"));
	}

	Nebel() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "NEBEL";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(ACC_INPUT, follow::ACC);
		fol.bind(SLIDE_INPUT, follow::SLIDE);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		std::vector<std::string> gens, chords, texts, spaces;
		for (int i = 0; i < NUM_GENS; i++)
			gens.push_back(GEN_NAMES[i]);
		for (int i = 0; i < NUM_CHORDS; i++)
			chords.push_back(CHORD_NAMES[i]);
		for (int i = 0; i < NUM_TEXTURES; i++)
			texts.push_back(TEXT_NAMES[i]);
		for (int i = 0; i < neon::NUM_VERBS; i++)
			spaces.push_back(neon::VERB_NAMES[i]);
		configSwitch(GEN_PARAM, 0.f, NUM_GENS - 1, 0.f, "Generator", gens);
		configParam(TIMBRE_PARAM, 0.f, 1.f, 0.5f, "Timbre", "%", 0.f, 100.f);
		configParam(MORPH_PARAM, 0.f, 1.f, 0.3f, "Morph", "%", 0.f, 100.f);
		paramQuantities[MORPH_PARAM]->description =
		    "Wavetable: through four tables · Analog: brightness · Additive: odd / even, stretch · Modal: ring time · String: bow";
		configParam(OCT_PARAM, -3.f, 3.f, 0.f, "Octave");
		paramQuantities[OCT_PARAM]->snapEnabled = true;
		configSwitch(CHORD_PARAM, 0.f, NUM_CHORDS - 1, 0.f, "Chord", chords);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.4f, "Spread (the voices over the octaves)", "%", 0.f, 100.f);
		configParam(VOICES_PARAM, 1.f, (float) MAXVG, 6.f, "Voices");
		paramQuantities[VOICES_PARAM]->snapEnabled = true;
		configParam(DETUNE_PARAM, 0.f, 1.f, 0.3f, "Detune", "%", 0.f, 100.f);
		configParam(CHAOS_PARAM, 0.f, 1.f, 0.1f, "Chaos (the voices leave the chord)", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.3f, "Drift (slow wandering)", "%", 0.f, 100.f);
		configParam(STRUM_PARAM, 0.f, 1.f, 0.f, "Strum (the voices one after the other)", "%", 0.f, 100.f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent amount", "%", 0.f, 100.f);
		configSwitch(TEXT_PARAM, 0.f, NUM_TEXTURES - 1, 0.f, "Texture", texts);
		configParam(AMOUNT_PARAM, 0.f, 1.f, 0.3f, "Texture amount", "%", 0.f, 100.f);
		configParam(COLOR_PARAM, 0.f, 1.f, 0.5f, "Texture color", "%", 0.f, 100.f);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Texture density", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.7f, "Width", "%", 0.f, 100.f);
		configSwitch(SPACE_PARAM, 0.f, neon::NUM_VERBS - 1, 0.f, "Space (reverb)", spaces);
		configParam(SIZE_PARAM, 0.f, 1.f, 0.7f, "Size", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.6f, "Decay", "%", 0.f, 100.f);
		configParam(SMIX_PARAM, 0.f, 1.f, 0.4f, "Space mix", "%", 0.f, 100.f);
		configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone (dark .. bright)", "%", 0.f, 100.f);
		configParam<TimeQuantity>(ATK_PARAM, 0.f, 1.f, 0.5f, "Attack");
		configParam<TimeQuantity>(DEC_PARAM, 0.f, 1.f, 0.6f, "Decay");
		configParam(SUS_PARAM, 0.f, 1.f, 0.8f, "Sustain", "%", 0.f, 100.f);
		configParam<TimeQuantity>(REL_PARAM, 0.f, 1.f, 0.6f, "Release");
		configParam<RateQuantity>(ORATE_PARAM, 0.f, neon::NUM_RATES - 1, 4.f, "Orbit rate (in time with CLOCK / GENRA)");
		paramQuantities[ORATE_PARAM]->snapEnabled = true;
		configParam(ODEPTH_PARAM, 0.f, 1.f, 0.4f, "Orbit depth (the voices turn in the stereo field)", "%", 0.f, 100.f);
		configParam(MACRO_PARAM, 0.f, 1.f, 0.f, "Macro (more of everything: timbre, spread, chaos, texture, space)", "%", 0.f, 100.f);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.1f, "Glide");
		configSwitch(CSRC_PARAM, 0.f, NUM_SRCS - 1, 0.f, "Chord from", {"Panel", "GENRA"});
		paramQuantities[CSRC_PARAM]->description = "GENRA: the notes that come in (GENRA's chord on the pad track) make the cluster";
		configButton(BANG_PARAM, "Big bang: a new cluster (chord, spread, voices, chaos …)");
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze: holds the cluster", {"Off", "On"});
		configButton(UNDO_PARAM, "Undo the last BIG BANG / INIT");
		configButton(INIT_PARAM, "Init: back to a plain cluster");
		for (int id : {BANG_PARAM, UNDO_PARAM, INIT_PARAM, FREEZE_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		configParam(LEVEL_PARAM, 0.f, 1.4125f, 1.f, "Level", " dB", -10.f, 20.f);
		paramQuantities[LEVEL_PARAM]->randomizeEnabled = false;
		const char* inNames[INPUTS_LEN] = {
			"V/Oct (polyphonic)", "Gate (polyphonic)", "Velocity (0–10 V)", "Accent (gate)", "Slide (glide only on a slide)",
			"Clock (ORBIT in time; GENRA: 16ths)", "Reset (ORBIT from the start)", "Big bang trigger", "Freeze trigger (toggles)",
			"Timbre CV", "Morph CV", "Generator CV (0–10 V through the five)", "Chord CV (0–10 V through the chords)",
			"Spread CV", "Voices CV (0–10 V: 1–8)", "Detune CV", "Chaos CV", "Audio in left (into texture and space)",
			"Audio in right", "Strum CV", "Texture CV (0–10 V through the five)", "Texture amount CV", "Texture color CV",
			"Size CV", "Decay CV", "Space mix CV", "Orbit rate CV", "Orbit depth CV", "Macro CV", "Attack CV", "Release CV",
			"Glide CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(ENV_OUTPUT, "Envelope (0–10 V)");
		configOutput(ORBIT_OUTPUT, "Orbit (±5 V, in time)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		initPresets();
	}

	void onAdd(const AddEvent& e) override { engine->setSampleRate(APP->engine->getSampleRate()); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { engine->setSampleRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine->allOff();
		bank.reset();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	float kv(int param, int input) { return clamp(params[param].getValue() + inputs[input].getVoltage() * 0.1f, 0.f, 1.f); }
	int ki(int param, int input, int n) {
		return clamp((int) std::round(params[param].getValue() + inputs[input].getVoltage() * 0.1f * n), 0, n - 1);
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		sinceClock += args.sampleTime;
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			clockSeen = false;
			sinceClock = 0.f;
			engine->resetOrbit();
		}
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clockSeen && sinceClock > 0.005f && sinceClock < 4.f)
				bpm = clamp(60.f / (sinceClock * ppqn), 20.f, 400.f);
			clockSeen = true;
			sinceClock = 0.f;
		}
		if (!inputs[CLOCK_INPUT].isConnected())
			bpm = 120.f, clockSeen = false;
		if (bangTrig.process(inputs[TBANG_INPUT].getVoltage(), 0.1f, 1.f) | bangBtn.process(params[BANG_PARAM].getValue()))
			bangReq++;
		if (undoBtn.process(params[UNDO_PARAM].getValue()))
			undoReq++;
		if (initBtn.process(params[INIT_PARAM].getValue()))
			initReq++;
		if (freezeTrig.process(inputs[TFREEZE_INPUT].getVoltage(), 0.1f, 1.f))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > 0.5f ? 0.f : 1.f);

		if (--ctl <= 0) {
			ctl = 16;
			// a jump (preset, generator, texture, space): mute first; the old sound keeps its knobs until it is silent
			int ng = ki(GEN_PARAM, GEN_INPUT, NUM_GENS), nt = ki(TEXT_PARAM, TEXT_INPUT, NUM_TEXTURES);
			int nsp = clamp((int) std::round(params[SPACE_PARAM].getValue()), 0, neon::NUM_VERBS - 1);
			if (bank.preset != lastPreset || bank.gain != lastBankGain ||
			    (lastGen >= 0 && (ng != lastGen || nt != lastText || nsp != lastSpace))) {
				if (appliedGain < 0.f)
					appliedGain = bank.gain;
				else
					sfade.trigger(true);
				lastPreset = bank.preset;
				lastBankGain = bank.gain;
			}
			lastGen = ng, lastText = nt, lastSpace = nsp;
		}
		if (ctl == 16 && sfade.state != 1) {
			p.gen = ki(GEN_PARAM, GEN_INPUT, NUM_GENS);
			p.timbre = kv(TIMBRE_PARAM, TIMBRE_INPUT);
			p.morph = kv(MORPH_PARAM, MORPH_INPUT);
			p.oct = params[OCT_PARAM].getValue();
			p.chord = ki(CHORD_PARAM, CHORD_INPUT, NUM_CHORDS);
			p.spread = kv(SPREAD_PARAM, SPREAD_INPUT);
			p.voices = clamp((int) std::round(params[VOICES_PARAM].getValue() + inputs[VOICES_INPUT].getVoltage() * 0.8f), 1, MAXVG);
			p.detune = kv(DETUNE_PARAM, DETUNE_INPUT);
			p.chaos = kv(CHAOS_PARAM, CHAOS_INPUT);
			p.drift = params[DRIFT_PARAM].getValue();
			p.strum = kv(STRUM_PARAM, STRUM_INPUT);
			p.accent = params[ACCENT_PARAM].getValue();
			p.texture = ki(TEXT_PARAM, TEXT_INPUT, NUM_TEXTURES);
			p.amount = kv(AMOUNT_PARAM, AMOUNT_INPUT);
			p.color = kv(COLOR_PARAM, COLOR_INPUT);
			p.density = params[DENSITY_PARAM].getValue();
			p.width = params[WIDTH_PARAM].getValue();
			p.space = clamp((int) std::round(params[SPACE_PARAM].getValue()), 0, neon::NUM_VERBS - 1);
			p.size = kv(SIZE_PARAM, SIZE_INPUT);
			p.decay = kv(DECAY_PARAM, DECAY_INPUT);
			p.smix = kv(SMIX_PARAM, SMIX_INPUT);
			p.tone = params[TONE_PARAM].getValue();
			p.atk = kv(ATK_PARAM, ATK_INPUT);
			p.dec = params[DEC_PARAM].getValue();
			p.sus = params[SUS_PARAM].getValue();
			p.rel = kv(REL_PARAM, REL_INPUT);
			p.orate = clamp((int) std::round(params[ORATE_PARAM].getValue() + inputs[ORBIT_INPUT].getVoltage() * 1.1f), 0,
			                neon::NUM_RATES - 1);
			p.odepth = clamp(params[ODEPTH_PARAM].getValue() + inputs[DEPTH_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
			p.macro = kv(MACRO_PARAM, MACRO_INPUT);
			p.glide = kv(GLIDE_PARAM, GLIDE_INPUT);
			if (inputs[SLIDE_INPUT].isConnected() && inputs[SLIDE_INPUT].getVoltage() < 1.f)
				p.glide = 0.f;  // with SLIDE patched: glide only on a slide
			p.chordSrc = clamp((int) std::round(params[CSRC_PARAM].getValue()), 0, NUM_SRCS - 1);
			p.outGain = appliedGain < 0.f ? bank.gain : appliedGain;
			p.freeze = params[FREEZE_PARAM].getValue() > 0.5f;
			p.seed = seed;
			if (restrikeDue) {
				restrikeDue = false;
				engine->restrike(p);
			}
		}
		int n = std::min(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), 8);
		bool vel = inputs[VEL_INPUT].isConnected();
		for (int c = 0; c < n; c++) {
			Channel& x = ch[c];
			x.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			float g = inputs[GATE_INPUT].getPolyVoltage(c);
			x.gate = g >= 1.f ? true : (g <= 0.1f ? false : x.gate);
			x.vel = vel ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 0.8f;
			x.acc = inputs[ACC_INPUT].getPolyVoltage(c) >= 1.f;
		}
		bool bottom;
		float fg = sfade.step(args.sampleRate, bottom);
		if (bottom) {
			if (sfade.wantReset) {
				engine->softReset();
				restrikeDue = true;
			}
			sfade.wantReset = false;
			appliedGain = p.outGain = bank.gain;
			ctl = 1;  // the new knobs at once
		}
		float l, r, env, orb;
		engine->process(p, ch, n, bpm, inputs[INL_INPUT].getVoltage() * 0.2f,
		                inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT].getVoltage() * 0.2f : inputs[INL_INPUT].getVoltage() * 0.2f,
		                l, r, env, orb);
		float lvl = fg * params[LEVEL_PARAM].getValue();
		l *= lvl, r *= lvl;
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[ENV_OUTPUT].setVoltage(10.f * env);
		outputs[ORBIT_OUTPUT].setVoltage(5.f * orb);
	}

	// ---------------------------------------------------------------- UI thread: BIG BANG, UNDO, INIT
	std::vector<json_t*> undoStack;
	~Nebel() {
		for (json_t* j : undoStack)
			json_decref(j);
	}
	void pushUndo() {
		undoStack.push_back(toJson());
		if (undoStack.size() > 16) {
			json_decref(undoStack.front());
			undoStack.erase(undoStack.begin());
		}
	}
	void bigBang() {
		pushUndo();
		auto rnd = [](float a, float b) { return a + (b - a) * random::uniform(); };
		seed = 1u + (random::u32() % 999999u);
		int chord = (int) (random::uniform() * (NUM_CHORDS - 0.01f));
		params[CHORD_PARAM].setValue((float) chord);
		params[SPREAD_PARAM].setValue(rnd(0.1f, 0.9f));
		params[VOICES_PARAM].setValue((float) (int) rnd(3.f, 8.99f));
		params[DETUNE_PARAM].setValue(rnd(0.1f, 0.6f));
		params[CHAOS_PARAM].setValue(random::uniform() < 0.3f ? rnd(0.2f, 0.6f) : rnd(0.f, 0.15f));
		params[DRIFT_PARAM].setValue(rnd(0.1f, 0.7f));
		params[STRUM_PARAM].setValue(random::uniform() < 0.3f ? rnd(0.1f, 0.5f) : 0.f);
		params[TIMBRE_PARAM].setValue(clamp(params[TIMBRE_PARAM].getValue() + rnd(-0.25f, 0.25f), 0.f, 1.f));
		params[MORPH_PARAM].setValue(clamp(params[MORPH_PARAM].getValue() + rnd(-0.25f, 0.25f), 0.f, 1.f));
		flash = 0.4f;
	}
	void doInit() {
		pushUndo();
		for (int i = 0; i < NUM_PARAMS; i++)
			params[i].setValue(paramQuantities[i]->getDefaultValue());
		params[FREEZE_PARAM].setValue(0.f);
		seed = 1;
		bank.reset();
	}
	void doUndo() {
		if (undoStack.empty())
			return;
		json_t* j = undoStack.back();
		undoStack.pop_back();
		fromJson(j);
		json_decref(j);
	}
	void uiStep(float dt) {
		uint32_t b = bangReq.load(), u = undoReq.load(), in = initReq.load();
		if (b != doneB)
			doneB = b, bigBang();
		if (u != doneU)
			doneU = u, doUndo();
		if (in != doneI)
			doneI = in, doInit();
		if (flash > 0.f)
			flash -= dt;
	}

	int hostPresetCount() override { return bank.count(); }
	void hostPresetInfo(int i, char* name, int nl, char* style, int sl) override {
		follow::putStr(name, nl, bank.nameOf(i).c_str());
		follow::putStr(style, sl, i >= 0 && i < bank.f.count && bank.f.style ? bank.f.style(i) : "");
	}
	int hostPresetCurrent() override { return bank.preset; }
	void hostPresetLoad(int i) override { bank.load(i); }

	json_t* dataToJson() override {
		json_t* j = json_object();
		json_object_set_new(j, "ppqn", json_integer(ppqn));
		json_object_set_new(j, "seed", json_integer((json_int_t) seed));
		bank.toJson(j);
		fol.toJson(j);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		if (json_t* v = json_object_get(j, "ppqn"))
			ppqn = std::max(1, (int) json_integer_value(v));
		if (json_t* v = json_object_get(j, "seed"))
			seed = std::max<uint32_t>(1u, (uint32_t) json_integer_value(v));
		bank.fromJson(j);
	}
};

// ---------------------------------------------------------------- the display: a galaxy and the voices as stars
struct Display : Widget {
	Nebel* module = nullptr;
	struct Star {
		float x, y, r, a;
	};
	std::vector<Star> stars;
	struct Dot {
		float ang, rad, r, a;
		int col;
	};
	std::vector<Dot> dots;
	float spin = 0.f;
	Display() {
		neon::Rng r(5);
		for (int i = 0; i < 260; i++)
			stars.push_back({r.uni(), r.uni(), 0.3f + 0.7f * r.uni(), 0.25f + 0.65f * r.uni()});
		for (int arm = 0; arm < 2; arm++)
			for (int k = 0; k < 320; k++) {
				float t = k / 320.f * 3.2f * M_PI;
				dots.push_back({t + arm * (float) M_PI + 0.15f * r.bi(), (1.2f + 3.6f * t) / 37.f + 0.02f * r.bi(),
				                0.3f + 0.8f * r.uni(), 0.35f + 0.6f * r.uni(), k < 140 ? 0 : (k < 230 ? 1 : 2)});
			}
	}
	void step() override {
		Widget::step();
		if (module) {
			module->uiStep(1.f / 60.f);
			if (!module->p.freeze)
				spin += 0.002f + 0.004f * module->p.odepth;
		} else
			spin += 0.003f;
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y, cx = W / 2, cy = H * 0.5f;
		nvgSave(vg);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		for (const Star& s : stars) {
			nvgBeginPath(vg);
			nvgCircle(vg, s.x * W, s.y * H, s.r);
			nvgFillColor(vg, nvgRGBAf(1, 1, 1, s.a));
			nvgFill(vg);
		}
		// the galaxy's glow and arms
		nvgBeginPath(vg);
		nvgEllipse(vg, cx, cy, W * 0.36f, H * 0.4f);
		nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 2.f, H * 0.5f, alpha(VIOLET, 0.22f), alpha(VIOLET, 0.f)));
		nvgFill(vg);
		const NVGcolor cols[3] = {PINK, VIOLET, CYAN};
		float rx = W * 0.48f, ry = H * 0.62f;
		for (const Dot& d : dots) {
			float a = d.ang + spin;
			nvgBeginPath(vg);
			nvgCircle(vg, cx + std::cos(a) * d.rad * rx, cy + std::sin(a) * d.rad * ry * 0.62f, d.r);
			nvgFillColor(vg, alpha(cols[d.col], d.a));
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 4.5f);
		nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 1.f, 9.f, WHITE, alpha(WHITE, 0.f)));
		nvgFill(vg);
		// the voices: stars on an orbit, joined
		float orx = W * 0.26f, ory = H * 0.33f;
		nvgBeginPath(vg);
		nvgEllipse(vg, cx, cy, orx, ory);
		nvgStrokeColor(vg, alpha(CYAN, 0.35f));
		nvgStrokeWidth(vg, 0.5f);
		nvgStroke(vg);
		Vec pts[MAXVOICES];
		float lv[MAXVOICES];
		float pitch[MAXVOICES];
		int n = 0;
		float orb = module ? module->engine->orbitPh : 0.f;
		for (int i = 0; i < MAXVOICES; i++) {
			bool on = module ? module->engine->showOn[i] : i < 8;
			if (!on)
				continue;
			float a = -M_PI / 2 + i * 2 * M_PI / (module ? std::max(1, std::min(16, module->engine->activeVoices)) : 8) +
			          (module ? module->p.odepth : 0.4f) * 2.f * M_PI * orb;
			pts[n] = Vec(cx + orx * std::cos(a), cy + ory * std::sin(a));
			lv[n] = module ? clamp(module->engine->showLevel[i], 0.f, 1.f) : 0.8f;
			pitch[n] = module ? module->engine->showPitch[i] : (float) i / 7.f;
			n++;
		}
		if (n > 1) {
			nvgBeginPath(vg);
			for (int i = 0; i < n; i++)
				i ? nvgLineTo(vg, pts[i].x, pts[i].y) : nvgMoveTo(vg, pts[i].x, pts[i].y);
			nvgClosePath(vg);
			nvgStrokeColor(vg, alpha(CYAN, 0.5f));
			nvgStrokeWidth(vg, 0.6f);
			nvgStroke(vg);
		}
		for (int i = 0; i < n; i++) {
			NVGcolor c = i < 4 ? WHITE : (i % 2 ? PINK : CYAN);
			float rr = 1.5f + 2.5f * lv[i];
			nvgBeginPath(vg);
			nvgCircle(vg, pts[i].x, pts[i].y, rr * 3.f);
			nvgFillPaint(vg, nvgRadialGradient(vg, pts[i].x, pts[i].y, rr * 0.5f, rr * 3.f, alpha(c, 0.5f * lv[i] + 0.1f), alpha(c, 0.f)));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgCircle(vg, pts[i].x, pts[i].y, rr);
			nvgFillColor(vg, c);
			nvgFill(vg);
			char nn[8];
			neon::noteName(pitch[i], nn);
			text(vg, pts[i].x, pts[i].y + (pts[i].y < cy ? -rr - 3.f : rr + 8.f), nn, 5.4f, LBL, NVG_ALIGN_CENTER, 0.6f);
		}
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		float pad = mm2px(1.5f);
		plaque(vg, pad, pad, mm2px(44.f), mm2px(9.4f), false);
		plaque(vg, W - pad - mm2px(46.f), pad, mm2px(46.f), mm2px(10.6f), true);
		std::string l1 = "WAVETABLE · MINOR 9", l2 = "8 VOICES · CHAOS 25 %", big = "C MIN9", fol = "FREE";
		if (module) {
			const Params& p = module->p;
			bool genra = p.chordSrc == SRC_GENRA;
			l1 = std::string(GEN_NAMES[p.gen]) + " · " + (genra ? "GENRA CHORD" : CHORD_NAMES[p.chord]);
			l2 = string::f("%d VOICES · CHAOS %d %% · %s", module->engine->activeVoices, (int) std::round(p.chaos * 100.f),
			               TEXT_NAMES[p.texture]);
			char nn[8];
			neon::noteName(module->engine->lastNote, nn);
			std::string root = nn;
			while (!root.empty() && (std::isdigit((unsigned char) root.back()) || root.back() == '-'))
				root.pop_back();
			big = genra ? root + " CHORD" : root + " " + CHORD_SHORT[p.chord];
			if (module->fol.active()) {
				int tr = module->fol.track.load();
				fol = std::string("FOLLOW · ") + (tr >= 0 && tr < follow::NUM_TRACKS ? follow::TRACK_NAMES[tr] : "");
			} else
				fol = module->inputs[Nebel::CLOCK_INPUT].isConnected() ? string::f("%.0f BPM", module->bpm) : "FREE · 120 BPM";
			fol += std::string(" · ") + neon::VERB_NAMES[p.space];
			if (p.freeze)
				fol += " · FREEZE";
		}
		text(vg, pad + mm2px(2.f), pad + mm2px(4.f), l1, 6.6f, CYAN, NVG_ALIGN_LEFT, 1.2f);
		text(vg, pad + mm2px(2.f), pad + mm2px(7.4f), l2, 5.8f, DIM, NVG_ALIGN_LEFT, 1.0f);
		text(vg, W - pad - mm2px(2.f), pad + mm2px(5.8f), big, 15.f, WHITE, NVG_ALIGN_RIGHT, 1.2f);
		text(vg, W - pad - mm2px(2.f), pad + mm2px(9.2f), fol, 5.4f, DIM, NVG_ALIGN_RIGHT, 1.0f);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct NebelWidget : ModuleWidget {
	NebelWidget(Nebel* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Nebel.svg")));
		follow::knownHost(module);
		using M = Nebel;
		Display* d = new Display;
		d->module = module;
		d->box.pos = mm2px(Vec(L::DISPLAY[0], L::DISPLAY[1]));
		d->box.size = mm2px(Vec(L::DISPLAY[2], L::DISPLAY[3]));
		addChild(d);

		genra_presets::PresetField* pf = createWidget<genra_presets::PresetField>(mm2px(Vec(L::PRESETF[0], L::PRESETF[1])));
		pf->box.size = mm2px(Vec(L::PRESETF[2], L::PRESETF[3]));
		pf->bank = module ? &module->bank : nullptr;
		pf->module = module;
		pf->paramId = M::PRESETSEL_PARAM;
		pf->accent = PINK;
		pf->sample = PRESETS[0].name;
		addChild(pf);

		auto field = [&](const float* r, int id, int n, const char* const* names, const char* const* info, const char* title) {
			Field* f = createParam<Field>(Vec(0, 0), module, id);
			f->setup(r);
			f->count = [=]() { return n; };
			f->name = [=](int i) { return std::string(names[i]); };
			f->info = [=](int i) { return std::string(info[i]); };
			f->title = title;
			f->undo = std::string("change ") + string::lowercase(title);
			addParam(f);
		};
		field(L::GENF, M::GEN_PARAM, NUM_GENS, GEN_NAMES, GEN_INFO, "Generator");
		field(L::CHORDF, M::CHORD_PARAM, NUM_CHORDS, CHORD_NAMES, CHORD_INFO, "Chord");
		field(L::TEXTF, M::TEXT_PARAM, NUM_TEXTURES, TEXT_NAMES, TEXT_INFO, "Texture");
		field(L::SPACEF, M::SPACE_PARAM, neon::NUM_VERBS, neon::VERB_NAMES, neon::VERB_INFO, "Space");

		auto knob = [&](const float* p, int id) {
			NKnob* k = createParam<NKnob>(Vec(0, 0), module, id);
			k->setup(p);
			addParam(k);
		};
		const std::pair<const float*, int> knobs[] = {
			{L::TIMBRE, M::TIMBRE_PARAM}, {L::MORPH, M::MORPH_PARAM},   {L::OCT, M::OCT_PARAM},       {L::SPREAD, M::SPREAD_PARAM},
			{L::VOICES, M::VOICES_PARAM}, {L::DETUNE, M::DETUNE_PARAM}, {L::CHAOS, M::CHAOS_PARAM},   {L::DRIFT, M::DRIFT_PARAM},
			{L::STRUM, M::STRUM_PARAM},   {L::ACCENT, M::ACCENT_PARAM}, {L::AMOUNT, M::AMOUNT_PARAM}, {L::COLOR, M::COLOR_PARAM},
			{L::DENSITY, M::DENSITY_PARAM}, {L::WIDTH, M::WIDTH_PARAM}, {L::SIZE, M::SIZE_PARAM},     {L::DECAY, M::DECAY_PARAM},
			{L::SMIX, M::SMIX_PARAM},     {L::TONE, M::TONE_PARAM},     {L::ATK, M::ATK_PARAM},       {L::DEC, M::DEC_PARAM},
			{L::SUS, M::SUS_PARAM},       {L::REL, M::REL_PARAM},       {L::ORATE, M::ORATE_PARAM},   {L::ODEPTH, M::ODEPTH_PARAM},
			{L::MACRO, M::MACRO_PARAM},   {L::GLIDE, M::GLIDE_PARAM},   {L::LEVEL, M::LEVEL_PARAM}};
		for (auto& k : knobs)
			knob(k.first, k.second);
		NLamps* lr = createParam<NLamps>(Vec(0, 0), module, M::CSRC_PARAM);
		lr->place(L::CSRC, NUM_SRCS);
		lr->accent = CYAN;
		addParam(lr);

		auto button = [&](const float* r, int id, bool latch, NVGcolor col) {
			NButton* b = createParam<NButton>(Vec(0, 0), module, id);
			b->setup(r, latch);
			b->col = col;
			addParam(b);
			return b;
		};
		NButton* bb = button(L::BANG, M::BANG_PARAM, false, PINK);
		bb->lit = [=]() { return module && module->flash > 0.f; };
		button(L::FREEZE, M::FREEZE_PARAM, true, CYAN);
		button(L::UNDO, M::UNDO_PARAM, false, VIOLET);
		button(L::INIT, M::INIT_PARAM, false, VIOLET);

		for (int i = 0; i < M::INPUTS_LEN; i++)
			addPort<NPort>(this, L::JACK_IN[i], module, i, false, hex(L::JACK_IN_COL[i]));
		for (int i = 0; i < M::OUTPUTS_LEN; i++)
			addPort<NPort>(this, L::JACK_OUT[i], module, i, true, PINK);
		std::vector<Vec> strip;
		for (int i : {M::VOCT_INPUT, M::GATE_INPUT, M::VEL_INPUT, M::ACC_INPUT, M::SLIDE_INPUT, M::CLOCK_INPUT, M::RESET_INPUT})
			strip.push_back(mm(L::JACK_IN[i]));
		follow::addStrip(this, module, strip);
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Nebel* m = dynamic_cast<Nebel*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem(
		    "CLOCK input", {"Quarters (1 PPQN)", "8ths (2 PPQN)", "16ths (4 PPQN, GENRA)", "24 PPQN"},
		    [=]() {
			    for (int i = 0; i < 4; i++)
				    if (PPQN_CHOICES[i] == m->ppqn)
					    return (size_t) i;
			    return (size_t) 2;
		    },
		    [=](size_t i) { m->ppqn = PPQN_CHOICES[i]; }));
		menu->addChild(createMenuLabel("With CHORD on GENRA: follow GENRA's pad track (its chord makes the cluster)"));
		(void) SRC_NAMES;
	}
};

}  // namespace nebel

Model* modelNebel = createModel<nebel::Nebel, nebel::NebelWidget>("Nebel");
