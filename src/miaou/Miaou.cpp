// SPDX-License-Identifier: GPL-3.0-only
// MIAOU: the toy keyboard's five sounds (piano, bells, organ, banjo, the meow) and its little speaker from MIAOU
// (github.com/pjac1810/miaou, GPL-3.0-only) as a VCV Rack voice. The engine is MIAOU's own (miaoucore/, its voices in
// a struct, so every instance has its own), at 44.1 kHz, resampled to Rack's rate; this file is the panel, the jacks
// and the glue: poly V/OCT + GATE + VEL into the voices, PAW (button or gate) makes every key meow, TOY's seven knobs
// scale what the firmware held fixed, and a chorus, a delay and a reverb (GENRA FX's) come after. No beats, songs,
// recorder, arpeggiator or sequencer: GENRA plays it.
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/jiant/JiantUI.hpp"
#include "../../genra/genrafx/GenraFxDsp.hpp"
#include "../../miaoucore/miaoucore.h"
#include "MiaouLayout.hpp"
#include <atomic>
#include <array>

namespace genra_miaou {

#define MIAOU_RGB 0xe5, 0x8c, 0xf0   // keep in step with tools/miaou_panel.py

static const int NT = MC_NPARAMS;    // TOY's knobs
static const char* SOUND_NAMES[MC_SOUNDS] = {"PIANO", "BELLS", "ORGAN", "BANJO", "MEOW"};
static const char* TOY_NAMES[NT] = {"DECAY", "TONE", "MEOW", "SWOOP", "BRTH", "KITTEN", "SPEAKER"};
static const char* TOY_LONG[NT] = {"Decay (the sounds' ring and release, x0.25 .. x4)", "Tone (brightness)",
                                   "Meow (its length)", "Swoop (the meow's rise and fall)", "Breath (in the meow)",
                                   "Kitten (the meow's formants: up is a younger cat)",
                                   "Speaker (the toy's little speaker: no bass, a soft top, eight bits)"};
static const float TOY_DEF[NT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.4f};
static const int CALL_SRC = 16;      // the cat clicked: a meow of its own

static std::string toyValue(int k, float x) {
	switch (k) {
		case MC_P_DECAY: return string::f("%d%%", (int) std::round(100.f * std::pow(2.f, (x - 0.5f) * 4.f)));
		case MC_P_MEOW: return string::f("%.2fs", 0.55f * std::pow(2.f, (x - 0.5f) * 3.f));
		case MC_P_KITTEN: {
			int st = (int) std::round(3.f + (x - 0.5f) * 18.f);
			return string::f("%+dst", st);
		}
		default: return string::f("%d%%", (int) std::round(x * 100.f));
	}
}

// ------------------------------------------------------------------------------------------------ a small chorus
struct Chorus {
	std::vector<float> buf;
	int w = 0, mask = 0;
	float ph = 0.f, sr = 48000.f;
	void setSampleRate(float r) {
		sr = r;
		int n = 1;
		while (n < (int) (0.05f * r))
			n <<= 1;
		buf.assign(n, 0.f);
		mask = n - 1;
		w = 0;
	}
	float tap(float d) const {
		float p = (float) w - d;
		int i = (int) std::floor(p);
		float f = p - i;
		float a = buf[i & mask], b = buf[(i + 1) & mask];
		return a + (b - a) * f;
	}
	/** two voices, their delays 7..19 ms, swaying at 0.5 Hz in opposite phase: wet left, right */
	void process(float in, float& l, float& r) {
		buf[w & mask] = in;
		ph += 0.5f / sr;
		if (ph >= 1.f)
			ph -= 1.f;
		float s = std::sin(2.f * gfx::PI * ph);
		l = tap((0.013f + 0.006f * s) * sr);
		r = tap((0.013f - 0.006f * s) * sr);
		w++;
	}
};

// ------------------------------------------------------------------------------------------------ the module
struct Miaou : Module, follow::Host {
	enum ParamId {
		PAW_PARAM, SOUND_PARAM, LEVEL_PARAM,
		ENUMS(T_PARAM, NT),
		CHOR_PARAM, DLY_PARAM, REV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(T_INPUT, NT),
		VOCT_INPUT, GATE_INPUT, VEL_INPUT, PAW_INPUT, SOUND_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { PAW_LIGHT, LIGHTS_LEN };
	enum { BLOCK = 32 };

	mc_engine* eng = nullptr;

	// for the UI thread
	std::atomic<int> shownMouth{0}, shownLook{0}, shownSound{0}, shownNote{-1};
	std::atomic<bool> shownPaw{false};
	std::array<std::atomic<uint32_t>, 4> shownLit{};
	std::atomic<bool> callReq{false};

