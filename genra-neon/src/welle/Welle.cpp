// SPDX-License-Identifier: GPL-3.0-only
// WELLE – the wavetable synth of GENRA NEON, in the NEON-TERRAIN look: the table is a neon mountain range over the
// display, the frame that plays now its glowing ridge. Two oscillators (FOLDER/TABLE field, TABLE, POSITION, WARP),
// filter, MOTION, envelopes, unison, the bus; MUTATE (new tables from the building blocks), FREEZE (holds the frame),
// UNDO / INIT; Follow GENRA (GENRA and GENRA TECHNO), presets (also via MIDI-MAP), many CV inputs, no LFO (patch one).
// Engine: WelleDsp.hpp; tables: TableBank.hpp; panel: tools/neon_panel.py (res/Welle.svg + WelleLayout.hpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/ui/PresetBank.hpp"
#include "../neon/NeonUI.hpp"
#include "WelleDsp.hpp"
#include "WellePresets.hpp"
#include "WelleLayout.hpp"
#include "TableBank.hpp"
#include <osdialog.h>
#include <memory>

namespace welle {

namespace L = wl;
using namespace neonui;
static const int PPQN_CHOICES[4] = {1, 2, 4, 24};

struct Welle : Module, follow::Host {
	enum ParamId {
		FOLDA_PARAM, TBLA_PARAM, POSA_PARAM, WARPMA_PARAM, WARPA_PARAM, OCTA_PARAM,
		FOLDB_PARAM, TBLB_PARAM, POSB_PARAM, WARPMB_PARAM, WARPB_PARAM, TUNEB_PARAM,
		FTYPE_PARAM, CUTOFF_PARAM, RES_PARAM, FENV_PARAM, DRIVE_PARAM, KEY_PARAM, FATK_PARAM, FDEC_PARAM,
		SCAN_PARAM, RATE_PARAM, PENV_PARAM,
		CHORUS_PARAM, DELAY_PARAM, REVERB_PARAM,
		ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM,
		MIX_PARAM, SUB_PARAM, UNISON_PARAM, SPREAD_PARAM,
		VMODE_PARAM, GLIDE_PARAM, ACCENT_PARAM,
		NUM_PARAMS,
		PRESETSEL_PARAM = NUM_PARAMS,  // after the knobs: not in a preset
		MUTATE_PARAM, FREEZE_PARAM, UNDO_PARAM, INIT_PARAM,
		LEVEL_PARAM,  // the output level (not in a preset)
		PARAMS_LEN
	};
	// in the order of the panel (WelleLayout.hpp JACK_IN)
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, CLOCK_INPUT, RESET_INPUT, TMUTATE_INPUT, TFREEZE_INPUT,
		POSA_INPUT, POSB_INPUT, WARPA_INPUT, WARPB_INPUT, TBLA_INPUT, TBLB_INPUT, MIX_INPUT, SUB_INPUT,
		FM_INPUT, UNISON_INPUT, SPREAD_INPUT, CUT_INPUT, RES_INPUT, FENV_INPUT, DRIVE_INPUT, SCAN_INPUT, RATE_INPUT,
		ATK_INPUT, DEC_INPUT, REL_INPUT, GLIDE_INPUT, DELAY_INPUT, VERB_INPUT,
		INPUTS_LEN
	};
	enum OutputId { ENV_OUTPUT, MOTION_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
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
	struct CutoffQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			float f = 20.f * std::pow(2.f, 10.f * getValue());
			return f < 1000.f ? string::f("%.0f", f) : string::f("%.2f k", f / 1000.f);
		}
		std::string getUnit() override { return "Hz"; }
	};
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return neon::RATE_NAMES[clamp((int) std::round(getValue()), 0, neon::NUM_RATES - 1)]; }
	};
	struct FolderQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Welle* w = dynamic_cast<Welle*>(module);
			return w ? w->tables->folderInfo((int) std::round(getValue())).name : "";
		}
	};
	struct TableQuantity : ParamQuantity {
		int osc = 0;
		std::string getDisplayValueString() override {
			Welle* w = dynamic_cast<Welle*>(module);
			if (!w)
				return "";
			FolderInfo f = w->tables->folderInfo((int) std::round(w->params[osc == 0 ? FOLDA_PARAM : FOLDB_PARAM].getValue()));
			int n = f.count();
			int i = n > 1 ? (int) std::round(getValue() * (n - 1)) : 0;
			return f.tableName(i);
		}
	};

	std::unique_ptr<Engine> engine{new Engine};
	std::unique_ptr<TableBank> tables{new TableBank};
	Params p;
	Channel ch[MAXCH];
	int ctl = 0;
	int ppqn = 4;
	dsp::SchmittTrigger clockTrig, resetTrig, mutateTrig, freezeTrig, mutateBtn, undoBtn, initBtn;
	float sinceClock = 0.f, bpm = 120.f;
	bool clockSeen = false;
	// buttons and triggers -> the UI thread (MUTATE, UNDO, INIT need the history and the presets)
	std::atomic<uint32_t> mutateReq{0}, undoReq{0}, initReq{0};
	// mutants: on while the table index stays where it was when MUTATE came
	std::atomic<bool> mutantOn[2];
	int mutantIdx[2] = {-1, -1}, mutantFold[2] = {-1, -1};
	int idxNow[2] = {0, 0};
	float flash = 0.f;
	// a folder / table to restore by name after the folders are known (patch from another computer)
	std::string wantFolderName[2], wantTableName[2];
	// jumps (a preset, the filter type, a table) happen while the output is muted for a moment (no clicks)
	neon::SwitchFade sfade;
	float appliedGain = -1.f;
	int lastPreset = -2, lastFtype = -1;
	float lastBankGain = -1.f;
	neon::Table* pendingTab[2] = {nullptr, nullptr};

	genra_presets::Bank bank;
	void initPresets() {
		genra_presets::Factory f;
		f.count = NUM_PRESETS;
		f.style = [](int i) { return PRESETS[i].style; };
		f.name = [](int i) { return PRESETS[i].name; };
		f.info = [](int i) { return BUILTINS[PRESETS[i].tA].name; };
		f.values = [](int i, float* v) { PRESETS[i].values(v); };
		f.gain = [](int i) { return PRESETS[i].gain; };
		bank.sel = PRESETSEL_PARAM;
		bank.init(this, f, asset::user("GenraNeon/WellePresets"));
	}

	Welle() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		mutantOn[0].store(false);
		mutantOn[1].store(false);
		genra_presel::config(this, PRESETSEL_PARAM);
		srcName = "WELLE";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(ACC_INPUT, follow::ACC);
		fol.bind(SLIDE_INPUT, follow::SLIDE);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		std::vector<std::string> warps, filters, modes;
		for (int i = 0; i < NUM_WARPS; i++)
			warps.push_back(WARP_NAMES[i]);
		for (int i = 0; i < gmono::NUM_FILTERS; i++)
			filters.push_back(gmono::FILTER_NAMES[i]);
		for (int i = 0; i < NUM_MODES; i++)
			modes.push_back(MODE_NAMES[i]);
		const char* AB[2] = {"A", "B"};
		for (int o = 0; o < 2; o++) {
			int b = o * 6;
			configParam<FolderQuantity>(FOLDA_PARAM + b, 0.f, (float) (neon::NUM_BUILTIN_FOLDERS - 1), 0.f, string::f("Osc %s folder", AB[o]));
			paramQuantities[FOLDA_PARAM + b]->snapEnabled = true;
			TableQuantity* tq = configParam<TableQuantity>(TBLA_PARAM + b, 0.f, 1.f, 0.f, string::f("Osc %s table", AB[o]));
			tq->osc = o;
			paramQuantities[TBLA_PARAM + b]->description = "Turns through the tables of the folder (stops at its end)";
			configParam(POSA_PARAM + b, 0.f, 1.f, 0.3f, string::f("Osc %s position", AB[o]), "%", 0.f, 100.f);
			configSwitch(WARPMA_PARAM + b, 0.f, NUM_WARPS - 1, 0.f, string::f("Osc %s warp type", AB[o]), warps);
			configParam(WARPA_PARAM + b, 0.f, 1.f, 0.f, string::f("Osc %s warp", AB[o]), "%", 0.f, 100.f);
		}
		configParam(OCTA_PARAM, -3.f, 3.f, 0.f, "Osc A octave");
		paramQuantities[OCTA_PARAM]->snapEnabled = true;
		configParam(TUNEB_PARAM, -24.f, 24.f, 0.f, "Osc B tune", " semitones");
		configSwitch(FTYPE_PARAM, 0.f, gmono::NUM_FILTERS - 1, 0.f, "Filter type", filters);
		configParam<CutoffQuantity>(CUTOFF_PARAM, 0.f, 1.f, 0.7f, "Cutoff");
		configParam(RES_PARAM, 0.f, 1.f, 0.15f, "Resonance", "%", 0.f, 100.f);
		configParam(FENV_PARAM, -1.f, 1.f, 0.3f, "Filter envelope amount", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(KEY_PARAM, 0.f, 1.f, 0.5f, "Key tracking", "%", 0.f, 100.f);
		configParam<TimeQuantity>(FATK_PARAM, 0.f, 1.f, 0.f, "Filter envelope attack");
		configParam<TimeQuantity>(FDEC_PARAM, 0.f, 1.f, 0.4f, "Filter envelope decay");
		configParam(SCAN_PARAM, 0.f, 1.f, 0.f, "Scan (how far MOTION moves the position)", "%", 0.f, 100.f);
		configParam<RateQuantity>(RATE_PARAM, 0.f, neon::NUM_RATES - 1, 6.f, "Motion rate (in time with CLOCK / GENRA)");
		paramQuantities[RATE_PARAM]->snapEnabled = true;
		configParam(PENV_PARAM, -1.f, 1.f, 0.f, "Position envelope (the filter envelope moves the position)", "%", 0.f, 100.f);
		configParam(CHORUS_PARAM, 0.f, 1.f, 0.f, "Chorus", "%", 0.f, 100.f);
		configParam(DELAY_PARAM, 0.f, 1.f, 0.f, "Delay (ping-pong, in time)", "%", 0.f, 100.f);
		configParam(REVERB_PARAM, 0.f, 1.f, 0.2f, "Reverb", "%", 0.f, 100.f);
		configParam<TimeQuantity>(ATK_PARAM, 0.f, 1.f, 0.f, "Attack");
		configParam<TimeQuantity>(DEC_PARAM, 0.f, 1.f, 0.5f, "Decay");
		configParam(SUS_PARAM, 0.f, 1.f, 0.8f, "Sustain", "%", 0.f, 100.f);
		configParam<TimeQuantity>(REL_PARAM, 0.f, 1.f, 0.35f, "Release");
		configParam(MIX_PARAM, 0.f, 1.f, 0.f, "A / B mix", "%", 0.f, 100.f);
		configParam(SUB_PARAM, 0.f, 1.f, 0.f, "Sub (sine an octave below A)", "%", 0.f, 100.f);
		configParam(UNISON_PARAM, 1.f, (float) MAXU, 1.f, "Unison copies");
		paramQuantities[UNISON_PARAM]->snapEnabled = true;
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.3f, "Unison spread (detune and stereo)", "%", 0.f, 100.f);
		configSwitch(VMODE_PARAM, 0.f, NUM_MODES - 1, MODE_POLY, "Voice mode", modes);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.2f, "Glide time");
		paramQuantities[GLIDE_PARAM]->description = "On GENRA's SLIDE; with SLIDE unplugged in Mono/Legato on overlapping notes";
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent amount", "%", 0.f, 100.f);
		configButton(MUTATE_PARAM, "Mutate: new tables for A and B from the building blocks");
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze: holds the frame (MOTION and the envelope stop moving it)", {"Off", "On"});
		configButton(UNDO_PARAM, "Undo the last MUTATE / INIT");
		configButton(INIT_PARAM, "Init: back to a plain sound (BASIC tables, no mutant)");
		for (int id : {MUTATE_PARAM, UNDO_PARAM, INIT_PARAM, FREEZE_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		configParam(LEVEL_PARAM, 0.f, 1.4125f, 1.f, "Level", " dB", -10.f, 20.f);
		paramQuantities[LEVEL_PARAM]->randomizeEnabled = false;

		const char* inNames[INPUTS_LEN] = {
			"V/Oct (polyphonic)", "Gate (polyphonic)", "Velocity (0–10 V)", "Accent (gate, from GENRA's ACC)",
			"Slide (from GENRA's SLIDE: glide into the next note)", "Clock (MOTION and delay in time; GENRA: 16ths)",
			"Reset (MOTION from the start)", "Mutate trigger", "Freeze trigger (toggles)", "Position A CV", "Position B CV",
			"Warp A CV", "Warp B CV", "Table A CV (0–10 V through the folder)", "Table B CV (0–10 V through the folder)",
			"A / B mix CV", "Sub CV", "FM (audio: phase modulation of both oscillators)", "Unison CV (0–10 V: 1–7)",
			"Spread CV", "Cutoff CV (1 V/oct)", "Resonance CV", "Filter envelope CV", "Drive CV", "Scan CV",
			"Motion rate CV", "Attack CV", "Decay CV", "Release CV", "Glide CV", "Delay CV", "Reverb CV"};
		for (int i = 0; i < INPUTS_LEN; i++)
			configInput(i, inNames[i]);
		configOutput(ENV_OUTPUT, "Envelope of the newest note (0–10 V)");
		configOutput(MOTION_OUTPUT, "Motion (±5 V, in time)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		initPresets();
		syncFolderRange();
	}

	/** The FOLDER params run over the folders there are (UI thread, after a scan). */
	void syncFolderRange() {
		int n = std::max(1, tables->folderCount.load());
		for (int id : {FOLDA_PARAM, FOLDB_PARAM})
			paramQuantities[id]->maxValue = (float) (n - 1);
	}

	void onAdd(const AddEvent& e) override { engine->setSampleRate(APP->engine->getSampleRate()); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { engine->setSampleRate(e.sampleRate); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine->allOff();
		bank.reset();
		for (int o = 0; o < 2; o++) {
			mutantOn[o].store(false);
			tables->clearMutant(o);
		}
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	float kv(int param, int input, float lo = 0.f, float hi = 1.f) {
		return clamp(params[param].getValue() + inputs[input].getVoltage() * 0.1f * (hi - lo), lo, hi);
	}
	/** The table index of oscillator o: TABLE (+ CV) over the folder. */
	int tableIndex(int o) {
		int n = tables->slot[o].count.load();
		if (n <= 1)
			return 0;
		float v = clamp(params[o ? TBLB_PARAM : TBLA_PARAM].getValue() + inputs[o ? TBLB_INPUT : TBLA_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		return clamp((int) std::round(v * (n - 1)), 0, n - 1);
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		// tempo from the clock
		sinceClock += args.sampleTime;
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			clockSeen = false;
			sinceClock = 0.f;
			engine->resetMotion();
		}
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clockSeen && sinceClock > 0.005f && sinceClock < 4.f)
				bpm = clamp(60.f / (sinceClock * ppqn), 20.f, 400.f);
			clockSeen = true;
			sinceClock = 0.f;
		}
		if (!inputs[CLOCK_INPUT].isConnected())
			bpm = 120.f, clockSeen = false;
		// buttons and triggers
		if (mutateTrig.process(inputs[TMUTATE_INPUT].getVoltage(), 0.1f, 1.f) | mutateBtn.process(params[MUTATE_PARAM].getValue()))
			mutateReq++;
		if (undoBtn.process(params[UNDO_PARAM].getValue()))
			undoReq++;
		if (initBtn.process(params[INIT_PARAM].getValue()))
			initReq++;
		if (freezeTrig.process(inputs[TFREEZE_INPUT].getVoltage(), 0.1f, 1.f))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > 0.5f ? 0.f : 1.f);

		if (--ctl <= 0) {
			ctl = 16;
			for (int o = 0; o < 2; o++) {
				int f = (int) std::round(params[o ? FOLDB_PARAM : FOLDA_PARAM].getValue());
				tables->wantFolder[o].store(f);
				int idx = tableIndex(o);
				if (mutantOn[o].load() && (idx != mutantIdx[o] || f != mutantFold[o]))
					mutantOn[o].store(false);  // the table was turned: the mutant goes
				idxNow[o] = idx;
				neon::Table* t = tables->pick(o, idx, mutantOn[o].load());
				if (!engine->tab[o])
					engine->tab[o] = t;
				else if (t == engine->tab[o])
					pendingTab[o] = nullptr;  // turned back before the dip
				else if (t != engine->tab[o] && t != pendingTab[o]) {
					pendingTab[o] = t;  // the new table comes in the silence of a short dip
					sfade.trigger(false);
				}
			}
			// a new preset (or its level) or another filter type: mute, start clean, then the new sound
			int ft = (int) std::round(params[FTYPE_PARAM].getValue());
			if (bank.preset != lastPreset || bank.gain != lastBankGain || (lastFtype >= 0 && ft != lastFtype)) {
				if (appliedGain < 0.f)
					appliedGain = bank.gain;  // the first time: no fade
				else
					sfade.trigger(true);
				lastPreset = bank.preset;
				lastBankGain = bank.gain;
			}
			lastFtype = ft;
		}
		if (ctl == 16 && sfade.state != 1) {  // while muting for a jump the old sound keeps its knobs
			p.posA = kv(POSA_PARAM, POSA_INPUT);
			p.posB = kv(POSB_PARAM, POSB_INPUT);
			p.warpModeA = (int) std::round(params[WARPMA_PARAM].getValue());
			p.warpModeB = (int) std::round(params[WARPMB_PARAM].getValue());
			p.warpA = kv(WARPA_PARAM, WARPA_INPUT);
			p.warpB = kv(WARPB_PARAM, WARPB_INPUT);
			p.octA = params[OCTA_PARAM].getValue();
			p.tuneB = params[TUNEB_PARAM].getValue();
			p.ftype = clamp((int) std::round(params[FTYPE_PARAM].getValue()), 0, gmono::NUM_FILTERS - 1);
			p.cutoff = params[CUTOFF_PARAM].getValue();
			p.cutCv = inputs[CUT_INPUT].getVoltage();
			p.res = kv(RES_PARAM, RES_INPUT);
			p.fenv = kv(FENV_PARAM, FENV_INPUT, -1.f, 1.f);
			p.drive = kv(DRIVE_PARAM, DRIVE_INPUT);
			p.key = params[KEY_PARAM].getValue();
			p.fatk = params[FATK_PARAM].getValue();
			p.fdec = params[FDEC_PARAM].getValue();
			p.scan = kv(SCAN_PARAM, SCAN_INPUT);
			p.rate = clamp((int) std::round(params[RATE_PARAM].getValue() + inputs[RATE_INPUT].getVoltage() * 1.1f), 0,
			               neon::NUM_RATES - 1);
			p.penv = params[PENV_PARAM].getValue();
			p.chorus = params[CHORUS_PARAM].getValue();
			p.delay = kv(DELAY_PARAM, DELAY_INPUT);
			p.verb = kv(REVERB_PARAM, VERB_INPUT);
			p.atk = kv(ATK_PARAM, ATK_INPUT);
			p.dec = kv(DEC_PARAM, DEC_INPUT);
			p.sus = params[SUS_PARAM].getValue();
			p.rel = kv(REL_PARAM, REL_INPUT);
			p.mix = kv(MIX_PARAM, MIX_INPUT);
			p.sub = kv(SUB_PARAM, SUB_INPUT);
			p.unison = clamp((int) std::round(params[UNISON_PARAM].getValue() + inputs[UNISON_INPUT].getVoltage() * 0.7f), 1, MAXU);
			p.spread = kv(SPREAD_PARAM, SPREAD_INPUT);
			p.mode = clamp((int) std::round(params[VMODE_PARAM].getValue()), 0, NUM_MODES - 1);
			p.glide = kv(GLIDE_PARAM, GLIDE_INPUT);
			p.accent = params[ACCENT_PARAM].getValue();
			p.outGain = appliedGain < 0.f ? bank.gain : appliedGain;
			p.freeze = params[FREEZE_PARAM].getValue() > 0.5f;
		}

		int n = std::min(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), MAXCH);
		bool vel = inputs[VEL_INPUT].isConnected();
		for (int c = 0; c < n; c++) {
			Channel& x = ch[c];
			x.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			float g = inputs[GATE_INPUT].getPolyVoltage(c);
			x.gate = g >= 1.f ? true : (g <= 0.1f ? false : x.gate);
			x.vel = vel ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 0.8f;
			x.acc = inputs[ACC_INPUT].getPolyVoltage(c) >= 1.f;
			x.slide = inputs[SLIDE_INPUT].getPolyVoltage(c) >= 1.f;
		}
		bool bottom;
		float fg = sfade.step(args.sampleRate, bottom);
		if (bottom) {
			if (sfade.wantReset)
				engine->softReset();
			sfade.wantReset = false;
			appliedGain = p.outGain = bank.gain;
			ctl = 1;  // the new knobs at once
			for (int o = 0; o < 2; o++)
				if (pendingTab[o]) {
					engine->tab[o] = pendingTab[o];
					pendingTab[o] = nullptr;
				}
		}
		float l, r, env, mot;
		engine->process(p, ch, n, inputs[SLIDE_INPUT].isConnected(), bpm, inputs[FM_INPUT].getVoltage(), l, r, env, mot);
		float lvl = fg * params[LEVEL_PARAM].getValue();
		l *= lvl, r *= lvl;
		outputs[L_OUTPUT].setVoltage(outputs[R_OUTPUT].isConnected() || pulled(args.frame) ? l : 0.5f * (l + r));
		outputs[R_OUTPUT].setVoltage(r);
		outputs[ENV_OUTPUT].setVoltage(10.f * env);
		outputs[MOTION_OUTPUT].setVoltage(5.f * mot);
	}

	// ---------------------------------------------------------------- UI thread: MUTATE, UNDO, INIT
	std::vector<json_t*> undoStack;
	void pushUndo() {
		undoStack.push_back(toJson());
		if (undoStack.size() > 16) {
			json_decref(undoStack.front());
			undoStack.erase(undoStack.begin());
		}
	}
	~Welle() {
		for (json_t* j : undoStack)
			json_decref(j);
	}
	void doMutate() {
		pushUndo();
		for (int o = 0; o < 2; o++) {
			uint32_t seed = 1u + (random::u32() % 99999u);
			tables->slot[o].mutantReq.store(seed);
			mutantIdx[o] = idxNow[o];
			mutantFold[o] = (int) std::round(params[o ? FOLDB_PARAM : FOLDA_PARAM].getValue());
			mutantOn[o].store(true);
		}
		tables->wake();
		flash = 0.4f;
	}
	void doInit() {
		pushUndo();
		for (int i = 0; i < NUM_PARAMS; i++)
			params[i].setValue(paramQuantities[i]->getDefaultValue());
		params[FREEZE_PARAM].setValue(0.f);
		for (int o = 0; o < 2; o++) {
			mutantOn[o].store(false);
			tables->clearMutant(o);
		}
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
	/** UI thread, every frame (from the panel). */
	void uiStep(float dt) {
		uint32_t m = mutateReq.load(), u = undoReq.load(), in = initReq.load();
		if (m != doneM) {
			doneM = m;
			doMutate();
		}
		if (u != doneU) {
			doneU = u;
			doUndo();
		}
		if (in != doneI) {
			doneI = in;
			doInit();
		}
		if (flash > 0.f)
			flash -= dt;
		syncFolderRange();
		// a patch from another computer: find its folders by name once they are scanned
		for (int o = 0; o < 2; o++)
			if (!wantFolderName[o].empty()) {
				int f = tables->findFolder(wantFolderName[o]);
				if (f >= 0) {
					params[o ? FOLDB_PARAM : FOLDA_PARAM].setValue((float) f);
					FolderInfo fi = tables->folderInfo(f);
					for (int i = 0; i < fi.count(); i++)
						if (fi.tableName(i) == wantTableName[o] && fi.count() > 1)
							params[o ? TBLB_PARAM : TBLA_PARAM].setValue((float) i / (fi.count() - 1));
				}
				wantFolderName[o].clear();
			}
	}
	uint32_t doneM = 0, doneU = 0, doneI = 0;

	/** UI thread: the user picked folder f, table i for oscillator o. */
	void choose(int o, int f, int i) {
		FolderInfo fi = tables->folderInfo(f);
		json_t* oldJ = toJson();
		params[o ? FOLDB_PARAM : FOLDA_PARAM].setValue((float) f);
		params[o ? TBLB_PARAM : TBLA_PARAM].setValue(fi.count() > 1 ? (float) i / (fi.count() - 1) : 0.f);
		mutantOn[o].store(false);
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "choose wavetable";
		h->moduleId = id;
		h->oldModuleJ = oldJ;
		h->newModuleJ = toJson();
		APP->history->push(h);
	}

	// presets the followed GENRA may switch
	int hostPresetCount() override { return bank.count(); }
	void hostPresetInfo(int i, char* name, int nl, char* style, int sl) override {
		follow::putStr(name, nl, bank.nameOf(i).c_str());
		follow::putStr(style, sl, i >= 0 && i < bank.f.count && bank.f.style ? bank.f.style(i) : "");
	}
	int hostPresetCurrent() override { return bank.preset; }
	void hostPresetLoad(int i) override {
		bank.load(i);
		for (int o = 0; o < 2; o++)
			mutantOn[o].store(false);
	}

	json_t* dataToJson() override {
		json_t* j = json_object();
		json_object_set_new(j, "ppqn", json_integer(ppqn));
		json_object_set_new(j, "delayDiv", json_integer(p.ddiv));
		bank.toJson(j);
		fol.toJson(j);
		json_t* a = json_array();
		for (int o = 0; o < 2; o++) {
			json_t* x = json_object();
			int f = (int) std::round(params[o ? FOLDB_PARAM : FOLDA_PARAM].getValue());
			FolderInfo fi = tables->folderInfo(f);
			json_object_set_new(x, "folder", json_string(fi.name.c_str()));
			json_object_set_new(x, "table", json_string(fi.tableName(idxNow[o]).c_str()));
			json_object_set_new(x, "mutant", json_integer(mutantOn[o].load() ? (json_int_t) tables->slot[o].mutantSeed : 0));
			json_array_append_new(a, x);
		}
		json_object_set_new(j, "osc", a);
		return j;
	}
	void dataFromJson(json_t* j) override {
		bank.selCode++;
		fol.fromJson(j);
		if (json_t* v = json_object_get(j, "ppqn"))
			ppqn = std::max(1, (int) json_integer_value(v));
		if (json_t* v = json_object_get(j, "delayDiv"))
			p.ddiv = clamp((int) json_integer_value(v), 0, NUM_DDIVS - 1);
		bank.fromJson(j);
		json_t* a = json_object_get(j, "osc");
		for (int o = 0; o < 2 && json_is_array(a) && o < (int) json_array_size(a); o++) {
			json_t* x = json_array_get(a, o);
			json_t* fj = json_object_get(x, "folder");
			json_t* tj = json_object_get(x, "table");
			std::string fname = json_is_string(fj) ? json_string_value(fj) : "";
			int f = (int) std::round(params[o ? FOLDB_PARAM : FOLDA_PARAM].getValue());
			if (!fname.empty() && tables->folderInfo(f).name != fname) {
				wantFolderName[o] = fname;
				wantTableName[o] = json_is_string(tj) ? json_string_value(tj) : "";
			}
			uint32_t seed = (uint32_t) json_integer_value(json_object_get(x, "mutant"));
			if (seed) {
				tables->slot[o].mutantReq.store(seed);
				mutantIdx[o] = tableIndex(o);
				mutantFold[o] = f;
				mutantOn[o].store(true);
			} else {
				mutantOn[o].store(false);
				tables->clearMutant(o);
			}
		}
		tables->wake();
	}
};

// ---------------------------------------------------------------- widgets
/** The FOLDER/TABLE field: shows both; a click opens the folders (sub menus of tables), LOAD WAV and the root. The param
 * under it is the FOLDER (MIDI-MAP turns through the folders); TABLE is the knob below. */
struct TableField : app::ParamWidget {
	Welle* w = nullptr;
	int osc = 0;
	void setup(const float* r) {
		box.pos = mm2px(Vec(r[0], r[1]));
		box.size = mm2px(Vec(r[2], r[3]));
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && w) {
			ParamWidget::onButton(e);
			Welle* m = w;
			int o = osc;
			Menu* menu = createMenu();
			menu->addChild(createMenuLabel(o ? "Osc B wavetable" : "Osc A wavetable"));
			std::vector<FolderInfo> fl = m->tables->folderList();
			int curF = (int) std::round(m->params[o ? Welle::FOLDB_PARAM : Welle::FOLDA_PARAM].getValue());
			for (size_t f = 0; f < fl.size(); f++) {
				FolderInfo fi = fl[f];
				if (f == (size_t) neon::NUM_BUILTIN_FOLDERS)
					menu->addChild(new MenuSeparator);
				menu->addChild(createSubmenuItem(fi.name, (int) f == curF ? "✔" : string::f("%d", fi.count()), [=](Menu* sub) {
					for (int i = 0; i < fi.count(); i++)
						sub->addChild(createCheckMenuItem(fi.tableName(i), fi.builtin >= 0 ? neon::BUILTINS[fi.ids[i]].info : "",
						                                  [=]() { return (int) f == curF && m->idxNow[o] == i; },
						                                  [=]() { m->choose(o, (int) f, i); }));
				}));
			}
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuItem("LOAD WAV …", "", [=]() { loadWav(m, o); }));
			menu->addChild(createMenuItem("SET ROOT FOLDER …", rootDir().empty() ? "none" : system::getFilename(rootDir()),
			                              [=]() { chooseRoot(m); }));
			menu->addChild(createMenuItem("Rescan folders", "", [=]() {
				m->tables->rescan.store(true);
				m->tables->wake();
			}));
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (w && e.scrollDelta.y != 0.f) {  // the wheel: the next / previous table of the folder
			int id = osc ? Welle::TBLB_PARAM : Welle::TBLA_PARAM;
			int n = w->tables->slot[osc].count.load();
			if (n > 1) {
				int i = clamp(w->idxNow[osc] + (e.scrollDelta.y < 0.f ? 1 : -1), 0, n - 1);
				w->params[id].setValue((float) i / (n - 1));
				w->mutantOn[osc].store(false);
			}
			e.consume(this);
			return;
		}
		ParamWidget::onHoverScroll(e);
	}
	static bool chooseRoot(Welle* m) {
		std::string start = isDirSafe(rootDir()) ? rootDir() : "";
		char* p = osdialog_file(OSDIALOG_OPEN_DIR, start.empty() ? nullptr : start.c_str(), nullptr, nullptr);
		if (!p)
			return false;
		std::string r = p;
		std::free(p);
		while (r.size() > 3 && (r.back() == '\\' || r.back() == '/'))
			r.pop_back();
		rootDir() = r;
		saveSettings();
		m->tables->rescan.store(true);
		m->tables->wake();
		return true;
	}
	/** A WAV from anywhere: its folder becomes the root's when it is below it; else the folder is set as root. */
	static void loadWav(Welle* m, int o) {
		if (!isDirSafe(rootDir()) && !chooseRoot(m))
			return;
		osdialog_filters* f = osdialog_filters_parse("WAV:wav,WAV");
		char* p = osdialog_file(OSDIALOG_OPEN, rootDir().c_str(), nullptr, f);
		osdialog_filters_free(f);
		if (!p)
			return;
		std::string path = p;
		std::free(p);
		std::string dir = system::getDirectory(path);
		if (dir.compare(0, rootDir().size(), rootDir()) != 0) {  // outside the root: its folder becomes the root
			rootDir() = dir;
			saveSettings();
		}
		std::vector<FolderInfo> fl = scanFolders(rootDir());
		{
			std::lock_guard<std::mutex> lk(m->tables->m);
			m->tables->folders = fl;
			m->tables->folderCount.store((int) fl.size());
			for (auto& s : m->tables->slot)
				s.folder = -1;
		}
		m->syncFolderRange();
		for (size_t k = 0; k < fl.size(); k++)
			if (fl[k].builtin < 0 && fl[k].path == dir)
				for (int i = 0; i < fl[k].count(); i++)
					if (fl[k].files[i] == path)
						m->choose(o, (int) k, i);
		m->tables->wake();
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			std::string s = osc ? "VOCAL/TALK" : "TECHNO/ACID";
			if (w) {
				int f = (int) std::round(w->params[osc ? Welle::FOLDB_PARAM : Welle::FOLDA_PARAM].getValue());
				FolderInfo fi = w->tables->folderInfo(f);
				std::string folder = fi.name;
				size_t sl = folder.find_last_of('/');
				if (sl != std::string::npos)
					folder = folder.substr(sl + 1);
				s = w->mutantOn[osc].load() ? "MUTANT" : folder + "/" + fi.tableName(w->idxNow[osc]);
			}
			float maxW = box.size.x - mm2px(5.f);
			std::shared_ptr<window::Font> f = font();
			if (f && f->handle >= 0) {
				nvgFontFaceId(args.vg, f->handle);
				nvgFontSize(args.vg, 7.0f);
				nvgTextLetterSpacing(args.vg, 0.8f);
				float bounds[4];
				while (s.size() > 4 && nvgTextBounds(args.vg, 0, 0, s.c_str(), nullptr, bounds) > maxW)
					s = s.substr(0, s.size() - 4) + "..";
			}
			text(args.vg, mm2px(1.6f), box.size.y / 2 + 2.4f, s, 7.0f, WHITE, NVG_ALIGN_LEFT, 0.8f);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** The display: the table of A as a neon mountain range (frames from far to near), the frame that plays its glowing
 * ridge; the folder, table, position and note in the corners. */
struct Display : Widget {
	Welle* module = nullptr;
	float pts[32][96];
	int nFrames = 0;
	std::string tname;
	float t = 0.f;
	void step() override {
		Widget::step();
		t += 1.f / 60.f;
		if (module)
			module->uiStep(1.f / 60.f);
		// the mountain range: 22 frames of A's table (from the far = 0 to the near = 1)
		nFrames = 0;
		for (int i = 0; i < 22; i++) {
			float f = i / 21.f;
			if (module) {
				if (!module->tables->shape(0, f, 96, pts[i], nullptr, i == 0 ? &tname : nullptr))
					break;
			} else
				for (int k = 0; k < 96; k++) {
					float ph = k / 96.f;
					pts[i][k] = std::sin(2 * M_PI * ph) * (1 - f) + f * (0.6f * std::sin(2 * M_PI * ph * (1 + 5 * f)) +
					                                                    0.4f * (std::fmod(ph * (2 + 6 * f), 1.f) < 0.5f ? 1 : -1));
			}
			nFrames = i + 1;
		}
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		nvgSave(vg);
		nvgScissor(vg, 2, 2, W - 4, H - 4);
		float hz = H * 0.09f, vx = W / 2;
		// floor grid
		nvgBeginPath(vg);
		for (int i = -15; i <= 15; i++) {
			nvgMoveTo(vg, vx + i * mm2px(2.f), hz);
			nvgLineTo(vg, vx + i * mm2px(24.f), H);
		}
		nvgStrokeColor(vg, alpha(VIOLET, 0.35f));
		nvgStrokeWidth(vg, 0.6f);
		nvgStroke(vg);
		float pos = module ? module->engine->showPos[0] : 0.6f;
		int cur = (int) std::round(pos * (nFrames - 1));
		for (int i = 0; i < nFrames; i++) {
			float f = i / std::max(1.f, (float) (nFrames - 1));
			float z = 0.25f + 0.75f * std::pow(f, 1.3f);
			float y0 = hz + (H - hz) * std::pow(f, 1.3f);
			float half = W * (0.15f + 0.34f * z), amp = mm2px(2.f + 9.f * z);
			nvgBeginPath(vg);
			nvgMoveTo(vg, vx - half, y0);
			for (int k = 0; k < 96; k++)
				nvgLineTo(vg, vx - half + 2 * half * k / 95.f, y0 - std::fabs(pts[i][k]) * amp);
			nvgLineTo(vg, vx + half, y0);
			nvgClosePath(vg);
			nvgFillColor(vg, nvgRGBA(7, 3, 15, 235));
			nvgFill(vg);
			nvgBeginPath(vg);
			for (int k = 0; k < 96; k++) {
				float x = vx - half + 2 * half * k / 95.f, y = y0 - std::fabs(pts[i][k]) * amp;
				if (k == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			if (i == cur) {
				glowStroke(vg, WHITE, 1.6f, 4.f);
			} else {
				NVGpaint pt = nvgLinearGradient(vg, vx - half, 0, vx + half, 0, alpha(PINK, 0.35f + 0.55f * f),
				                                alpha(CYAN, 0.35f + 0.55f * f));
				nvgStrokePaint(vg, pt);
				nvgStrokeWidth(vg, 0.4f + 0.8f * f);
				nvgStroke(vg);
			}
		}
		scanlines(vg, W, H);
		nvgResetScissor(vg);
		// corners
		float pad = mm2px(1.5f);
		plaque(vg, pad, pad, mm2px(44.f), mm2px(9.4f), false);
		plaque(vg, W - pad - mm2px(46.f), pad, mm2px(46.f), mm2px(10.6f), true);
		std::string l1 = "BASIC/BASIC", l2 = "POS 37 · FR 24/64 · BEND 18 %", note = "C4", fol = "FREE";
		if (module) {
			int f = (int) std::round(module->params[Welle::FOLDA_PARAM].getValue());
			FolderInfo fi = module->tables->folderInfo(f);
			int n = fi.count(), i = module->idxNow[0];
			l1 = module->mutantOn[0].load() ? tname : string::f("%s / %s · %d OF %d", fi.name.c_str(), fi.tableName(i).c_str(), i + 1, n);
			int frames = 64;
			int fr = (int) std::round(pos * (frames - 1)) + 1;
			const Params& p = module->p;
			l2 = string::f("POS %d · FR %d/%d · %s %d %%", (int) std::round(pos * 100.f), fr, frames,
			               WARP_NAMES[clamp(p.warpModeA, 0, NUM_WARPS - 1)], (int) std::round(p.warpA * 100.f));
			char nn[8];
			neon::noteName(module->engine->lastNote, nn);
			note = nn;
			if (module->fol.active()) {
				int tr = module->fol.track.load();
				fol = std::string("FOLLOW · ") + (tr >= 0 && tr < follow::NUM_TRACKS ? follow::TRACK_NAMES[tr] : "");
			} else
				fol = module->inputs[Welle::CLOCK_INPUT].isConnected() ? string::f("%.0f BPM", module->bpm) : "FREE · 120 BPM";
			if (p.freeze)
				fol += " · FREEZE";
		}
		text(vg, pad + mm2px(2.f), pad + mm2px(4.f), string::uppercase(l1), 6.6f, CYAN, NVG_ALIGN_LEFT, 1.2f);
		text(vg, pad + mm2px(2.f), pad + mm2px(7.4f), l2, 5.8f, DIM, NVG_ALIGN_LEFT, 1.0f);
		text(vg, W - pad - mm2px(2.f), pad + mm2px(5.8f), note, 16.f, WHITE, NVG_ALIGN_RIGHT, 1.2f);
		text(vg, W - pad - mm2px(2.f), pad + mm2px(9.2f), string::uppercase(fol), 5.4f, DIM, NVG_ALIGN_RIGHT, 1.0f);
		nvgRestore(vg);
		Widget::drawLayer(args, layer);
	}
};

struct WelleWidget : ModuleWidget {
	WelleWidget(Welle* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Welle.svg")));
		follow::knownHost(module);
		using M = Welle;
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

		for (int o = 0; o < 2; o++) {
			TableField* tf = createParam<TableField>(Vec(0, 0), module, o ? M::FOLDB_PARAM : M::FOLDA_PARAM);
			tf->setup(o ? L::TABLEFB : L::TABLEFA);
			tf->w = module;
			tf->osc = o;
			addParam(tf);
		}
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
		field(L::WARPFA, M::WARPMA_PARAM, NUM_WARPS, WARP_NAMES, WARP_INFO, "Osc A warp");
		field(L::WARPFB, M::WARPMB_PARAM, NUM_WARPS, WARP_NAMES, WARP_INFO, "Osc B warp");
		field(L::FTYPEF, M::FTYPE_PARAM, gmono::NUM_FILTERS, gmono::FILTER_NAMES, gmono::FILTER_INFO, "Filter type");

		auto knob = [&](const float* p, int id) {
			NKnob* k = createParam<NKnob>(Vec(0, 0), module, id);
			k->setup(p);
			addParam(k);
		};
		knob(L::TBLA, M::TBLA_PARAM);
		knob(L::POSA, M::POSA_PARAM);
		knob(L::WARPA, M::WARPA_PARAM);
		knob(L::OCTA, M::OCTA_PARAM);
		knob(L::TBLB, M::TBLB_PARAM);
		knob(L::POSB, M::POSB_PARAM);
		knob(L::WARPB, M::WARPB_PARAM);
		knob(L::TUNEB, M::TUNEB_PARAM);
		knob(L::CUTOFF, M::CUTOFF_PARAM);
		knob(L::RES, M::RES_PARAM);
		knob(L::FENV, M::FENV_PARAM);
		knob(L::DRIVE, M::DRIVE_PARAM);
		knob(L::KEY, M::KEY_PARAM);
		knob(L::FATK, M::FATK_PARAM);
		knob(L::FDEC, M::FDEC_PARAM);
		knob(L::SCAN, M::SCAN_PARAM);
		knob(L::RATE, M::RATE_PARAM);
		knob(L::PENV, M::PENV_PARAM);
		knob(L::CHORUS, M::CHORUS_PARAM);
		knob(L::DELAY, M::DELAY_PARAM);
		knob(L::REVERB, M::REVERB_PARAM);
		knob(L::ATK, M::ATK_PARAM);
		knob(L::DEC, M::DEC_PARAM);
		knob(L::SUS, M::SUS_PARAM);
		knob(L::REL, M::REL_PARAM);
		knob(L::MIX, M::MIX_PARAM);
		knob(L::SUB, M::SUB_PARAM);
		knob(L::UNISON, M::UNISON_PARAM);
		knob(L::SPREAD, M::SPREAD_PARAM);
		knob(L::GLIDE, M::GLIDE_PARAM);
		knob(L::ACCENT, M::ACCENT_PARAM);
		knob(L::LEVEL, M::LEVEL_PARAM);
		NLamps* lr = createParam<NLamps>(Vec(0, 0), module, M::VMODE_PARAM);
		lr->place(L::VMODE, NUM_MODES);
		lr->accent = CYAN;
		addParam(lr);

		auto button = [&](const float* r, int id, bool latch, NVGcolor col) {
			NButton* b = createParam<NButton>(Vec(0, 0), module, id);
			b->setup(r, latch);
			b->col = col;
			addParam(b);
			return b;
		};
		NButton* mb = button(L::MUTATE, M::MUTATE_PARAM, false, PINK);
		mb->lit = [=]() { return module && module->flash > 0.f; };
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
		Welle* m = dynamic_cast<Welle*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		genra_presets::appendMenu(menu, &m->bank);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Set wavetable root folder …", rootDir().empty() ? "none" : rootDir(),
		                              [=]() { TableField::chooseRoot(m); }));
		menu->addChild(createIndexSubmenuItem(
		    "Delay time", {"1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted"}, [=]() { return (size_t) m->p.ddiv; },
		    [=](size_t i) { m->p.ddiv = (int) i; }));
		menu->addChild(createIndexSubmenuItem(
		    "CLOCK input", {"Quarters (1 PPQN)", "8ths (2 PPQN)", "16ths (4 PPQN, GENRA)", "24 PPQN"},
		    [=]() {
			    for (int i = 0; i < 4; i++)
				    if (PPQN_CHOICES[i] == m->ppqn)
					    return (size_t) i;
			    return (size_t) 2;
		    },
		    [=](size_t i) { m->ppqn = PPQN_CHOICES[i]; }));
		menu->addChild(createMenuLabel("Patch GENRA's melody (or GENRA TECHNO's stab): V/OCT, GATE, VEL, ACC, SLIDE, CLOCK"));
	}
};

}  // namespace welle

Model* modelWelle = createModel<welle::Welle, welle::WelleWidget>("Welle");
