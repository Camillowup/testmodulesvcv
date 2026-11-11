// SPDX-License-Identifier: GPL-3.0-only
// CANTA: MIKU-FM's singing voice as a VCV Rack voice. The voice (an integer formant voice that sings Japanese morae), its
// effects, the morae and the lyrics grammar run unchanged in a private copy of the CANTA core (CantaCore.hpp); this
// file is the panel, the jacks and the glue: knobs and CV into the voice, V/OCT + GATE into its notes (each gate the
// next syllable of the lyrics), CLOCK into SCAT's tempo, the 44.1 kHz audio resampled to Rack's rate, and the four
// singers (own pixel art, CantaSprites.hpp; nothing of MIKU-FM's art) singing along.
// After KURBEL (src/kurbel/Kurbel.cpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "../../genra/ui/Look.hpp"
#include "CantaCore.hpp"
#include "CantaLayout.hpp"
#include "CantaSprites.hpp"
#include <atomic>
#include <array>
#include <cstring>
#include <mutex>

namespace genra_canta {

#define CANTA_RGB 0xff, 0x7a, 0x59   // keep in step with tools/jpanel.py COLORS

static const int NK = 16;                                       // the knobs: VOICE (8), SHAPE (4), FX (4) = core 0..15
static const int NV = 8;                                        // VOICE knobs with CV
static const char* KNOB_NAMES[NK] = {"Breath", "Brightness", "Glide (portamento)", "Tension (50: neutral)",
                                     "Fry (vocal fry, a growl)", "Whisper", "Formant (fine shift, 50: neutral)",
                                     "Human (drift, jitter, overshoot)", "Attack", "Release", "Scoop (the onset from below)",
                                     "Vibrato depth", "Chorus", "Delay", "Reverb", "Drive"};
static const char* LANG_NAMES[4] = {"JAPAN", "ANIMAL", "SCAT", "VOWEL"};
static const char* CHR_NAMES[5] = {"DEEP", "LOW", "MID", "YOUNG", "TINY"};
static const char* HARM_NAMES[7] = {"OFF", "-12", "+3", "+4", "+5", "+7", "+12"};
static const char* CLOCK_MODES[4] = {"16ths (4 PPQN)", "8ths (2 PPQN)", "Quarters (1 PPQN)", "24 PPQN"};
static const int CLOCK_PPQN[4] = {4, 2, 1, 24};
static const int MAXU = 96;                                     // lyric units the display keeps

// ------------------------------------------------------------------------------------------------ the module
struct Canta : Module, follow::Host {
	enum ParamId {
		ENUMS(K_PARAM, NK),
		LEVEL_PARAM, MIX_PARAM, HARM_PARAM, LYRICS_PARAM, SYL_PARAM, LANG_PARAM, CHR_PARAM, DANCE_PARAM,
		PREV_PARAM, NEXT_PARAM, CALL_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(V_INPUT, NV),
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, CLOCK_INPUT, RESET_INPUT, SYL_INPUT, NEXT_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { CALL_LIGHT, LIGHTS_LEN };
	enum { BLOCK = 32, CALL_NOTE = 67 };

	CantaCore core;
	std::string status;

	// settings
	int preset = 0;
	int clockMode = 0;
	bool keepLyrics = false;                      // (right-click) presets leave the lyrics alone
	bool legatoNext = false;                      // (right-click) a new pitch under a held gate sings the next syllable
	bool fromPatch = false;
	std::string lyrics;                           // the text (engine thread's copy: what the core has)
	std::atomic<int> presetReq{-1};
	std::mutex lyricsMutex;
	std::string lyricsReq;                        // from the UI, under lyricsMutex
	std::atomic<bool> lyricsPending{false};

	// for the UI thread
	std::array<std::array<char, 16>, JCA_NP> values{};
	std::atomic<int> shownPreset{0};
	std::atomic<float> shownBpm{120.f};
	std::atomic<int> mouth{5}, envLevel{0}, sung{-1}, nextPos{0}, sungMora{0}, beats{0}, lyricError{-1};
	std::atomic<bool> gateShown{false}, clocked{false};
	struct Unit {
		char t[4];
		uint8_t flags;
	};
	std::array<Unit, MAXU> units{};
	std::atomic<int> nUnits{0};
	std::array<std::atomic<int>, 8> history{};      // the last babbled morae (ANIMAL, SCAT)
	std::atomic<uint32_t> histN{0};
	std::string shownLyrics;                        // UI copy of the text (the editor's start)
	std::mutex shownMutex;