	// engine thread
	bool gate[16] = {};
	float pitch[16] = {};
	int lastNote = -1;
	float dev[BLOCK];
	jiant::Resampler src;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 2048> outBuf;
	uint32_t framesSinceUi = 0;
	int callLeft = 0;
	Chorus chorus;
	gfx::Delay delay;
	gfx::Reverb reverb;
	float fxRate = 0.f;

	Miaou() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		srcName = "MIAOU";
		audioL = L_OUTPUT;
		audioR = R_OUTPUT;
		fol.bind(VOCT_INPUT, follow::VOCT);
		fol.bind(GATE_INPUT, follow::GATE);
		fol.bind(VEL_INPUT, follow::VEL);
		configSwitch(PAW_PARAM, 0.f, 1.f, 0.f, "Paw (every key meows)", {"Off", "On"});
		configSwitch(SOUND_PARAM, 0.f, 4.f, 0.f, "Sound", {"PIANO", "BELLS", "ORGAN", "BANJO", "MEOW"});
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);
		for (int k = 0; k < NT; k++) {
			configParam<ToyQuantity>(T_PARAM + k, 0.f, 1.f, TOY_DEF[k], TOY_LONG[k]);
			configInput(T_INPUT + k, string::f("%s CV (±5 V: ± half the range)", TOY_NAMES[k]));
		}
		configParam(CHOR_PARAM, 0.f, 1.f, 0.f, "Chorus", "%", 0.f, 100.f);
		configParam(DLY_PARAM, 0.f, 1.f, 0.f, "Delay send", "%", 0.f, 100.f);
		configParam(REV_PARAM, 0.f, 1.f, 0.f, "Reverb send", "%", 0.f, 100.f);
		configInput(VOCT_INPUT, "V/Oct (polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic)");
		configInput(VEL_INPUT, "Velocity (0..10 V)");
		configInput(PAW_INPUT, "Paw (gate: every key meows while high)");
		configInput(SOUND_INPUT, "Sound CV (2 V a sound, added to the switch)");
		configOutput(L_OUTPUT, "Left (mono when Right is not patched)");
		configOutput(R_OUTPUT, "Right");
		paramQuantities[PAW_PARAM]->randomizeEnabled = false;
		paramQuantities[LEVEL_PARAM]->randomizeEnabled = false;
		for (auto& w : shownLit)
			w = 0;
		eng = mc_new(random::u32() | 1u);
	}
	~Miaou() { mc_free(eng); }

