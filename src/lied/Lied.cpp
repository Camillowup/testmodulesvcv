// SPDX-License-Identifier: GPL-3.0-only
// LIED: a text singer. Four lines of lyrics on the panel, spoken by eSpeak NG (the worker, LiedCore.hpp) and sung at
// the note of V/OCT, syllable by syllable on every gate (LiedDsp.hpp). The GENRA strip (V/OCT GATE VEL ACC SLIDE MOD,
// CLOCK RESET) and Follow GENRA as on every instrument of the family.
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "LiedCore.hpp"
#include "LiedDsp.hpp"
#include "LiedLayout.hpp"
#include <array>
#include <atomic>
#include <mutex>

namespace genra_lied {

#define LIED_RGB 0xff, 0x7f, 0x6e   // coral; keep in step with tools/lied_panel.py (ACC)
static const NVGcolor ACCENT = nvgRGB(LIED_RGB);
namespace L = llayout;

struct Lang {
	const char* name;
	const char* code;
};
static const Lang LANGS[] = {{"DEUTSCH", "de"},      {"ENGLISH", "en"},       {"ENGLISH US", "en-us"},
                             {"FRAN\xc3\x87" "AIS", "fr"}, {"ESPA\xc3\x91OL", "es"}, {"ITALIANO", "it"},
                             {"NEDERLANDS", "nl"},   {"PORTUGU\xc3\x8aS", "pt"}, {"SVENSKA", "sv"},
                             {"POLSKI", "pl"},       {"SUOMI", "fi"},         {"LATINA", "la"}};
static const int NLANG = 12;
struct VoiceDef {
	const char* name;
	const char* variant;
	int f0;  // the pitch eSpeak speaks at (near where it is sung: less to shift)
};
static const VoiceDef VOICES[] = {{"KLATT 1", "klatt", 130},  {"KLATT 2", "klatt2", 130}, {"KLATT 3", "klatt3", 130},
                                  {"KLATT 4", "klatt4", 130}, {"KLATT 5", "klatt5", 130}, {"KLATT 6", "klatt6", 130},
                                  {"MALE 1", "m1", 130},      {"MALE 3", "m3", 130},      {"MALE 6", "m6", 130},
                                  {"FEMALE 1", "f1", 210},    {"FEMALE 3", "f3", 210},    {"FEMALE 5", "f5", 210},
                                  {"CROAK", "croak", 120},    {"WHISPER", "whisper", 140}};
static const int NVOICE = 14;
static const char* STEP_NAMES[3] = {"SYLL", "WORD", "FREE"};
static const char* CLOCK_MODES[4] = {"16ths (4 PPQN)", "8ths (2 PPQN)", "Quarters (1 PPQN)", "24 PPQN"};
static const int CLOCK_PPQN[4] = {4, 2, 1, 24};
static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// the knobs a preset sets, in this order
enum Knob { K_FORMANT, K_BREATH, K_BRIGHT, K_ROUGH, K_CHOIR, K_SPEED, K_VOWEL, K_GLIDE, K_ACCENT, K_VDEPTH, K_VRATE,
            K_VDELAY, K_ECHO, K_FDBK, K_VERB, K_LOFI, NKNOB };
struct Preset {
	const char* name;
	int voice;
	float k[NKNOB];
};
// FORMANT BREATH BRIGHT ROUGH CHOIR | SPEED VOWEL GLIDE ACCENT | DEPTH RATE DELAY | ECHO FDBK VERB LO-FI
static const Preset PRESETS[] = {
    {"POP LEAD", 2, {.5f, .15f, .55f, 0.f, 0.f, .5f, 1.f, .15f, .5f, .35f, .5f, .35f, .2f, .35f, .3f, 0.f}},
    {"POP CHORUS", 10, {.5f, .2f, .55f, 0.f, .6f, .5f, 1.f, .12f, .5f, .3f, .5f, .3f, .15f, .3f, .4f, 0.f}},
    {"SOUL DIVA", 9, {.55f, .25f, .5f, .05f, 0.f, .5f, 1.f, .3f, .6f, .55f, .55f, .25f, .1f, .3f, .35f, 0.f}},
    {"BARITONE", 7, {.4f, .1f, .45f, .05f, 0.f, .5f, 1.f, .15f, .5f, .3f, .45f, .4f, .1f, .3f, .3f, 0.f}},
    {"BOY", 4, {.62f, .15f, .55f, 0.f, 0.f, .5f, 1.f, .1f, .5f, .25f, .5f, .3f, .1f, .3f, .3f, 0.f}},
    {"GIANT", 0, {.2f, .1f, .4f, .3f, .2f, .45f, 1.f, .2f, .5f, .2f, .35f, .4f, 0.f, .3f, .5f, 0.f}},
    {"MONK CHANT", 6, {.45f, .1f, .45f, .1f, .8f, .45f, 1.f, .2f, .3f, 0.f, .4f, .5f, 0.f, .3f, .7f, 0.f}},
    {"ANGEL CHOIR", 11, {.55f, .3f, .55f, 0.f, 1.f, .5f, 1.f, .25f, .4f, .25f, .45f, .4f, .1f, .3f, .8f, 0.f}},
    {"ROBOT", 3, {.5f, 0.f, .7f, 0.f, 0.f, .55f, 1.f, 0.f, .5f, 0.f, .5f, 0.f, .1f, .3f, .2f, .35f}},
    {"MACHINE CHOIR", 5, {.5f, .05f, .75f, 0.f, .4f, .55f, 1.f, .05f, .5f, 0.f, .5f, 0.f, .3f, .4f, .3f, .15f}},
    {"CROAK", 12, {.4f, .2f, .45f, .6f, 0.f, .5f, 1.f, .15f, .5f, .2f, .4f, .3f, .1f, .3f, .3f, 0.f}},
    {"WHISPER", 13, {.5f, .6f, .6f, 0.f, .2f, .5f, .8f, .1f, .4f, 0.f, .5f, .3f, .2f, .3f, .5f, 0.f}},
    {"ELF", 9, {.85f, .2f, .6f, 0.f, .2f, .55f, 1.f, .15f, .5f, .4f, .6f, .2f, .3f, .4f, .4f, 0.f}},
    {"LO-FI TAPE", 8, {.48f, .15f, .4f, .1f, 0.f, .5f, 1.f, .15f, .5f, .3f, .45f, .3f, .25f, .35f, .2f, .5f}},
    {"DUB ECHO", 1, {.5f, .1f, .5f, 0.f, 0.f, .5f, 1.f, .15f, .6f, .2f, .5f, .3f, .6f, .7f, .3f, 0.f}},
    {"SPOKEN", 1, {.5f, .05f, .5f, 0.f, 0.f, .55f, .2f, 0.f, .5f, 0.f, .5f, .3f, 0.f, .3f, .15f, 0.f}},
};
static const int NPRESET = (int) (sizeof(PRESETS) / sizeof(PRESETS[0]));

// ------------------------------------------------------------------------------------------------ the module
struct Lied : Module, follow::Host {
	enum ParamId {
		LANG_PARAM, VOICE_PARAM, STEP_PARAM, LINE_PARAM, RESTART_PARAM, NEXT_PARAM,
		FORMANT_PARAM, BREATH_PARAM, BRIGHT_PARAM, ROUGH_PARAM, CHOIR_PARAM,
		SPEED_PARAM, VOWEL_PARAM, GLIDE_PARAM, ACCENT_PARAM,
		VDEPTH_PARAM, VRATE_PARAM, VDELAY_PARAM,
		ECHO_PARAM, FDBK_PARAM, VERB_PARAM, LOFI_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, ACC_INPUT, SLIDE_INPUT, MOD_INPUT, CLOCK_INPUT, RESET_INPUT,
		LINE_INPUT, NEXT_INPUT, FORMANT_INPUT, BREATH_INPUT, SPEED_INPUT, VOWEL_INPUT, CHOIR_INPUT, VERB_INPUT,
		INPUTS_LEN
	};
	enum OutputId { SYL_OUTPUT, END_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	lied::Worker worker;
	lied::Singer singer;
	lied::Fx fx;
	lied::Params prm;