	// engine thread
	int sent[JCA_NP];
	float devL[BLOCK], devR[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceUi = 0;
	bool gate = false, gateIn = false;
	int note = 60;
	uint32_t lastMora = 0;
	dsp::BooleanTrigger prevTrig, nextTrig;
	dsp::SchmittTrigger clockTrig, resetTrig, nextInTrig;
	double sincePulse = 1e9, period = 0.0;
	float bpm = 120.f;
	int pulses = 0;

	Canta() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "CANTA";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		fol.bind(CLOCK_INPUT, follow::CLOCK);
		fol.bind(RESET_INPUT, follow::RESET);
		static const float DEF[NK] = {8, 79, 10, 50, 0, 0, 50, 63, 13, 35, 40, 35, 0, 0, 0, 0};
		for (int k = 0; k < NK; k++)
			configParam<RawQuantity>(K_PARAM + k, 0.f, 100.f, DEF[k], KNOB_NAMES[k])->snapEnabled = true;
		for (int k = 0; k < NV; k++)
			configInput(V_INPUT + k, string::f("%s CV (±5 V: ± half the range)", KNOB_NAMES[k]));
		configParam<RawQuantity>(LEVEL_PARAM, 0.f, 127.f, 100.f, "Level")->snapEnabled = true;
		configParam<RawQuantity>(MIX_PARAM, 0.f, 100.f, 70.f, "Harmony mix")->snapEnabled = true;
		configSwitch(HARM_PARAM, 0.f, 6.f, 0.f, "Harmony (a second voice at an interval)",
		             {"OFF", "-12 (an octave down)", "+3 (minor third)", "+4 (major third)", "+5 (fourth)", "+7 (fifth)",
		              "+12 (an octave up)"});
		configSwitch(LYRICS_PARAM, 0.f, 1.f, 0.f, "Lyrics",
		             {"FOLLOW (each gate the next syllable of the lyrics)", "FIXED (always the SYLLABLE)"});
		std::vector<std::string> morae;
		morae.push_back("-");
		for (int i = 1; i < 73; i++)
			morae.push_back(string::f("%d", i));
		configSwitch(SYL_PARAM, 1.f, 72.f, 39.f, "Syllable (FIXED)", morae);
		configSwitch(LANG_PARAM, 0.f, 3.f, 0.f, "Language",
		             {"JAPANESE (the lyrics as written)", "ANIMAL (babble while the gate is held)",
		              "SCAT (da ba bi do on eighths of the clock)", "VOWELS (the lyrics' vowels only)"});
		configSwitch(CHR_PARAM, 0.f, 4.f, 2.f, "Character (formant size)", {"DEEP", "LOW", "MID", "YOUNG", "TINY"});
		configSwitch(DANCE_PARAM, 0.f, 1.f, 0.f, "Dance (the singer steps to the beat)", {"Off", "On"});
		configButton(PREV_PARAM, "Previous preset");
		configButton(NEXT_PARAM, "Next preset");
		configButton(CALL_PARAM, "Call (sings: V/OCT, or G4 without a cable)");
		configInput(VOCT_INPUT, "V/Oct");
		configInput(GATE_INPUT, "Gate (each gate sings the next syllable)");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(CLOCK_INPUT, "Clock: SCAT's eighths (GENRA: 16ths, see the context menu)");
		configInput(RESET_INPUT, "Reset (the lyrics from the start)");
		configInput(SYL_INPUT, "Syllable CV (0..10 V: where in the lyrics the next gate sings; FIXED: which syllable)");
		configInput(NEXT_INPUT, "Next (trigger: the next syllable now, legato)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		paramQuantities[LEVEL_PARAM]->randomizeEnabled = false;
		paramQuantities[LYRICS_PARAM]->randomizeEnabled = false;
		paramQuantities[LANG_PARAM]->randomizeEnabled = false;
		paramQuantities[DANCE_PARAM]->randomizeEnabled = false;
		for (int k = 12; k < NK; k++)
			paramQuantities[K_PARAM + k]->randomizeEnabled = false;
		for (int& s : sent)
			s = INT32_MIN;
		for (auto& h : history)
			h = 0;
	}

	/** Knobs shown as the core writes the value (ms, cents, %). */
	struct RawQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Canta* m = dynamic_cast<Canta*>(module);
			int j = m ? m->coreIndex(paramId) : -1;
			if (m && j >= 0 && m->core.loaded() && m->values[j][0])
				return m->values[j].data();
			return ParamQuantity::getDisplayValueString();
		}
	};

	/** The core's parameter a panel parameter drives (-1: none). */
	int coreIndex(int id) const {
		if (id >= K_PARAM && id < K_PARAM + NK)
			return id - K_PARAM;
		switch (id) {
			case LEVEL_PARAM: return JCA_LEVEL;
			case MIX_PARAM: return JCA_HMIX;
			case HARM_PARAM: return JCA_HINT;
			case LYRICS_PARAM: return JCA_LYRICS;
			case SYL_PARAM: return JCA_FIXED;
			case LANG_PARAM: return JCA_LANG;
			case CHR_PARAM: return JCA_CHR;
			default: return -1;
		}
	}

	// ---------------------------------------------------------------------------------- power, presets, lyrics
	void powerOn() {
		if (!core.load()) {
			status = core.error;
			WARN("CANTA: %s", status.c_str());
			return;
		}
		status.clear();
		core.api->init(random::u32() | 1u);
		for (int& s : sent)
			s = INT32_MIN;
		outBuf.clear();
		src.reset();
		gate = false;
		int n = std::max(1, (int) core.api->npresets());
		preset = ((preset % n) + n) % n;
		if (!fromPatch)
			applyPreset(preset);
		else
			takeLyrics(lyrics);
		shownPreset = preset;
	}