	struct ToyQuantity : ParamQuantity {
		std::string getDisplayValueString() override { return toyValue(paramId - T_PARAM, getValue()); }
	};

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		allOff();
	}
	void onRemove(const RemoveEvent& e) override { follow::gone(this); }
	void onPortChange(const PortChangeEvent& e) override { fol.portChange(e); }
	json_t* dataToJson() override {
		json_t* r = json_object();
		fol.toJson(r);
		return r;
	}
	void dataFromJson(json_t* r) override { fol.fromJson(r); }

	void allOff() {
		if (eng)
			mc_all_off(eng);
		for (bool& g : gate)
			g = false;
		callLeft = 0;
	}

	void setupFx(float sr) {
		fxRate = sr;
		chorus.setSampleRate(sr);
		gfx::DelayParams dp;
		dp.sync = false;
		dp.ms = 330.f;
		dp.feedback = 0.38f;
		dp.tone = -0.25f;
		dp.pingpong = true;
		delay.P = dp;
		delay.setSampleRate(sr);
		gfx::ReverbParams rp;
		rp.type = gfx::ROOM;
		rp.size = 0.45f;
		rp.decay = 1.6f;
		rp.damp = 0.45f;
		rp.predelayMs = 8.f;
		reverb.P = rp;
		reverb.setSampleRate(sr);
	}

	int soundNow() {
		float s = params[SOUND_PARAM].getValue();
		if (inputs[SOUND_INPUT].isConnected())
			s += inputs[SOUND_INPUT].getVoltage() * 0.5f;
		return clamp((int) std::round(s), 0, MC_SOUNDS - 1);
	}
	bool pawNow() {
		return params[PAW_PARAM].getValue() > 0.5f || inputs[PAW_INPUT].getVoltage() >= 1.f;
	}

	/** Poly V/OCT + GATE (+ VEL): a rising gate plays its channel's note, a held one follows V/OCT, a falling one lets go. */
	void processNotes() {
		int sound = pawNow() ? MC_MEOW : soundNow();
		int ch = std::max(inputs[GATE_INPUT].getChannels(), 0);
		ch = std::min(ch, 16);
		for (int c = 0; c < 16; c++) {
			bool on = false;
			if (c < ch) {
				float g = inputs[GATE_INPUT].getVoltage(c);
				on = gate[c] ? g > 0.5f : g >= 1.f;
			}
			float p = 60.f + 12.f * inputs[VOCT_INPUT].getPolyVoltage(c);
			if (on && !gate[c]) {
				int vel = 100;
				if (inputs[VEL_INPUT].isConnected())
					vel = clamp((int) std::round(inputs[VEL_INPUT].getPolyVoltage(c) / 10.f * 127.f), 1, 127);
				mc_note_on(eng, c, sound, p, vel);
				pitch[c] = p;
				lastNote = (int) std::round(p);
			} else if (!on && gate[c]) {
				mc_note_off(eng, c);
			} else if (on && std::fabs(p - pitch[c]) > 1e-4f) {
				mc_pitch(eng, c, p);
				pitch[c] = p;
			}
			gate[c] = on;
		}
		if (callReq.exchange(false)) {
			mc_note_on(eng, CALL_SRC, MC_MEOW, 67.f, 100);
			lastNote = 67;
			callLeft = 4410;   // a click is a short paw on the key: 0.1 s
		}
	}

	void renderBlock(float sampleRate) {
		for (int k = 0; k < NT; k++) {
			float x = params[T_PARAM + k].getValue();
			if (inputs[T_INPUT + k].isConnected())
				x += inputs[T_INPUT + k].getVoltage() / 10.f;
			mc_set(eng, k, clamp(x, 0.f, 1.f));
		}
		mc_render(eng, dev, BLOCK);
		if (callLeft > 0) {
			callLeft -= BLOCK;
			if (callLeft <= 0)
				mc_note_off(eng, CALL_SRC);
		}
		dsp::Frame<2> in[BLOCK];
		for (int i = 0; i < BLOCK; i++)
			in[i].samples[0] = in[i].samples[1] = dev[i];
		src.process(in, BLOCK, MC_RATE, (int) sampleRate, outBuf);
		framesSinceUi += BLOCK;
		if (framesSinceUi >= MC_RATE / 30) {
			framesSinceUi = 0;
			shownMouth = mc_mouth(eng);
			shownLook = mc_look(eng);
			shownSound = pawNow() ? MC_MEOW : soundNow();
			shownPaw = pawNow();
			shownNote = lastNote;
			uint32_t w[4] = {};
			for (int n = 0; n < 128; n++)
				if (mc_lit(eng, n))
					w[n >> 5] |= 1u << (n & 31);
			for (int i = 0; i < 4; i++)
				shownLit[i] = w[i];
		}
	}

	void process(const ProcessArgs& args) override {
		fol.process(this, args.frame);
		if (!eng) {
			outputs[L_OUTPUT].setVoltage(0.f);
			outputs[R_OUTPUT].setVoltage(0.f);
			return;
		}
		if (fxRate != args.sampleRate)
			setupFx(args.sampleRate);
		processNotes();
		lights[PAW_LIGHT].setBrightness(pawNow() ? 1.f : 0.f);
		int guard = 0;
		while (outBuf.empty() && guard++ < 8)
			renderBlock(args.sampleRate);
		dsp::Frame<2> f = outBuf.empty() ? dsp::Frame<2>{} : outBuf.shift();
		float x = f.samples[0] * 6.f;   // the core's -1..1 at about 2-4 V
		// FX: the chorus in the dry path, the delay and the reverb as sends (wet only)
		float chor = params[CHOR_PARAM].getValue(), dly = params[DLY_PARAM].getValue(), rev = params[REV_PARAM].getValue();
		float cl, cr;
		chorus.process(x, cl, cr);
		float l = x + chor * (cl - 0.5f * x), r = x + chor * (cr - 0.5f * x);
		float dl = 0.f, dr = 0.f, rl = 0.f, rr = 0.f;
		delay.process(l * dly, r * dly, dl, dr);      // (always: the echoes and the tail ring on when a send closes)
		reverb.process(l * rev, r * rev, rl, rr);
		float level = params[LEVEL_PARAM].getValue();
		l = gfx::outLimit((l + dl + rl) * level);
		r = gfx::outLimit((r + dr + rr) * level);
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
namespace L = mlayout::miaou;
static const NVGcolor ACCENT = nvgRGB(MIAOU_RGB);

// the cat (16 x 13): L fur, P inner ear, N nose, w whiskers; eyes and mouth drawn on top (keep in step with
// tools/miaou_panel.py: CAT)
static const char* CAT[13] = {"..L..........L..", "..LL........LL..", ".LPLL......LLPL.", ".LLLLLLLLLLLLLL.",
                              "LLLLLLLLLLLLLLLL", "LLLLLLLLLLLLLLLL", "LLLLLLLLLLLLLLLL", "LLLLLLLLLLLLLLLL",
                              "LLLLLLLNNLLLLLLL", "wwLLLLLLLLLLLLww", "LLLLLLLLLLLLLLLL", ".LLLLLLLLLLLLLL.",
                              "..LLLLLLLLLLLL.."};

/** The cat: its eyes follow the last note (low left, high right) and blink now and then, its mouth opens with the meow.
 * A click on it is a paw on a key: a short meow. */
struct CatWidget : Widget {
	Miaou* module = nullptr;
	double nextBlink = 0.0, blinkUntil = 0.0;
	void rect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (module)
				module->callReq = true;
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		double t = system::getTime();
		if (t > nextBlink) {
			blinkUntil = t + 0.14;
			nextBlink = t + 2.5 + 4.0 * random::uniform();
		}
		bool blink = t < blinkUntil;
		int look = module ? (int) module->shownLook : 0;
		int px = look < -2 ? -1 : look > 2 ? 1 : 0;
		int mouth = module ? (int) module->shownMouth : 0;
		const float P = 5.5f;
		float ox = (box.size.x - 16 * P) / 2, oy = (box.size.y - 13 * P) / 2;
		NVGcolor fur = ACCENT, ear = nvgRGB(0xff, 0xc6, 0xf5), nose = nvgRGB(0xff, 0x6f, 0xa8),
		         wh = nvgRGBA(0xf2, 0xf3, 0xf7, 200), dark = nvgRGB(0x5a, 0x23, 0x50), pupil = nvgRGB(0x11, 0x11, 0x11);
		for (int r = 0; r < 13; r++)
			for (int c = 0; c < 16; c++) {
				char ch = CAT[r][c];
				if (ch == '.')
					continue;
				NVGcolor col = ch == 'P' ? ear : ch == 'N' ? nose : ch == 'w' ? wh : fur;
				rect(vg, ox + c * P, oy + r * P, P + 0.15f, P + 0.15f, col);
			}
		for (int ex : {3, 10}) {
			if (blink) {
				rect(vg, ox + ex * P, oy + 6 * P, 3 * P, P * 0.6f, dark);
			} else {
				rect(vg, ox + ex * P, oy + 5 * P, 3 * P, 2 * P, nvgRGB(0xf2, 0xf3, 0xf7));
				rect(vg, ox + (ex + 1 + px) * P, oy + 5 * P, P, 2 * P, pupil);
			}
		}
		if (mouth > 24) {
			float h = mouth > 140 ? 2.f : 1.f;
			rect(vg, ox + 6 * P, oy + 9 * P, 4 * P, h * P, dark);
		} else {
			rect(vg, ox + 6.5f * P, oy + 9 * P, P, P * 0.5f, dark);
			rect(vg, ox + 8.5f * P, oy + 9 * P, P, P * 0.5f, dark);
			rect(vg, ox + 7.5f * P, oy + 9.5f * P, P, P * 0.5f, dark);
		}
	}
};