	// the lyrics (UI thread; the worker gets copies)
	std::mutex textM;
	std::string texts[4];
	std::atomic<uint32_t> textGen{1};
	int preset = 0;
	std::atomic<int> shownPreset{0};
	int clockMode = 0;

	// audio thread
	dsp::SchmittTrigger gateTrig, nextTrig, resetTrig, clockTrig;
	dsp::BooleanTrigger restartBtn, nextBtn;
	dsp::PulseGenerator sylPulse, endPulse;
	bool gateHigh = false;
	int cur = 0;            // the next unit to sing
	int lastLine = -1;
	const lied::Line* singing = nullptr;
	double sincePulse = 1e9, period = 0.0;
	float bpm = 120.f;
	int sampleCount = 0;

	// for the UI thread
	std::atomic<int> shownLine{0}, shownUnit{-1}, shownStep{0};
	std::atomic<float> shownSemis{0.f};
	std::atomic<bool> shownHeld{false};

	Lied() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "LIED";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(ACC_INPUT, follow::ACC);
		fol.bind(SLIDE_INPUT, follow::SLIDE);
		fol.bind(MOD_INPUT, follow::MOD);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		std::vector<std::string> ln, vn;
		for (const Lang& l : LANGS)
			ln.push_back(l.name);
		for (const VoiceDef& v : VOICES)
			vn.push_back(v.name);
		configSwitch(LANG_PARAM, 0.f, NLANG - 1, 0.f, "Language", ln);
		configSwitch(VOICE_PARAM, 0.f, NVOICE - 1, 2.f, "Voice", vn);
		configSwitch(STEP_PARAM, 0.f, 2.f, 0.f, "Step (what each gate sings)",
		             {"SYLL: the next syllable", "WORD: the next word", "FREE: the whole line"});
		configSwitch(LINE_PARAM, 0.f, 3.f, 0.f, "Line", {"1", "2", "3", "4"});
		configButton(RESTART_PARAM, "Restart (the line from its first syllable)");
		configButton(NEXT_PARAM, "Next (skip a syllable)");
		configParam(FORMANT_PARAM, 0.f, 1.f, 0.5f, "Formant (smaller / bigger singer)", "%", 0.f, 200.f, -100.f);
		configParam(BREATH_PARAM, 0.f, 1.f, 0.1f, "Breath", "%", 0.f, 100.f);
		configParam(BRIGHT_PARAM, 0.f, 1.f, 0.5f, "Bright", "%", 0.f, 200.f, -100.f);
		configParam(ROUGH_PARAM, 0.f, 1.f, 0.f, "Rough", "%", 0.f, 100.f);
		configParam(CHOIR_PARAM, 0.f, 1.f, 0.f, "Choir (three more singers)", "%", 0.f, 100.f);
		configParam(SPEED_PARAM, 0.f, 1.f, 0.5f, "Speed (of the consonants)", "x", 4.f, 0.5f);
		configParam(VOWEL_PARAM, 0.f, 1.f, 1.f, "Vowel (held while the gate is: 100% = as long as the gate)", "%", 0.f, 100.f);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.1f, "Glide", "%", 0.f, 100.f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent (how much ACC lifts a note)", "%", 0.f, 100.f);
		configParam(VDEPTH_PARAM, 0.f, 1.f, 0.3f, "Vibrato depth", " semitones");
		configParam(VRATE_PARAM, 0.f, 1.f, 0.5f, "Vibrato rate", " Hz", 0.f, 5.f, 3.f);
		configParam(VDELAY_PARAM, 0.f, 1.f, 0.3f, "Vibrato delay", " s", 0.f, 1.5f);
		configParam(ECHO_PARAM, 0.f, 1.f, 0.1f, "Echo (a dotted eighth of the clock)", "%", 0.f, 100.f);
		configParam(FDBK_PARAM, 0.f, 1.f, 0.35f, "Echo feedback", "%", 0.f, 90.f);
		configParam(VERB_PARAM, 0.f, 1.f, 0.25f, "Reverb", "%", 0.f, 100.f);
		configParam(LOFI_PARAM, 0.f, 1.f, 0.f, "Lo-fi", "%", 0.f, 100.f);
		configInput(VOCT_INPUT, "V/Oct");
		configInput(GATE_INPUT, "Gate (each gate sings the next syllable)");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(ACC_INPUT, "Accent (gate)");
		configInput(SLIDE_INPUT, "Slide (gate: the next note goes on with the same vowel)");
		configInput(MOD_INPUT, "Mod (0..10 V: more vibrato)");
		configInput(CLOCK_INPUT, "Clock (the echo's tempo; GENRA: 16ths, see the context menu)");
		configInput(RESET_INPUT, "Reset (the line from its first syllable)");
		configInput(LINE_INPUT, "Line (0..10 V: lines 1..4)");
		configInput(NEXT_INPUT, "Next (trigger: skip a syllable)");
		configInput(FORMANT_INPUT, "Formant CV (±5 V)");
		configInput(BREATH_INPUT, "Breath CV (±5 V)");
		configInput(SPEED_INPUT, "Speed CV (±5 V)");
		configInput(VOWEL_INPUT, "Vowel CV (±5 V)");
		configInput(CHOIR_INPUT, "Choir CV (±5 V)");
		configInput(VERB_INPUT, "Reverb CV (±5 V)");
		configOutput(SYL_OUTPUT, "Syllable (a trigger at each one sung)");
		configOutput(END_OUTPUT, "End (a trigger at the last syllable of the line)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		for (int p : {LANG_PARAM, VOICE_PARAM, STEP_PARAM, LINE_PARAM, ECHO_PARAM, FDBK_PARAM, VERB_PARAM, LOFI_PARAM})
			paramQuantities[p]->randomizeEnabled = false;
		texts[0] = "Hal-lo Welt, ich bin da";
		texts[1] = "wir sin-gen durch die Nacht";
		texts[2] = "la la la la la";
		texts[3] = "";
		applyPreset(0);
	}