	/** The preset's sound into the knobs (and the core); its lyrics too unless "keep lyrics". */
	void applyPreset(int i) {
		const jca_api* a = core.api;
		if (!a)
			return;
		int n = (int) a->npresets();
		preset = ((i % n) + n) % n;
		a->set(JCA_LEVEL, (int) std::round(params[LEVEL_PARAM].getValue()));
		a->set(JCA_LYRICS, (int) std::round(params[LYRICS_PARAM].getValue()));
		a->set(JCA_FIXED, (int) std::round(params[SYL_PARAM].getValue()));
		a->load_preset((uint32_t) preset);
		for (int k = 0; k < NK; k++)
			params[K_PARAM + k].setValue((float) a->get(k));
		params[MIX_PARAM].setValue((float) a->get(JCA_HMIX));
		params[HARM_PARAM].setValue((float) a->get(JCA_HINT));
		params[LANG_PARAM].setValue((float) a->get(JCA_LANG));
		params[CHR_PARAM].setValue((float) a->get(JCA_CHR));
		for (int& s : sent)
			s = INT32_MIN;
		if (keepLyrics && !lyrics.empty())
			takeLyrics(lyrics);
		else
			takeLyrics(a->preset_lyrics((uint32_t) preset));
		shownPreset = preset;
	}

	/** Lyrics into the core (engine thread). Bad lyrics: the old ones stay, the error is shown. */
	void takeLyrics(const std::string& s) {
		const jca_api* a = core.api;
		if (!a)
			return;
		int e = a->set_lyrics(s.c_str());
		lyricError = e;
		if (e < 0) {
			lyrics = s;
			std::lock_guard<std::mutex> lock(shownMutex);
			shownLyrics = s;
		}
		snapshotLyrics(a);
	}

	void snapshotLyrics(const jca_api* a) {
		int n = std::min((int) a->lyric_count(), MAXU);
		for (int i = 0; i < n; i++) {
			std::snprintf(units[i].t, sizeof units[i].t, "%s", a->lyric_text((uint32_t) i));
			units[i].flags = (uint8_t) a->lyric_flags((uint32_t) i);
		}
		nUnits = n;
	}

	/** From the UI: new lyrics, taken by the engine thread. */
	void requestLyrics(const std::string& s) {
		std::lock_guard<std::mutex> lock(lyricsMutex);
		lyricsReq = s;
		lyricsPending = true;
	}