static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static std::string noteName(int n) { return n < 0 ? "-" : string::f("%s%d", NOTE_NAMES[n % 12], n / 12 - 1); }

/** Right of the cat: the sound, PAW, the last note, the speaker, and 27 keys (C3 .. D5) that light as they sound. */
struct InfoWidget : Widget {
	Miaou* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		auto bold = jiant::boldFont(), mono = jiant::monoFont();
		float w = box.size.x;
		int sound = module ? (int) module->shownSound : MC_PIANO;
		bool paw = module && module->shownPaw;
		jiant::text(vg, bold, 0, 20, SOUND_NAMES[clamp(sound, 0, MC_SOUNDS - 1)], 15.f, jiant::WHITE, NVG_ALIGN_LEFT, 0.6f);
		jiant::text(vg, bold, w, 20, paw ? "PAW ON" : "PAW OFF", 6.5f, paw ? ACCENT : jiant::DIM, NVG_ALIGN_RIGHT, 0.8f);
		float sp = module ? module->params[Miaou::T_PARAM + MC_P_SPEAKER].getValue() : 0.4f;
		if (module && module->inputs[Miaou::T_INPUT + MC_P_SPEAKER].isConnected())
			sp = clamp(sp + module->inputs[Miaou::T_INPUT + MC_P_SPEAKER].getVoltage() / 10.f, 0.f, 1.f);
		jiant::text(vg, mono, 0, 33,
		            string::f("POLY \xc2\xb7 %s \xc2\xb7 SPEAKER %d%%", noteName(module ? (int) module->shownNote : -1).c_str(),
		                      (int) std::round(sp * 100.f)),
		            7.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
		uint32_t lit[4] = {};
		if (module)
			for (int i = 0; i < 4; i++)
				lit[i] = module->shownLit[i];
		float kw = w / 27.f;
		for (int i = 0; i < 27; i++) {
			int n = 48 + i;
			bool on = (lit[n >> 5] >> (n & 31)) & 1u;
			bool black = (n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10);
			jiant::roundRect(vg, i * kw, 42, kw - 1.4f, 38, 1.3f, on ? ACCENT : black ? nvgRGB(0x1c, 0x1d, 0x23) : nvgRGB(0x2a, 0x2b, 0x33));
		}
		jiant::text(vg, mono, 0, 93, "C3 \xc2\xb7 \xc2\xb7 \xc2\xb7 D5", 6.f, jiant::DIM, NVG_ALIGN_LEFT, 0.f);
	}
};