	// ---------------------------------------------------------------------------------- lyrics, presets
	std::string text(int i) {
		std::lock_guard<std::mutex> lk(textM);
		return texts[i];
	}
	void setText(int i, const std::string& t) {
		std::lock_guard<std::mutex> lk(textM);
		if (texts[i] == t)
			return;
		texts[i] = t;
		textGen++;
	}
	std::string voiceName() {
		int l = clamp((int) std::round(params[LANG_PARAM].getValue()), 0, NLANG - 1);
		int v = clamp((int) std::round(params[VOICE_PARAM].getValue()), 0, NVOICE - 1);
		return std::string(LANGS[l].code) + "+" + VOICES[v].variant;
	}
	int voiceF0() { return VOICES[clamp((int) std::round(params[VOICE_PARAM].getValue()), 0, NVOICE - 1)].f0; }

	void applyPreset(int i) {
		preset = ((i % NPRESET) + NPRESET) % NPRESET;
		const Preset& p = PRESETS[preset];
		params[VOICE_PARAM].setValue((float) p.voice);
		for (int k = 0; k < NKNOB; k++)
			params[FORMANT_PARAM + k].setValue(p.k[k]);
		shownPreset = preset;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		clockMode = 0;
		applyPreset(0);
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		fx.setRate(e.sampleRate);
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_t* a = json_array();
		for (int i = 0; i < 4; i++)
			json_array_append_new(a, json_string(text(i).c_str()));
		json_object_set_new(r, "lyrics", a);
		json_object_set_new(r, "preset", json_integer(preset));
		json_object_set_new(r, "clockMode", json_integer(clockMode));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* a = json_object_get(r, "lyrics"))
			for (int i = 0; i < 4 && i < (int) json_array_size(a); i++)
				setText(i, json_string_value(json_array_get(a, i)) ? json_string_value(json_array_get(a, i)) : "");
		if (json_t* j = json_object_get(r, "preset")) {
			preset = clamp((int) json_integer_value(j), 0, NPRESET - 1);
			shownPreset = preset;
		}
		if (json_t* j = json_object_get(r, "clockMode"))
			clockMode = clamp((int) json_integer_value(j), 0, 3);
	}