	std::string lyricsForUi() {
		std::lock_guard<std::mutex> lock(shownMutex);
		return shownLyrics;
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
		keepLyrics = false;
		legatoNext = false;
		presetReq = 0;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "preset", json_integer(preset));
		json_object_set_new(r, "clockMode", json_integer(clockMode));
		json_object_set_new(r, "keepLyrics", json_boolean(keepLyrics));
		json_object_set_new(r, "legatoNext", json_boolean(legatoNext));
		json_object_set_new(r, "lyrics", json_string(lyricsForUi().c_str()));
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override {
		fol.fromJson(r);
		if (json_t* j = json_object_get(r, "preset"))
			preset = (int) json_integer_value(j);
		if (json_t* j = json_object_get(r, "clockMode"))
			clockMode = clamp((int) json_integer_value(j), 0, 3);
		if (json_t* j = json_object_get(r, "keepLyrics"))
			keepLyrics = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "legatoNext"))
			legatoNext = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "lyrics")) {
			lyrics = json_string_value(j) ? json_string_value(j) : "";
			{
				std::lock_guard<std::mutex> lock(shownMutex);
				shownLyrics = lyrics;
			}
			requestLyrics(lyrics);
		}
		fromPatch = true;
		shownPreset = preset;
		for (int& s : sent)
			s = INT32_MIN;
	}

	// ---------------------------------------------------------------------------------- the engine
	void push(const jca_api* a, uint32_t j, int v) {
		if (sent[j] != v) {
			a->set(j, v);
			sent[j] = v;
		}
	}

	void processKnobs(const jca_api* a) {
		for (int k = 0; k < NK; k++) {
			float x = params[K_PARAM + k].getValue();
			if (k < NV && inputs[V_INPUT + k].isConnected())
				x += inputs[V_INPUT + k].getVoltage() * 10.f;
			push(a, (uint32_t) k, clamp((int) std::round(x), 0, 100));
		}
		for (int id : {LEVEL_PARAM, MIX_PARAM, HARM_PARAM, LYRICS_PARAM, LANG_PARAM, CHR_PARAM})
			push(a, (uint32_t) coreIndex(id), (int) std::round(params[id].getValue()));
		int syl = (int) std::round(params[SYL_PARAM].getValue());
		if (params[LYRICS_PARAM].getValue() > 0.5f && inputs[SYL_INPUT].isConnected())
			syl = 1 + clamp((int) (inputs[SYL_INPUT].getVoltage() / 10.f * 72.f), 0, 71);
		push(a, JCA_FIXED, syl);
	}

	/** One voice: the first channel of V/OCT and GATE, or CALL. A new gate sings the next syllable; a new pitch under
	 * a held gate glides (or, "legato syllables", sings the next syllable). */
	void processNotes(const jca_api* a) {
		bool on = false, keyed = false;
		int nn = clamp((int) std::round(inputs[VOCT_INPUT].getVoltage() * 12.f) + 60, 0, 127);
		if (inputs[GATE_INPUT].isConnected()) {
			float g = inputs[GATE_INPUT].getVoltage();
			on = keyed = gateIn ? g > 0.5f : g >= 1.f;
		}
		gateIn = keyed;
		bool call = params[CALL_PARAM].getValue() > 0.5f;
		lights[CALL_LIGHT].setBrightness(call ? 1.f : 0.f);
		if (!keyed && call) {
			on = true;
			if (!inputs[VOCT_INPUT].isConnected())
				nn = CALL_NOTE;
		}
		if (on && !gate) {
			int vel = 100;
			if (keyed && inputs[VEL_INPUT].isConnected())
				vel = clamp((int) std::round(inputs[VEL_INPUT].getVoltage() / 10.f * 127.f), 1, 127);
			if (params[LYRICS_PARAM].getValue() < 0.5f && inputs[SYL_INPUT].isConnected()) {
				int n = (int) a->lyric_count();
				if (n > 0)
					a->lyric_seek((uint32_t) clamp((int) (inputs[SYL_INPUT].getVoltage() / 10.f * n), 0, n - 1));
			}
			a->note_on(nn, vel);
			note = nn;
			if (!clocked)
				beats++;
		} else if (!on && gate) {
			a->note_off();
		} else if (on && nn != note) {
			if (legatoNext) {
				a->glide(nn);
				a->next();
			} else
				a->glide(nn);
			note = nn;
		}
		gate = on;
		if (nextInTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
			a->next();
	}

	/** CLOCK into the tempo (SCAT's eighths, the dance); RESET: the lyrics from the start. */
	void processClock(const jca_api* a, float sampleRate) {
		int ppqn = CLOCK_PPQN[clamp(clockMode, 0, 3)];
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			clockTrig.reset();
			a->lyric_seek(0);
			pulses = 0;
		}
		sincePulse += 1.0;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sincePulse < sampleRate * 4.0) {
				period = period > 0.0 ? period + (sincePulse - period) * 0.5 : sincePulse;
				bpm = clamp((float) (60.0 * sampleRate / (period * ppqn)), 20.f, 300.f);
			}
			sincePulse = 0.0;
			if (pulses++ % ppqn == 0)
				beats++;
		}
		clocked = period > 0.0 && sincePulse < std::min(period * 2.5, (double) sampleRate * 4.0);
	}

	void renderBlock(const jca_api* a, float sampleRate) {
		processKnobs(a);
		int devRate = (int) a->sample_rate();
		a->set_bpm((uint32_t) std::round(bpm));
		a->render(BLOCK, devL, devR);
		dsp::Frame<2> in[BLOCK];
		for (int i = 0; i < BLOCK; i++) {
			in[i].samples[0] = devL[i];
			in[i].samples[1] = devR[i];
		}
		src.process(in, BLOCK, devRate, (int) sampleRate, outBuf);
		uint32_t m = a->sung_mora();
		if (m != lastMora) {
			lastMora = m;
			uint32_t h = histN;
			history[h % 8] = (int) m;
			histN = h + 1;
		}
		framesSinceUi += BLOCK;
		if (framesSinceUi >= (uint32_t) devRate / 60) {
			framesSinceUi = 0;
			for (uint32_t j = 0; j < JCA_NP; j++)
				a->format(j, a->get(j), values[j].data(), (uint32_t) values[j].size());
			mouth = (int) a->mouth();
			envLevel = (int) a->level();
			sung = a->lyric_sung();
			nextPos = (int) a->lyric_pos();
			sungMora = (int) m;
			gateShown = a->gated() != 0;
			shownBpm = bpm;
		}
	}

	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		const jca_api* a = core.api;
		if (!a) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		if (lyricsPending) {
			std::string s;
			{
				std::lock_guard<std::mutex> lock(lyricsMutex);
				s = lyricsReq;
				lyricsPending = false;
			}
			takeLyrics(s);
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
		processClock(a, args.sampleRate);
		processNotes(a);
		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(a, args.sampleRate);
		dsp::Frame<2> f = outBuf.empty() ? dsp::Frame<2>{} : outBuf.shift();
		float l = f.samples[0] * 8.f, r = f.samples[1] * 8.f;
		if (outputs[R_OUTPUT].isConnected() || pulled(args.frame)) {
			outputs[L_OUTPUT].setVoltage(l);
			outputs[R_OUTPUT].setVoltage(r);
		} else {
			outputs[L_OUTPUT].setVoltage(0.5f * (l + r));
			outputs[R_OUTPUT].setVoltage(0.f);
		}
	}

	// ---- presets for the GENRA it follows
	int hostPresetCount() override { return core.loaded() ? (int) core.api->npresets() : 0; }
	void hostPresetInfo(int i, char* name, int nameLen, char* style, int styleLen) override {
		follow::putStr(name, nameLen, core.loaded() ? core.api->preset_name((uint32_t) i) : "");
		follow::putStr(style, styleLen, "");
	}
	int hostPresetCurrent() override { return shownPreset; }
	void hostPresetLoad(int i) override { presetReq = i; }
};