struct MiaouWidget : ModuleWidget {
	MiaouWidget(Miaou* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Miaou.svg")));
		follow::addStrip(this, module, {jiant::at(L::VOCT), jiant::at(L::GATE), jiant::at(L::VEL)});
		jiant::addScrews(this);

		CatWidget* cat = new CatWidget;
		cat->module = module;
		cat->box = jiant::area(L::SCREEN);
		addChild(cat);
		InfoWidget* info = new InfoWidget;
		info->module = module;
		info->box = jiant::area(L::INFO);
		addChild(info);

		addParam(createLightParamCentered<VCVLightLatch<jiant::AccentLight<MIAOU_RGB>>>(jiant::at(L::PAW), module,
		                                                                              Miaou::PAW_PARAM, Miaou::PAW_LIGHT));
		jiant::LampRow* snd = createParam<jiant::LampRow>(Vec(0, 0), module, Miaou::SOUND_PARAM);
		snd->place(L::SOUND, MC_SOUNDS);
		snd->accent = ACCENT;
		addParam(snd);
		addParam(createParamCentered<GSmallKnob>(jiant::at(L::LEVEL), module, Miaou::LEVEL_PARAM));

		for (int k = 0; k < NT; k++) {
			addParam(createParamCentered<GSmallKnob>(jiant::at(L::T[k]), module, Miaou::T_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(jiant::at(L::T_IN[k]), module, Miaou::T_INPUT + k));
			jiant::KnobText* t = new jiant::KnobText;
			t->box = jiant::area(L::T_TEXT[k]);
			t->accent = ACCENT;
			t->label = [=]() -> std::string { return TOY_NAMES[k]; };
			t->value = [=]() -> std::string {
				float x = module ? module->params[Miaou::T_PARAM + k].getValue() : TOY_DEF[k];
				if (module && module->inputs[Miaou::T_INPUT + k].isConnected())
					x = clamp(x + module->inputs[Miaou::T_INPUT + k].getVoltage() / 10.f, 0.f, 1.f);
				return toyValue(k, x);
			};
			addChild(t);
		}
		addParam(createParamCentered<GTrimpot>(jiant::at(L::CHOR), module, Miaou::CHOR_PARAM));
		addParam(createParamCentered<GTrimpot>(jiant::at(L::DLY), module, Miaou::DLY_PARAM));
		addParam(createParamCentered<GTrimpot>(jiant::at(L::REV), module, Miaou::REV_PARAM));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VOCT), module, Miaou::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::GATE), module, Miaou::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::VEL), module, Miaou::VEL_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::PAW_IN), module, Miaou::PAW_INPUT));
		addInput(createInputCentered<PJ301MPort>(jiant::at(L::SOUND_IN), module, Miaou::SOUND_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_L), module, Miaou::L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(jiant::at(L::OUT_R), module, Miaou::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		if (follow::Host* h = follow::host(module))
			h->fol.appendMenu(menu, module);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Click the cat for a meow"));
		menu->addChild(createMenuLabel("Sounds: MIAOU by pjac1810 (GPL-3.0)"));
	}
};

}  // namespace genra_miaou

Model* modelMiaou = createModel<genra_miaou::Miaou, genra_miaou::MiaouWidget>("Miaou");