	// ---------------------------------------------------------------------------------- the sound
	float knob(int p, int in, float lo = 0.f, float hi = 1.f) {
		float v = params[p].getValue();
		if (in >= 0 && inputs[in].isConnected())
			v += inputs[in].getVoltage() / 10.f;
		return clamp(v, lo, hi);
	}

	int activeLine() {
		if (inputs[LINE_INPUT].isConnected())
			return clamp((int) (inputs[LINE_INPUT].getVoltage() / 2.5f), 0, 3);
		return clamp((int) std::round(params[LINE_PARAM].getValue()), 0, 3);
	}

	void readParams() {
		prm.formant = std::exp2((knob(FORMANT_PARAM, FORMANT_INPUT) - 0.5f) * 1.2f);
		prm.breath = knob(BREATH_PARAM, BREATH_INPUT);
		prm.bright = params[BRIGHT_PARAM].getValue();
		prm.rough = params[ROUGH_PARAM].getValue();
		prm.choir = knob(CHOIR_PARAM, CHOIR_INPUT);
		prm.speed = std::exp2((knob(SPEED_PARAM, SPEED_INPUT) - 0.5f) * 2.f);
		prm.vowel = knob(VOWEL_PARAM, VOWEL_INPUT);
		float g = params[GLIDE_PARAM].getValue();
		prm.glide = g * g * 0.8f;
		float mod = inputs[MOD_INPUT].isConnected() ? clamp(inputs[MOD_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 0.f;
		prm.vibDepth = params[VDEPTH_PARAM].getValue() + mod;
		prm.vibRate = 3.f + 5.f * params[VRATE_PARAM].getValue();
		prm.vibDelay = 1.5f * params[VDELAY_PARAM].getValue();
	}

	void processClock(float sampleRate) {
		int ppqn = CLOCK_PPQN[clamp(clockMode, 0, 3)];
		sincePulse += 1.0;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sincePulse < sampleRate * 4.0) {
				period = period > 0.0 ? period + (sincePulse - period) * 0.5 : sincePulse;
				bpm = clamp((float) (60.0 * sampleRate / (period * ppqn)), 20.f, 300.f);
			}
			sincePulse = 0.0;
		}
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		if (fx.dl.empty())
			fx.setRate(args.sampleRate);
		lied::Core& core = lied::Core::get();
		singer.setRate(args.sampleRate, core.rate > 0 ? (float) core.rate : 22050.f);
		if ((sampleCount++ & 31) == 0)
			readParams();
		processClock(args.sampleRate);

		int ln = activeLine();
		if (ln != lastLine) {
			lastLine = ln;
			cur = 0;
		}
		int step = clamp((int) std::round(params[STEP_PARAM].getValue()), 0, 2);
		if (restartBtn.process(params[RESTART_PARAM].getValue() > 0.5f) |
		    resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			cur = 0;
		if (nextBtn.process(params[NEXT_PARAM].getValue() > 0.5f) |
		    nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
			cur++;

		float semis = inputs[VOCT_INPUT].getVoltage() * 12.f;
		bool slide = inputs[SLIDE_INPUT].getVoltage() > 1.f;
		singer.note(semis, slide);

		float g = inputs[GATE_INPUT].getVoltage();
		bool high = gateHigh ? g > 0.5f : g >= 1.f;
		if (high && !gateHigh) {
			const lied::Line* line = worker.line[ln].load();
			if (slide && singer.sounding() && step != 2) {
				singer.hold();  // a melisma: the vowel goes on to the new note
			} else if (line && !line->empty()) {
				const std::vector<lied::Unit>* units = step == 0 ? &line->syl : step == 1 ? &line->word : nullptr;
				const lied::Unit* u = nullptr;
				int n = units ? (int) units->size() : 1;
				int idx = ((cur % n) + n) % n;
				u = units ? &(*units)[idx] : &line->whole;
				float gain = 1.f;
				if (inputs[VEL_INPUT].isConnected())
					gain = 0.25f + 0.75f * clamp(inputs[VEL_INPUT].getVoltage() / 10.f, 0.f, 1.f);
				if (inputs[ACC_INPUT].getVoltage() > 1.f)
					gain *= 1.f + 0.8f * params[ACCENT_PARAM].getValue();
				worker.inUse[1] = worker.inUse[0].load();
				worker.inUse[0] = line;
				singing = line;
				singer.start(u, gain, prm);
				if (step == 2)
					singer.release();  // FREE: the line goes on by itself
				sylPulse.trigger(1e-3f);
				if (idx == n - 1)
					endPulse.trigger(1e-3f);
				cur = idx + 1;
				shownUnit = idx;
			}
		} else if (!high && gateHigh)
			singer.release();
		gateHigh = high;
		if (!singer.sounding() && singing) {
			singing = nullptr;
			worker.inUse[0] = nullptr;
			worker.inUse[1] = nullptr;
		}

		float l, r;
		bool acc = inputs[ACC_INPUT].getVoltage() > 1.f && gateHigh;
		lied::Params p = prm;
		if (acc)
			p.bright = clamp(p.bright + 0.25f * params[ACCENT_PARAM].getValue(), 0.f, 1.f);
		singer.process(p, l, r);
		fx.process(l, r, params[ECHO_PARAM].getValue(), params[FDBK_PARAM].getValue() * 0.9f,
		           knob(VERB_PARAM, VERB_INPUT), params[LOFI_PARAM].getValue(), bpm);
		l = 5.f * std::tanh(1.4f * l);
		r = 5.f * std::tanh(1.4f * r);
		if (outputs[R_OUTPUT].isConnected() || pulled(args.frame)) {
			outputs[L_OUTPUT].setVoltage(l);
			outputs[R_OUTPUT].setVoltage(r);
		} else {
			outputs[L_OUTPUT].setVoltage(0.5f * (l + r));
			outputs[R_OUTPUT].setVoltage(0.f);
		}
		outputs[SYL_OUTPUT].setVoltage(sylPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[END_OUTPUT].setVoltage(endPulse.process(args.sampleTime) ? 10.f : 0.f);

		shownLine = ln;
		shownStep = step;
		shownSemis = semis;
		shownHeld = singer.v[0].active && singer.v[0].held;
	}
};

// ------------------------------------------------------------------------------------------------ the panel
static Vec mm(const float* p) { return mm2px(Vec(p[0], p[1])); }
static Rect mmBox(const float* p) { return Rect(mm2px(Vec(p[0], p[1])), mm2px(Vec(p[2], p[3]))); }

/** The display: line and syllable, the syllable sung (big) and its phonemes, its vowel's spectrum, voice and note. */
struct Screen : Widget {
	Lied* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x, h = box.size.y;
		float x0 = 11.f, x1 = w - 11.f;
		if (module && module->worker.failed) {
			jiant::text(vg, mono, x0, 16, "eSpeak did not start:", 7.f, jiant::ERR, NVG_ALIGN_LEFT, 0.f);
			jiant::text(vg, mono, x0, 28, module->worker.error, 7.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
			return;
		}
		int ln = module ? (int) module->shownLine : 1;
		int step = module ? (int) module->shownStep : 0;
		const lied::Line* line = module ? module->worker.line[ln].load() : nullptr;
		int ui = module ? (int) module->shownUnit : 3;
		const lied::Unit* u = nullptr;
		int count = 0;
		if (line && !line->empty()) {
			const std::vector<lied::Unit>& us = step == 1 ? line->word : line->syl;
			count = step == 2 ? 1 : (int) us.size();
			if (step == 2)
				u = &line->whole;
			else if (ui >= 0 && ui < count)
				u = &us[ui];
		}
		std::string head = string::f("LINE %d / 4", ln + 1);
		if (count > 0 && step < 2)
			head += string::f(" \xc2\xb7 %s %d / %d", step == 0 ? "SYLLABLE" : "WORD", u ? ui + 1 : 0, count);
		else if (step == 2)
			head += " \xc2\xb7 FREE";
		if (module && module->worker.busy > 0)
			head += " \xc2\xb7 \xe2\x80\xa6";
		jiant::text(vg, bold, x0, 13.5f, head, 5.6f, jiant::DIM, NVG_ALIGN_LEFT, 0.9f);
		std::string big = u ? (step == 2 ? "FREE" : u->text) : (module ? "\xe2\x80\x94" : "durch");
		jiant::text(vg, bold, x0, 30.5f, big, 15.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.5f);
		std::string ph = u ? u->phon : (module ? "" : "d UR C");
		if (step == 2 && u)
			ph = "";
		if (ph.size() > 28)
			ph = ph.substr(0, 27) + "\xe2\x80\xa6";
		jiant::text(vg, mono, x0, 39.5f, ph.empty() ? "" : "[ " + ph + " ]", 6.4f, ACCENT, NVG_ALIGN_LEFT, 0.6f);
		// the vowel's spectrum
		float sx = w * 0.36f, sw = w * 0.30f, base = h - 9.f, top = 7.f;
		nvgBeginPath(vg);
		for (int k = 0; k < 48; k++) {
			float v = u ? u->env[k] : 0.5f + 0.4f * std::sin(k * 0.4f);
			float x = sx + sw * k / 47.f, y = base - (base - top) * v;
			if (k == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgStrokeColor(vg, ACCENT);
		nvgStrokeWidth(vg, 1.3f);
		nvgStroke(vg);
		jiant::text(vg, mono, sx, h - 2.f, "0", 5.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		jiant::text(vg, mono, sx + sw, h - 2.f, "5 kHz", 5.f, jiant::DIM, NVG_ALIGN_RIGHT, 0.f);
		// right: language, voice, note, step
		std::string voice = "DE \xc2\xb7 KLATT 3";
		std::string note = "A4";
		if (module) {
			int l = clamp((int) std::round(module->params[Lied::LANG_PARAM].getValue()), 0, NLANG - 1);
			int v = clamp((int) std::round(module->params[Lied::VOICE_PARAM].getValue()), 0, NVOICE - 1);
			voice = string::uppercase(LANGS[l].code) + " \xc2\xb7 " + VOICES[v].name;
			int n = (int) std::round(module->shownSemis.load()) + 60;
			note = string::f("%s%d", NOTE_NAMES[((n % 12) + 12) % 12], (int) std::floor(n / 12.f) - 1);
		}
		jiant::text(vg, bold, x1, 13.5f, voice, 5.6f, jiant::DIM, NVG_ALIGN_RIGHT, 0.9f);
		jiant::text(vg, bold, x1, 30.5f, note, 15.f, jiant::WHITE, NVG_ALIGN_RIGHT, 0.5f);
		std::string st = std::string("STEP ") + STEP_NAMES[step];
		if (module && module->shownHeld)
			st += " \xc2\xb7 HOLD";
		jiant::text(vg, bold, x1, 39.5f, st, 5.6f, ACCENT, NVG_ALIGN_RIGHT, 0.9f);
	}
};

/** A line of the lyrics: typed into directly; while it is not being typed in, the syllable sung is lit. */
struct LyricField : app::LedDisplayTextField {
	Lied* module = nullptr;
	int index = 0;
	float fontSize = 8.6f;
	LyricField() {
		bgColor = nvgRGBA(0, 0, 0, 0);
		color = jiant::TXT;
		textOffset = Vec(0, -3.f);
		placeholder = "click to type\xe2\x80\xa6";
	}
	bool editing() { return this == APP->event->selectedWidget; }
	void onChange(const ChangeEvent& e) override {
		if (module)
			module->setText(index, getText());
		LedDisplayTextField::onChange(e);
	}
	void step() override {
		// show what the module holds (a patch loaded, an undo) unless it is being typed in
		if (module && !editing()) {
			std::string t = module->text(index);
			if (t != getText())
				setText(t);
		}
		LedDisplayTextField::step();
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		std::shared_ptr<window::Font> font = jiant::boldFont();
		bool active = module ? (int) module->shownLine == index : index == 1;
		if (editing()) {
			if (font && font->handle >= 0) {
				bndSetFont(font->handle);
				NVGcolor hl = ACCENT;
				hl.a = 0.5f;
				int begin = std::min(cursor, selection), end = std::max(cursor, selection);
				bndIconLabelCaret(args.vg, textOffset.x, textOffset.y, box.size.x - 2 * textOffset.x, box.size.y, -1,
				                  jiant::WHITE, fontSize, text.c_str(), hl, begin, end);
				bndSetFont(APP->window->uiFont->handle);
			}
		} else if (font && font->handle >= 0) {
			NVGcontext* vg = args.vg;
			float y = box.size.y / 2 + fontSize * 0.36f;
			if (text.empty()) {
				jiant::text(vg, font, 4.f, y, placeholder, fontSize, nvgRGB(0x4a, 0x4c, 0x57), NVG_ALIGN_LEFT, 0.3f);
			} else {
				// the bytes being sung (this line only)
				int a = -1, b = -1;
				if (module && active) {
					const lied::Line* line = module->worker.line[index].load();
					int step = module->shownStep, ui = module->shownUnit;
					if (line && line->text == text && ui >= 0) {
						const auto& at = step == 1 ? line->wordAt : line->sylAt;
						if (step == 2) {
							a = 0;
							b = (int) text.size();
						} else if (ui < (int) at.size()) {
							a = at[ui].first;
							b = at[ui].second;
						}
					}
				}
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, fontSize);
				nvgTextLetterSpacing(vg, 0.3f);
				NVGcolor base = active ? jiant::TXT : jiant::DIM;
				float x = 4.f;
				auto part = [&](int s, int e, NVGcolor c, bool lit) {
					if (e <= s)
						return;
					std::string t = text.substr(s, e - s);
					float bounds[4];
					float adv = nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, bounds);
					if (lit) {
						NVGcolor bg = ACCENT;
						bg.a = 0.22f;
						jiant::roundRect(vg, x - 1.5f, 2.f, adv + 3.f, box.size.y - 4.f, 2.f, bg);
					}
					jiant::text(vg, font, x, y, t, fontSize, c, NVG_ALIGN_LEFT, 0.3f);
					x += adv;
				};
				if (a >= 0 && b > a && b <= (int) text.size()) {
					part(0, a, base, false);
					part(a, b, ACCENT, true);
					part(b, (int) text.size(), base, false);
				} else
					part(0, (int) text.size(), base, false);
			}
		}
		nvgResetScissor(args.vg);
		Widget::drawLayer(args, layer);
	}
	int getTextPosition(math::Vec mousePos) override {
		std::shared_ptr<window::Font> font = jiant::boldFont();
		if (!font || font->handle < 0)
			return 0;
		bndSetFont(font->handle);
		int pos = bndIconLabelTextPosition(APP->window->vg, textOffset.x, textOffset.y, box.size.x - 2 * textOffset.x,
		                                   box.size.y, -1, fontSize, text.c_str(), mousePos.x, mousePos.y);
		bndSetFont(APP->window->uiFont->handle);
		return pos;
	}
};

/** The line's number: a click makes it the line that is sung; the line in use has its bar lit. */
struct LinePick : app::ParamWidget {
	Lied* module = nullptr;
	int index = 0;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (engine::ParamQuantity* pq = getParamQuantity())
				pq->setValue((float) index);
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			bool on = module ? (int) module->shownLine == index : index == 1;
			if (on)
				jiant::roundRect(args.vg, 0.f, 2.f, 2.2f, box.size.y - 4.f, 1.f, ACCENT);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** A choice of a switch parameter as a field with its value; a click opens the list (the ▾ is on the panel). */
struct SelectField : app::ParamWidget {
	std::vector<std::string> names;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (engine::ParamQuantity* pq = getParamQuantity()) {
				ui::Menu* menu = createMenu();
				menu->addChild(createMenuLabel(pq->getLabel()));
				for (size_t i = 0; i < names.size(); i++)
					menu->addChild(createCheckMenuItem(names[i], "", [=]() { return (int) std::round(pq->getValue()) == (int) i; },
					                                   [=]() { pq->setValue((float) i); }));
			}
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			engine::ParamQuantity* pq = getParamQuantity();
			int v = pq ? (int) std::round(pq->getValue()) : 0;
			std::string s = v >= 0 && v < (int) names.size() ? names[v] : "?";
			jiant::text(args.vg, jiant::boldFont(), 6.f, box.size.y / 2 + 2.4f, s, 6.6f, jiant::TXT, NVG_ALIGN_LEFT, 0.5f);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** The preset field in the header: number and name; a click opens the list. */
struct PresetField : Widget {
	Lied* module = nullptr;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (module) {
				Lied* m = module;
				ui::Menu* menu = createMenu();
				menu->addChild(createMenuLabel("Preset (the voice and the knobs; lyrics and language stay)"));
				for (int i = 0; i < NPRESET; i++)
					menu->addChild(createCheckMenuItem(string::f("%02d  %s", i + 1, PRESETS[i].name), "",
					                                   [=]() { return (int) m->shownPreset == i; },
					                                   [=]() { m->applyPreset(i); }));
			}
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int p = module ? (int) module->shownPreset : 1;
			jiant::text(args.vg, jiant::boldFont(), 6.f, box.size.y / 2 + 2.5f,
			            string::f("%02d  %s", p + 1, PRESETS[clamp(p, 0, NPRESET - 1)].name), 6.8f, jiant::TXT,
			            NVG_ALIGN_LEFT, 0.6f);
		}
		Widget::drawLayer(args, layer);
	}
};

struct LiedWidget : ModuleWidget {
	LyricField* fields[4] = {};
	std::string sentVoice;
	uint32_t sentGen = 0;
	double editedAt = 0.0;

	LiedWidget(Lied* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lied.svg")));
		follow::addStrip(this, module, {mm(L::VOCT), mm(L::GATE), mm(L::VEL), mm(L::IN_ACC), mm(L::SLIDE), mm(L::IN_MOD),
		                                mm(L::CLOCK), mm(L::IN_RESET)});
		jiant::addScrews(this);

		Screen* scr = new Screen;
		scr->module = module;
		scr->box = mmBox(L::DISPLAY);
		addChild(scr);
		PresetField* pf = new PresetField;
		pf->module = module;
		pf->box = mmBox(L::PRESETF);
		addChild(pf);

		// the lyrics: four lines in the field, a number to pick each
		Rect ly = mmBox(L::LYRICSF);
		float lh = ly.size.y / 4.f;
		for (int i = 0; i < 4; i++) {
			LinePick* pick = createParam<LinePick>(Vec(ly.pos.x + 1.5f, ly.pos.y + i * lh), module, Lied::LINE_PARAM);
			pick->box.size = Vec(mm2px(4.2f), lh);
			pick->module = module;
			pick->index = i;
			addParam(pick);
			LyricField* f = createWidget<LyricField>(Vec(ly.pos.x + mm2px(4.4f), ly.pos.y + i * lh));
			f->box.size = Vec(ly.size.x - mm2px(5.0f), lh);
			f->module = module;
			f->index = i;
			if (module)
				f->setText(module->text(i));
			else
				f->setText(i == 0 ? "Hal-lo Welt, ich bin da" : i == 1 ? "wir sin-gen durch die Nacht" : i == 2 ? "la la la la la" : "");
			addChild(f);
			fields[i] = f;
		}

		SelectField* lang = createParam<SelectField>(mmBox(L::LANGF).pos, module, Lied::LANG_PARAM);
		lang->box.size = mmBox(L::LANGF).size;
		for (const Lang& l : LANGS)
			lang->names.push_back(l.name);
		addParam(lang);
		SelectField* voice = createParam<SelectField>(mmBox(L::VOICEF).pos, module, Lied::VOICE_PARAM);
		voice->box.size = mmBox(L::VOICEF).size;
		for (const VoiceDef& v : VOICES)
			voice->names.push_back(v.name);
		addParam(voice);

		jiant::LampRow* stepRow = createParam<jiant::LampRow>(Vec(), module, Lied::STEP_PARAM);
		float sp[3] = {mm2px(L::STEP_LAMPS[0]), mm2px(L::STEP_LAMPS[1]), mm2px(L::STEP_LAMPS[2])};
		stepRow->place(sp, 3);
		stepRow->accent = ACCENT;
		addParam(stepRow);
		addParam(createParamCentered<VCVBezel>(mm(L::RESTART), module, Lied::RESTART_PARAM));
		addParam(createParamCentered<VCVBezel>(mm(L::NEXT), module, Lied::NEXT_PARAM));

		addParam(createParamCentered<GLargeKnob>(mm(L::FORMANT), module, Lied::FORMANT_PARAM));
		addParam(createParamCentered<GKnob>(mm(L::BREATH), module, Lied::BREATH_PARAM));
		addParam(createParamCentered<GKnob>(mm(L::BRIGHT), module, Lied::BRIGHT_PARAM));
		addParam(createParamCentered<GSmallKnob>(mm(L::ROUGH), module, Lied::ROUGH_PARAM));
		addParam(createParamCentered<GSmallKnob>(mm(L::CHOIR), module, Lied::CHOIR_PARAM));
		const float* small[] = {L::SPEED, L::VOWEL, L::GLIDE, L::ACCENT, L::VDEPTH, L::VRATE, L::VDELAY,
		                        L::ECHO, L::FDBK, L::VERB, L::LOFI};
		for (int i = 0; i < 11; i++)
			addParam(createParamCentered<GSmallKnob>(mm(small[i]), module, Lied::SPEED_PARAM + i));

		const float* ins[] = {L::VOCT, L::GATE, L::VEL, L::IN_ACC, L::SLIDE, L::IN_MOD, L::CLOCK, L::IN_RESET,
		                      L::IN_LINE, L::IN_NEXT, L::IN_FORM, L::IN_BREATH, L::IN_SPEED, L::IN_VOWEL, L::IN_CHOIR,
		                      L::IN_VERB};
		for (int i = 0; i < Lied::INPUTS_LEN; i++)
			addInput(createInputCentered<PJ301MPort>(mm(ins[i]), module, i));
		const float* outs[] = {L::OUT_SYL, L::OUT_END, L::OUT_L, L::OUT_R};
		for (int i = 0; i < Lied::OUTPUTS_LEN; i++)
			addOutput(createOutputCentered<PJ301MPort>(mm(outs[i]), module, i));
	}

	/** Hand the lines to the worker: after typing has paused a moment, or at once when the voice changed. */
	void step() override {
		ModuleWidget::step();
		Lied* m = getModule<Lied>();
		if (!m)
			return;
		std::string voice = m->voiceName();
		uint32_t gen = m->textGen;
		double now = system::getTime();
		if (gen != sentGen && editedAt == 0.0)
			editedAt = now;
		bool typing = false;
		for (LyricField* f : fields)
			typing |= f && f->editing();
		bool due = voice != sentVoice || (gen != sentGen && (!typing || now - editedAt > 0.4));
		if (!due)
			return;
		for (int i = 0; i < 4; i++)
			m->worker.want(i, m->text(i), voice, m->voiceF0());
		sentVoice = voice;
		sentGen = gen;
		editedAt = 0.0;
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Lied* m = getModule<Lied>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {CLOCK_MODES[0], CLOCK_MODES[1], CLOCK_MODES[2], CLOCK_MODES[3]},
		                                      [=]() { return (size_t) m->clockMode; },
		                                      [=](size_t i) { m->clockMode = (int) i; }));
		std::vector<std::string> names;
		for (int i = 0; i < NPRESET; i++)
			names.push_back(string::f("%02d  %s", i + 1, PRESETS[i].name));
		menu->addChild(createIndexSubmenuItem("Preset", names, [=]() { return (size_t) m->shownPreset; },
		                                      [=](size_t i) { m->applyPreset((int) i); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Lyrics: a hyphen cuts a word where you want it (sin-gen)"));
		menu->addChild(createMenuLabel("Voice: eSpeak NG 1.52 (GPL-3.0-or-later)"));
	}
};

}  // namespace genra_lied

Model* modelLied = createModel<genra_lied::Lied, genra_lied::LiedWidget>("Lied");