// ------------------------------------------------------------------------------------------------ the panel (GENRA look)
namespace L = klayout::canta;
static const NVGcolor ACCENT = nvgRGB(CANTA_RGB);

static std::string upper(const std::string& s) {
	std::string o = s;
	for (char& c : o)
		if (c >= 'a' && c <= 'z')
			c = (char) (c - 32);
	return o;
}

/** The singer of the language (own pixel art) with her mouth per vowel, blinking, dancing to the beat; right of her
 * the syllable wheel: the lyrics around, the one sung now on top and large in the middle. */
struct ScreenWidget : Widget {
	Canta* module = nullptr;
	double blinkAt = 3.0, lastT = 0.0;
	float bob = 0.f;
	int lastBeats = 0;

	void drawSinger(NVGcontext* vg, int which, float x0, float y0, float k, int mouth, bool blink) {
		const canta_sprites::Singer& s = canta_sprites::SINGERS[which & 3];
		auto color = [&](char c) -> NVGcolor {
			const char* p = std::strchr(s.keys, c);
			uint32_t v = p ? s.colors[p - s.keys] : 0xff00ff;
			return nvgRGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
		};
		int mc = -1;
		for (int r = 0; r < 21; r++) {
			for (int c = 0; c < 16; c++) {
				char ch = s.rows[r][c];
				if (ch == '.')
					continue;
				if (ch == 'M' && mc < 0)
					mc = c;
				NVGcolor col = color(ch);
				float h = k + 0.05f, y = y0 + r * k;
				if (ch == 'E' && blink) {   // closed: skin, and a lash line at the bottom
					nvgBeginPath(vg);
					nvgRect(vg, x0 + c * k, y, k + 0.05f, h);
					nvgFillColor(vg, color('S'));
					nvgFill(vg);
					col = color('E');
					y += k * 0.65f;
					h = k * 0.35f;
				}
				if (ch == 'M')
					col = mouth >= 5 ? color('M') : color('E');
				nvgBeginPath(vg);
				nvgRect(vg, x0 + c * k, y, k + 0.05f, h);
				nvgFillColor(vg, col);
				nvgFill(vg);
			}
		}
		if (mc < 0 || mouth >= 5)
			return;
		// the open mouth: A wide and deep, I wide and flat, U small, E middle, O round
		static const float MW[5] = {2.f, 2.6f, 1.f, 2.f, 1.6f}, MH[5] = {1.3f, 0.5f, 0.8f, 0.9f, 1.3f};
		float cx = x0 + (mc + 1.f) * k, y = y0 + 10.f * k;
		nvgBeginPath(vg);
		nvgRect(vg, cx - MW[mouth] * k / 2, y, MW[mouth] * k, MH[mouth] * k);
		nvgFillColor(vg, color('E'));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, cx - MW[mouth] * k / 2 + 0.2f * k, y + MH[mouth] * k * 0.55f, MW[mouth] * k - 0.4f * k,
		        MH[mouth] * k * 0.45f);
		nvgFillColor(vg, nvgRGB(0xff, 0x8f, 0xa3));
		nvgFill(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		if (module && !module->status.empty()) {
			jiant::drawStatus(vg, module->status, box.size.x);
			return;
		}
		double t = system::getTime();
		float dt = lastT > 0.0 ? (float) std::min(0.1, t - lastT) : 0.f;
		lastT = t;
		int lang = module ? clamp((int) std::round(module->params[Canta::LANG_PARAM].getValue()), 0, 3) : 0;
		int mouth = module ? (int) module->mouth : 0;
		bool blink = false;
		if (t > blinkAt) {
			blink = true;
			if (t > blinkAt + 0.12)
				blinkAt = t + 2.5 + random::uniform() * 3.0;
		}
		bool dance = module && module->params[Canta::DANCE_PARAM].getValue() > 0.5f;
		int beatsNow = module ? (int) module->beats : 0;
		if (beatsNow != lastBeats) {
			lastBeats = beatsNow;
			bob = 1.f;
		}
		bob = std::max(0.f, bob - dt * 5.f);
		float k = box.size.y / 23.f, sx = 3.f, sy = (box.size.y - 21.f * k) / 2;
		if (dance) {
			sx += (lastBeats & 1) ? k * 0.5f : -k * 0.5f;
			sy -= bob * k;
		}
		drawSinger(vg, lang, sx, sy, k, mouth, blink);
		if (dance && bob > 0.2f) {   // a little note over her head on the beat
			NVGcolor c = ACCENT;
			c.a = bob;
			jiant::text(vg, jiant::boldFont(), sx + 16.f * k, sy + 3.f * k, "\xe2\x99\xaa", 8.f, c, NVG_ALIGN_CENTER, 0.f);
		}

		// the syllable sung now, right of the singer
		float cx = 3.f + 16.f * k + (box.size.x - 3.f - 16.f * k) / 2, cy = box.size.y / 2;
		auto bold = jiant::boldFont();
		std::vector<std::string> around;
		std::string centre;
		bool sounding = module && (module->gateShown || module->envLevel > 20);
		bool fixed = module && module->params[Canta::LYRICS_PARAM].getValue() > 0.5f;
		const jca_api* a = module && module->core.loaded() ? module->core.api : nullptr;
		if (!module) {
			around = {"ka", "ze", "ni", "no", "ru", "so", "ra", "e"};
			centre = "ka";
		} else if (a && (lang == 1 || lang == 2)) {
			uint32_t h = module->histN;
			for (int i = 0; i < 8; i++)
				around.push_back(h > (uint32_t) i ? a->mora_name((uint32_t) (int) module->history[(h - 1 - i) % 8]) : "");
			centre = a->mora_name((uint32_t) (int) module->sungMora);
		} else if (a && fixed) {
			centre = a->mora_name((uint32_t) std::round(module->params[Canta::SYL_PARAM].getValue()));
			for (int i = 0; i < 8; i++)
				around.push_back(centre);
		} else if (a) {
			int n = module->nUnits, s = module->sung;
			int start = sounding && s >= 0 ? s : module->nextPos.load();
			for (int i = 0; i < 8 && n > 0; i++) {
				const Canta::Unit& u = module->units[(start + i) % n];
				around.push_back(u.flags & JCA_REST ? "\xc2\xb7" : u.t);
			}
			if (n > 0)
				centre = module->units[start % n].t;
			if (centre == "_")
				centre = a->mora_name((uint32_t) (int) module->sungMora);
		}
		NVGcolor cc = jiant::WHITE;
		cc.a = sounding ? 1.f : 0.35f;
		jiant::text(vg, bold, cx, cy + 4.f, upper(centre), 11.f, cc, NVG_ALIGN_CENTER, 0.4f);
	}

	static constexpr float M_PI_2_F = 1.5707963f;
};

/** Right of the singer: the preset, language · character · harmony · tempo. */
struct InfoWidget : Widget {
	Canta* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x;
		const jca_api* a = module && module->core.loaded() ? module->core.api : nullptr;
		int p = module ? (int) module->shownPreset : 0;
		float h = box.size.y;
		jiant::text(vg, bold, 0, 7.f, string::f("CHARACTER %d / %u", p + 1, a ? a->npresets() : 12u), 5.2f, jiant::DIM,
		            NVG_ALIGN_LEFT, 1.f);
		jiant::text(vg, bold, 0, h * 0.62f, a ? a->preset_name((uint32_t) p) : "KIKO", 12.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.6f);
		static const char* LANGS[4] = {"JAPANESE", "ANIMAL", "SCAT", "VOWELS"};
		int lang = module ? clamp((int) std::round(module->params[Canta::LANG_PARAM].getValue()), 0, 3) : 0;
		int chr = module ? clamp((int) std::round(module->params[Canta::CHR_PARAM].getValue()), 0, 4) : 2;
		int harm = module ? clamp((int) std::round(module->params[Canta::HARM_PARAM].getValue()), 0, 6) : 0;
		std::string s = string::f("%s \xc2\xb7 %s", LANGS[lang], CHR_NAMES[chr]);
		if (harm)
			s += string::f(" \xc2\xb7 HARMONY %s", HARM_NAMES[harm]);
		if (module && lang == 2)
			s += string::f(" \xc2\xb7 %d BPM", (int) std::round((float) module->shownBpm));
		if (module && module->params[Canta::DANCE_PARAM].getValue() > 0.5f)
			s += " \xc2\xb7 DANCE";
		jiant::text(vg, bold, 0, h - 1.f, s, 5.2f, ACCENT, NVG_ALIGN_LEFT, 0.8f);
		(void) mono;
		(void) w;
	}
};

/** The lyrics in a field: the syllable sung now lit; a click opens the editor. */
struct LyricsLine : OpaqueWidget {
	Canta* module = nullptr;

	struct Editor : ui::TextField {
		Canta* module = nullptr;
		void apply() {
			if (module)
				module->requestLyrics(text);
			if (ui::MenuOverlay* o = getAncestorOfType<ui::MenuOverlay>())
				o->requestDelete();
		}
		void onSelectKey(const SelectKeyEvent& e) override {
			if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
				apply();
				e.consume(this);
				return;
			}
			ui::TextField::onSelectKey(e);
		}
	};

	static void openEditor(Canta* m) {
		if (!m)
			return;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("Lyrics (Enter takes them)"));
		menu->addChild(createMenuLabel("ka-ra-o-ke: syllables by -, words by spaces"));
		menu->addChild(createMenuLabel("_ holds the vowel, / a rest, | the end of a phrase, ! a new onset"));
		Editor* f = new Editor;
		f->module = m;
		f->box.size.x = 320.f;
		f->text = m->lyricsForUi();
		f->selectAll();
		menu->addChild(f);
		menu->addChild(createMenuItem("Take them", "Enter", [=]() { m->requestLyrics(f->text); }));
		APP->event->setSelectedWidget(f);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			openEditor(module);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		jiant::roundRect(vg, 0, 0, box.size.x, box.size.y, 2.2f, jiant::FIELD);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.4f, 0.4f, box.size.x - 0.8f, box.size.y - 0.8f, 2.2f);
		nvgStrokeColor(vg, jiant::EDGE);
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);
		auto mono = jiant::monoFont();
		bool fixed = module && module->params[Canta::LYRICS_PARAM].getValue() > 0.5f;
		int lang = module ? (int) std::round(module->params[Canta::LANG_PARAM].getValue()) : 0;
		int err = module ? (int) module->lyricError : -1;
		std::string head = "LYRICS \xc2\xb7 CLICK TO EDIT";
		if (err >= 0)
			head = string::f("LYRICS \xc2\xb7 NOT TAKEN: SEE CHARACTER %d", err + 1);
		else if (fixed)
			head = "LYRICS \xc2\xb7 FIXED: THE SYLLABLE ONLY";
		else if (lang == 1 || lang == 2)
			head = "LYRICS \xc2\xb7 NOT SUNG IN ANIMAL / SCAT";
		jiant::text(vg, mono, 4, 10.5f, head, 5.2f, err >= 0 ? jiant::ERR : jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		if (!mono || mono->handle < 0)
			return;
		// the units, from a little before the one sung now so it is in view
		std::vector<std::pair<std::string, int>> tok;   // text, unit index (-1: a separator)
		int n = 0, cur = -1;
		if (module) {
			n = module->nUnits;
			cur = module->gateShown || module->envLevel > 20 ? (int) module->sung : -1;
			if (cur < 0)
				cur = module->nextPos;
			for (int i = 0; i < n; i++) {
				const Canta::Unit& u = module->units[i];
				std::string t = (u.flags & JCA_REST) ? ((u.flags & JCA_PHRASE_END) ? "|" : "/") : u.t;
				tok.push_back({t, i});
				bool wordEnd = u.flags & JCA_WORD_END;
				tok.push_back({wordEnd ? " " : "-", -1});
			}
		} else {
			for (const char* t : {"ka", " ", "ze", " ", "ni", " ", "no", "-", "ru", " ", "|", " ", "so", "-", "ra"})
				tok.push_back({t, -1});
		}
		nvgFontFaceId(vg, mono->handle);
		nvgFontSize(vg, 7.5f);
		nvgTextLetterSpacing(vg, 0.f);
		float adv = nvgTextBounds(vg, 0, 0, "m", nullptr, nullptr);
		// scroll: the current unit at most two thirds in
		float x = 4.f, curX = 0.f;
		for (auto& tk : tok) {
			if (tk.second == cur)
				curX = x;
			x += adv * tk.first.size();
		}
		float off = std::max(0.f, curX - (box.size.x * 0.66f));
		nvgScissor(vg, 2, 0, box.size.x - 4, box.size.y);
		x = 4.f - off;
		for (auto& tk : tok) {
			bool on = tk.second == cur && tk.second >= 0 && !fixed && lang != 1 && lang != 2;
			jiant::text(vg, mono, x, box.size.y * 0.5f + 12.f, tk.first, 7.5f, on ? ACCENT : (tk.second < 0 && tk.first != " " ? jiant::DIM : jiant::TXT),
			            NVG_ALIGN_LEFT, 0.f);
			x += adv * tk.first.size();
		}
		nvgResetScissor(vg);
	}
};

/** DANCE: the fifth lamp of the style row, lit while on; a click toggles it. */
struct DanceLamp : app::ParamWidget {
	void place(const float* p, int index) {
		box.pos = Vec(p[0] + p[2] * index - p[2] / 2, p[1] - 8);
		box.size = Vec(p[2], 16);
	}
	bool on() {
		engine::ParamQuantity* pq = getParamQuantity();
		return pq && pq->getValue() > 0.5f;
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (engine::ParamQuantity* pq = getParamQuantity())
				pq->setValue(on() ? 0.f : 1.f);
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, box.size.x / 2, box.size.y / 2, 3.6f);
		nvgFillColor(args.vg, on() ? ACCENT : nvgRGB(0x3d, 0x3f, 0x48));
		nvgFill(args.vg);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && on()) {
			float cx = box.size.x / 2, cy = box.size.y / 2;
			NVGcolor glow = ACCENT, none = ACCENT;
			glow.a = 0.35f;
			none.a = 0.f;
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cx, cy, 8.f);
			nvgFillPaint(args.vg, nvgRadialGradient(args.vg, cx, cy, 2.f, 8.f, glow, none));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cx, cy, 3.6f);
			nvgFillColor(args.vg, ACCENT);
			nvgFill(args.vg);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

static const char* const MORA_FALLBACK = "ra";

struct CantaWidget : ModuleWidget {
	CantaWidget(Canta* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Canta.svg")));
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
		// the lyrics: right in the display
		float iw = L::INFO[2] * 0.48f;
		LyricsLine* line = new LyricsLine;
		line->module = module;
		line->box = Rect(L::INFO[0] + L::INFO[2] - iw, L::INFO[1], iw, L::INFO[3]);
		addChild(line);

		genra_look::PickField* pf = createWidget<genra_look::PickField>(Vec(L::PRESETF[0], L::PRESETF[1]));
		pf->box.size = Vec(L::PRESETF[2], L::PRESETF[3]);
		pf->accent = ACCENT;
		pf->count = [=]() { return module && module->core.loaded() ? (int) module->core.api->npresets() : 1; };
		pf->name = [=](int i) -> std::string {
			return module && module->core.loaded() ? module->core.api->preset_name((uint32_t) i) : "KIKO SONG";
		};
		pf->current = [=]() { return module ? (int) module->shownPreset : 0; };
		pf->choose = [=](int i) {
			if (module)
				module->presetReq = i;
		};
		addChild(pf);

		addParam(createLightParamCentered<VCVLightBezel<jiant::AccentLight<CANTA_RGB>>>(
		    jiant::at(L::CALL), module, Canta::CALL_PARAM, Canta::CALL_LIGHT));
		genra_look::ParamField* lyr = createParam<genra_look::ParamField>(Vec(L::LYRF[0], L::LYRF[1]), module, Canta::LYRICS_PARAM);
		lyr->box.size = Vec(L::LYRF[2], L::LYRF[3]);
		lyr->count = 2;
		lyr->nameOf = [](int i) -> std::string { return i ? "FIXED" : "FOLLOW"; };
		lyr->infoOf = [](int i) -> std::string { return i ? "the SYLLABLE only" : "the next syllable each gate"; };
		lyr->title = "Lyrics";
		lyr->undo = "change lyrics mode";
		addParam(lyr);
		genra_look::ParamField* syl = createParam<genra_look::ParamField>(Vec(L::SYLF[0], L::SYLF[1]), module, Canta::SYL_PARAM);
		syl->box.size = Vec(L::SYLF[2], L::SYLF[3]);
		syl->count = 72;
		syl->nameOf = [=](int i) -> std::string {
			if (module && module->core.loaded())
				return module->core.api->mora_name((uint32_t) clamp(i + 1, 1, 72));
			return MORA_FALLBACK;
		};
		syl->title = "Syllable (FIXED)";
		syl->undo = "change syllable";
		addParam(syl);
		genra_look::ParamField* harm = createParam<genra_look::ParamField>(Vec(L::HARMF[0], L::HARMF[1]), module, Canta::HARM_PARAM);
		harm->box.size = Vec(L::HARMF[2], L::HARMF[3]);
		harm->count = 7;
		harm->nameOf = [](int i) -> std::string { return HARM_NAMES[clamp(i, 0, 6)]; };
		harm->title = "Harmony";
		harm->undo = "change harmony";
		addParam(harm);
		jiant::LampRow* lang = createParam<jiant::LampRow>(Vec(0, 0), module, Canta::LANG_PARAM);
		lang->place(L::LANG, 4);
		lang->accent = ACCENT;
		addParam(lang);
		DanceLamp* dance = createParam<DanceLamp>(Vec(0, 0), module, Canta::DANCE_PARAM);
		dance->place(L::LANG, 4);
		addParam(dance);
		jiant::LampRow* chr = createParam<jiant::LampRow>(Vec(0, 0), module, Canta::CHR_PARAM);
		chr->place(L::CHR, 5);
		chr->accent = ACCENT;
		addParam(chr);
		addParam(createParamCentered<GKnob>(jiant::at(L::MIX), module, Canta::MIX_PARAM));
		addParam(createParamCentered<GKnob>(jiant::at(L::LEVEL), module, Canta::LEVEL_PARAM));

		for (int k = 0; k < NV; k++) {
			Vec c = jiant::at(L::V[k]);
			if (k == 6)
				addParam(createParamCentered<GLargeKnob>(c, module, Canta::K_PARAM + k));
			else if (k < 3)
				addParam(createParamCentered<GKnob>(c, module, Canta::K_PARAM + k));
			else
				addParam(createParamCentered<GSmallKnob>(c, module, Canta::K_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::V_IN[k]), module, Canta::V_INPUT + k));
		}
		const float* small[8] = {L::ATK, L::REL, L::SCOOP, L::VIB, L::CHOR, L::DLY, L::REV, L::DRV};
		for (int i = 0; i < 8; i++)
			addParam(createParamCentered<GSmallKnob>(jiant::at(small[i]), module, Canta::K_PARAM + NV + i));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Canta::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Canta::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Canta::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::CLOCK), module, Canta::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::RESET), module, Canta::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::SYL_IN), module, Canta::SYL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::NEXT_IN), module, Canta::NEXT_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Canta::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Canta::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		Canta* m = getModule<Canta>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Edit the lyrics…", "", [=]() { LyricsLine::openEditor(m); }));
		menu->addChild(createBoolPtrMenuItem("Presets keep the lyrics", "", &m->keepLyrics));
		menu->addChild(createBoolPtrMenuItem("A new pitch under a held gate sings the next syllable", "", &m->legatoNext));
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
		menu->addChild(createMenuLabel("Voice: MIKU-FM's fvoice (MichaelAPerry/MIKU-FM1, GPL-3.0); singers: own art"));
	}
};

}  // namespace genra_canta

Model* modelCanta = createModel<genra_canta::Canta, genra_canta::CantaWidget>("Canta");
