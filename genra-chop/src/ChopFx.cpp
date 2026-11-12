// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP FX: two effect buses to play live, after the idea of the SP-404, in GENRA CHOP's look (22 HP).
//
// Stereo in, stereo out. Each bus: an effect out of 37 in six groups (the field, the mouse wheel, or the FX jack),
// CTRL 1-3 (the LCD says what they do for the effect), FX ON (tap: on / off, hold: only while held) and an ON gate.
// ROUTE: 1 > 2, 2 > 1 or both side by side. Six quick keys put an effect on the bus last touched and switch it on
// (right-click: which effect). PRESET: whole setups (the buses stay off until you play them). The tempo for the
// echoes, the gater, the looper: 16ths at CLK, else the tempo in the menu. Everything can be MIDI learned.
//
// Prepared for later: where the sound and the clock come from is a choice of its own (today only the cables), saved
// in the patch, with room on the panel for the light segments of a connection without cables.
#include "plugin.hpp"
#include "ChopUI.hpp"
#include "ChopFxLayout.hpp"
#include "MidiLearn.hpp"
#include "fx/FxEngine.hpp"
#include <atomic>

using namespace fx;

static const int QUICKS = 6;
static const int QUICK_DEFAULT[QUICKS] = {FILTDRIVE, RESONATOR, DELAY, ISOLATOR, LOOPER, STOPPER};

/** Names short enough for a quick key. */
static std::string shortName(int f) {
	switch (f) {
		case CASSETTE: return "CASSETTE";
		case WOWFLUT: return "WOW";
		case FILTDRIVE: return "FILTER";
		case SUPERFILT: return "SUPER FLT";
		case RESONATOR: return "RESO";
		case TALK: return "TALK";
		case SATURATOR: return "SATURATE";
		default: break;
	}
	std::string s = FX[f].name;
	if (s.size() > 9) {
		size_t sp = s.find(' ');
		if (sp != std::string::npos && sp >= 3)
			s = s.substr(0, sp);
	}
	return s;
}

struct ChopFx : Module {
	enum ParamId { IN_PARAM, OUT_PARAM, ENUMS(FX_PARAM, 2), ENUMS(C_PARAM, 6), NUM_PARAMS };
	enum InputId { INL_INPUT, INR_INPUT, CLK_INPUT, RST_INPUT, ENUMS(BUS_INPUT, 10), NUM_INPUTS }; // per bus: CV 1-3, ON, FX
	enum OutputId { OUTL_OUTPUT, OUTR_OUTPUT, NUM_OUTPUTS };

	// where the sound and the clock come from (saved): today the cables; later also GENRA CHOP without cables
	enum Source { SRC_CABLES = 0, NUM_SOURCES };
	int audioSource = SRC_CABLES, clockSource = SRC_CABLES;

	Bus bus[2];
	std::atomic<bool> latch[2];      // FX ON (the panel, MIDI)
	std::atomic<bool> busOn[2];      // what plays (for the panel)
	std::atomic<int> fxNow[2];       // the effect playing (after the FX jack)
	std::atomic<bool> retrig[2];
	int route = ROUTE_12;
	int quick[QUICKS];
	int preset = 1;
	float freeBpm = 90.f;
	std::atomic<int> activeBus{0};   // the quick keys and the LCD work on it (UI)
	ml::CtrlIn ctrl[ml::CTRLS];

	// the tempo
	Tempo tempo;
	dsp::SchmittTrigger clkTrig, rstTrig;
	float sinceClk = 0.f;
	int pulse = -1;
	bool clocked = false;
	int block = 0;

	ChopFx() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(IN_PARAM, 0.f, 2.f, 1.f, "Input level", "%", 0.f, 100.f);
		configParam(OUT_PARAM, 0.f, 2.f, 1.f, "Output level", "%", 0.f, 100.f);
		for (int b = 0; b < 2; b++) {
			configParam(FX_PARAM + b, 0.f, (float) (NUM_FX - 1), b == 0 ? (float) VINYL : (float) TAPEECHO,
			            string::f("Bus %d effect", b + 1))->snapEnabled = true;
			for (int k = 0; k < 3; k++)
				configParam<CtrlQuantity>(C_PARAM + b * 3 + k, 0.f, 1.f, 0.5f, string::f("Bus %d CTRL %d", b + 1, k + 1));
			const char* IN[5] = {"CV 1", "CV 2", "CV 3", "ON (gate)", "Effect (0-10 V over the list)"};
			for (int i = 0; i < 5; i++)
				configInput(BUS_INPUT + b * 5 + i, string::f("Bus %d %s", b + 1, IN[i]));
			latch[b] = false;
			busOn[b] = false;
			fxNow[b] = b == 0 ? VINYL : TAPEECHO;
			retrig[b] = false;
		}
		configInput(INL_INPUT, "Left (mono: both)");
		configInput(INR_INPUT, "Right");
		configInput(CLK_INPUT, "Clock (16ths, e.g. GENRA CHOP's CLK out)");
		configInput(RST_INPUT, "Reset (the bar starts)");
		configOutput(OUTL_OUTPUT, "Left");
		configOutput(OUTR_OUTPUT, "Right");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);
		for (int i = 0; i < QUICKS; i++)
			quick[i] = QUICK_DEFAULT[i];
		applyPreset(1);
	}

	/** The CTRL knobs say what they do for the effect on their bus. */
	struct CtrlQuantity : ParamQuantity {
		int bus() const { return (paramId - C_PARAM) / 3; }
		int k() const { return (paramId - C_PARAM) % 3; }
		int fxOf() const {
			ChopFx* m = dynamic_cast<ChopFx*>(module);
			return m ? m->fxNow[bus()].load() : 0;
		}
		std::string getLabel() override {
			return string::f("Bus %d %s", bus() + 1, FX[clamp(fxOf(), 0, NUM_FX - 1)].ctrl[k()]);
		}
		std::string getDisplayValueString() override { return valueText(fxOf(), k(), getValue()); }
		std::string getUnit() override { return ""; }
	};

	void applyPreset(int p) {
		if (p < 0 || p >= NUM_PRESETS)
			return;
		const Preset& P = PRESETS[p];
		for (int b = 0; b < 2; b++) {
			params[FX_PARAM + b].setValue((float) P.fx[b]);
			for (int k = 0; k < 3; k++)
				params[C_PARAM + b * 3 + k].setValue(P.c[b][k]);
			latch[b] = false;
		}
		route = P.route;
		preset = p;
	}
	/** A new effect on a bus from the panel: its controls start where the effect sounds good. */
	void setFx(int b, int f) {
		f = clamp(f, 0, NUM_FX - 1);
		if ((int) std::round(params[FX_PARAM + b].getValue()) == f)
			return;
		params[FX_PARAM + b].setValue((float) f);
		for (int k = 0; k < 3; k++)
			params[C_PARAM + b * 3 + k].setValue(FX[f].def[k]);
	}

	void readAudio(float& L, float& R) {
		switch (audioSource) {
			default: // SRC_CABLES
				L = inputs[INL_INPUT].getVoltage() / 5.f;
				R = inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT].getVoltage() / 5.f : L;
		}
	}
	/** A 16th from the clock (true on its edge); connected: is there a clock at all. */
	bool readClock(bool& connected) {
		switch (clockSource) {
			default:
				connected = inputs[CLK_INPUT].isConnected();
				return connected && clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f);
		}
	}

	float srSeen = 0.f;

	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;
		if (sr != srSeen) {
			srSeen = sr;
			for (Bus& b : bus)
				b.setSampleRate(sr);
		}
		// ---- the tempo: 16ths at CLK (the bar follows the pulses), else the tempo of the menu
		bool conn = false;
		bool edge = readClock(conn);
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			pulse = -1;
			tempo.barPos = 0.0;
		}
		sinceClk += 1.f;
		if (conn) {
			if (edge) {
				if (sinceClk > 8.f && sinceClk < 4.f * sr) {
					float spb = 4.f * sinceClk;
					if (!clocked || std::fabs(spb - tempo.spb) > tempo.spb * 0.004f)
						tempo.spb = spb;
					clocked = true;
				}
				sinceClk = 0.f;
				pulse = (pulse + 1) % 16;
				tempo.barPos = pulse / 16.0;
			}
			else if (clocked) {
				double next = (pulse + 1) / 16.0;
				tempo.barPos = std::min(next - 1e-6, tempo.barPos + 1.0 / (4.0 * tempo.spb));
			}
		}
		else {
			clocked = false;
			tempo.spb = sr * 60.f / clamp(freeBpm, 40.f, 240.f);
			tempo.barPos += 1.0 / (4.0 * tempo.spb);
			if (tempo.barPos >= 1.0)
				tempo.barPos -= 1.0;
		}
		// ---- the controls (every 16 samples)
		if (--block <= 0) {
			block = 16;
			for (int b = 0; b < 2; b++) {
				int f = (int) std::round(params[FX_PARAM + b].getValue());
				Input& fxIn = inputs[BUS_INPUT + b * 5 + 4];
				if (fxIn.isConnected())
					f = (int) std::round(clamp(fxIn.getVoltage(), 0.f, 10.f) / 10.f * (NUM_FX - 1));
				f = clamp(f, 0, NUM_FX - 1);
				bus[b].want = f;
				fxNow[b] = f;
				for (int k = 0; k < 3; k++) {
					float v = params[C_PARAM + b * 3 + k].getValue();
					Input& cv = inputs[BUS_INPUT + b * 5 + k];
					if (cv.isConnected())
						v += cv.getVoltage() / 10.f;
					bus[b].c[k] = clamp(v, 0.f, 1.f);
				}
				Input& gate = inputs[BUS_INPUT + b * 5 + 3];
				bool on = latch[b].load() || (gate.isConnected() && gate.getVoltage() >= 1.f);
				bus[b].on = on;
				busOn[b] = on;
				if (retrig[b].exchange(false))
					bus[b].retrigger = true;
			}
		}
		// ---- the sound
		float L, R;
		readAudio(L, R);
		float gin = params[IN_PARAM].getValue();
		L *= gin, R *= gin;
		if (route == ROUTE_PAR) {
			float l1 = L, r1 = R, l2 = L, r2 = R;
			bus[0].process(l1, r1, tempo);
			bus[1].process(l2, r2, tempo);
			L = l1 + l2 - L;
			R = r1 + r2 - R;
		}
		else {
			int first = route == ROUTE_21 ? 1 : 0;
			bus[first].process(L, R, tempo);
			bus[1 - first].process(L, R, tempo);
		}
		float gout = params[OUT_PARAM].getValue() * 5.f;
		// a soft ceiling: above about 8 V it rounds off instead of clipping hard
		auto ceil = [](float v) { return std::fabs(v) < 8.f ? v : (v > 0.f ? 1.f : -1.f) * (8.f + 3.f * std::tanh((std::fabs(v) - 8.f) / 3.f)); };
		outputs[OUTL_OUTPUT].setVoltage(ceil(L * gout));
		outputs[OUTR_OUTPUT].setVoltage(ceil(R * gout));
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "route", json_integer(route));
		json_object_set_new(r, "preset", json_integer(preset));
		json_object_set_new(r, "freeBpm", json_real(freeBpm));
		json_object_set_new(r, "audioSource", json_integer(audioSource));
		json_object_set_new(r, "clockSource", json_integer(clockSource));
		json_object_set_new(r, "on1", json_boolean(latch[0]));
		json_object_set_new(r, "on2", json_boolean(latch[1]));
		json_object_set_new(r, "activeBus", json_integer(activeBus));
		json_t* q = json_array();
		for (int i = 0; i < QUICKS; i++)
			json_array_append_new(q, json_integer(quick[i]));
		json_object_set_new(r, "quick", q);
		return r;
	}
	void dataFromJson(json_t* r) override {
		auto gi = [&](const char* k, int d) {
			json_t* j = json_object_get(r, k);
			return j ? (int) json_integer_value(j) : d;
		};
		route = clamp(gi("route", ROUTE_12), 0, 2);
		preset = clamp(gi("preset", 1), 0, NUM_PRESETS - 1);
		if (json_t* j = json_object_get(r, "freeBpm"))
			freeBpm = clamp((float) json_number_value(j), 40.f, 240.f);
		audioSource = clamp(gi("audioSource", SRC_CABLES), 0, NUM_SOURCES - 1);
		clockSource = clamp(gi("clockSource", SRC_CABLES), 0, NUM_SOURCES - 1);
		latch[0] = json_is_true(json_object_get(r, "on1"));
		latch[1] = json_is_true(json_object_get(r, "on2"));
		activeBus = clamp(gi("activeBus", 0), 0, 1);
		json_t* q = json_object_get(r, "quick");
		for (int i = 0; i < QUICKS && i < (int) json_array_size(q); i++)
			quick[i] = clamp((int) json_integer_value(json_array_get(q, i)), 0, NUM_FX - 1);
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < QUICKS; i++)
			quick[i] = QUICK_DEFAULT[i];
		applyPreset(1);
		freeBpm = 90.f;
	}
};

// ---------------------------------------------------------------- the panel
struct FxLcd : widget::Widget {
	ChopFx* m = nullptr;
	std::string* msg = nullptr;
	float* msgTime = nullptr;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 1.5f);
		nvgFillColor(vg, C_LCD);
		nvgFill(vg);
		auto f = lcdFont();
		std::string l[2], l3;
		int act = m ? m->activeBus.load() : 0;
		for (int b = 0; b < 2; b++) {
			int fx = m ? m->fxNow[b].load() : (b == 0 ? VINYL : TAPEECHO);
			bool on = m && m->busOn[b].load();
			std::string tag;
			if (m && m->inputs[ChopFx::BUS_INPUT + b * 5 + 4].isConnected())
				tag = " CV";
			l[b] = string::f("%c%d %s%s", b == act ? '>' : ' ', b + 1, FX[fx].name, tag.c_str());
			l[b] += std::string(std::max(1, 21 - (int) l[b].size()), ' ') + (on ? "ON" : "--");
		}
		int fx = m ? m->fxNow[act].load() : VINYL;
		for (int k = 0; k < 3; k++) {
			float v = m ? m->params[ChopFx::C_PARAM + act * 3 + k].getValue() : FX[fx].def[k];
			l3 += string::f("%s %s  ", FX[fx].ctrl[k], valueText(fx, k, v).c_str());
		}
		if (msg && msgTime && *msgTime > 0.f)
			l3 = string::uppercase(*msg);
		label(vg, f, 5.f, 9.5f, l[0], 11.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 5.f, 20.5f, l[1], 11.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgSave(vg);
		nvgIntersectScissor(vg, 0, 0, box.size.x - 3.f, box.size.y);
		label(vg, f, 5.f, 30.5f, l3, 9.5f, nvgRGB(0x3a, 0x4a, 0x2a), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgRestore(vg);
	}
};

/** The effect of a bus: the list in its six groups; the mouse wheel steps through. */
struct FxField : HHField {
	ChopFx* m = nullptr;
	int b = 0;
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && m) {
			ui::Menu* menu = createMenu();
			int cur = m->fxNow[b].load();
			ChopFx* mm = m;
			int bb = b;
			for (int g = 0; g < NUM_GROUPS; g++) {
				menu->addChild(createMenuLabel(GROUP_NAMES[g]));
				for (int i = 0; i < NUM_FX; i++)
					if (FX[i].group == g)
						menu->addChild(createCheckMenuItem(std::string("   ") + FX[i].name, "", [=]() { return cur == i; }, [=]() {
							mm->setFx(bb, i);
							mm->activeBus = bb;
						}));
			}
			e.consume(this);
			return;
		}
		HHField::onButton(e);
	}
};

/** The bus's title bar: a click makes it the bus the quick keys and the LCD work on (a red line under it). */
struct BusSel : widget::OpaqueWidget {
	ChopFx* m = nullptr;
	int b = 0;
	void draw(const DrawArgs& args) override {
		if (!m || m->activeBus.load() != b)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 6.f, box.size.y - 2.2f, box.size.x - 12.f, 1.6f);
		nvgFillColor(args.vg, C_RED);
		nvgFill(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		if (m && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			m->activeBus = b;
			e.consume(this);
		}
	}
};

/** A small light in the corner of a button while its bus plays or rings out. */
struct ChopFxWidget : ModuleWidget, ml::LearnHost {
	ml::Learner learn;
	ml::Learner* learner() override { return &learn; }
	std::string msg;
	float msgTime = 0.f;
	// FX ON and the quick keys: tap = latch, hold = only while held
	double pressAt[2 + QUICKS] = {};
	bool turnedOn[2 + QUICKS] = {};
	int pressBus[2 + QUICKS] = {};

	void say(const std::string& s) {
		msg = s;
		msgTime = 2.5f;
	}

	template <typename T>
	T* at(const float* r) {
		T* w = new T();
		w->box = rectOf(r);
		addChild(w);
		return w;
	}
	HHButton* button(const float* r, const std::string& text, NVGcolor col, std::function<void()> act, const std::string& id,
	                 const std::string& lab) {
		HHButton* b = at<HHButton>(r);
		b->text = text;
		b->col = col;
		b->act = act;
		ml::Target t;
		t.id = id;
		t.label = lab;
		t.btn = b;
		learn.targets.push_back(t);
		b->learning = [=]() { return learn.learning == id; };
		return b;
	}
	void learnField(HHField* f, const std::string& id, const std::string& lab) {
		ml::Target t;
		t.id = id;
		t.label = lab;
		t.field = f;
		learn.targets.push_back(t);
		f->rightMenu = [=](ui::Menu* mn) {
			mn->addChild(createMenuLabel(lab));
			learn.appendTargetMenu(mn, id);
		};
		f->learning = [=]() { return learn.learning == id; };
	}
	template <class K>
	void knob(const float* p, ChopFx* m, int id, const std::string& lab) {
		addParam(createParamCentered<ml::LearnKnob<K>>(Vec(p[0], p[1]), m, id));
		ml::Target t;
		t.id = string::f("p%d", id);
		t.label = lab;
		t.param = id;
		learn.targets.push_back(t);
	}

	/** Press of FX ON (k = bus) or of a quick key (k = 2 + i). */
	void press(ChopFx* m, int k) {
		if (!m)
			return;
		int b = k < 2 ? k : m->activeBus.load();
		pressAt[k] = system::getTime();
		pressBus[k] = b;
		if (k >= 2) {
			int f = m->quick[k - 2];
			bool same = m->fxNow[b].load() == f;
			if (same && m->latch[b]) {
				m->latch[b] = false;
				turnedOn[k] = false;
				return;
			}
			m->setFx(b, f);
			m->latch[b] = true;
			turnedOn[k] = true;
			return;
		}
		m->activeBus = b;
		if (m->latch[b]) {
			m->latch[b] = false;
			turnedOn[k] = false;
		}
		else {
			m->latch[b] = true;
			turnedOn[k] = true;
		}
	}
	void release(ChopFx* m, int k) {
		if (!m)
			return;
		// held longer than a tap: it was momentary
		if (turnedOn[k] && system::getTime() - pressAt[k] > 0.3)
			m->latch[pressBus[k]] = false;
		turnedOn[k] = false;
	}

	ChopFxWidget(ChopFx* m) {
		setModule(m);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ChopFx.svg")));
		learn.kind = "fx";
		learn.module = m;
		learn.ins = m ? m->ctrl : nullptr;
		if (m)
			learn.restorePorts();
		using M = ChopFx;
		knob<HHKnobSmall>(cfx::IN, m, M::IN_PARAM, "IN");
		knob<HHKnobSmall>(cfx::OUT, m, M::OUT_PARAM, "OUT");
		FxLcd* lcd = at<FxLcd>(cfx::LCD);
		lcd->m = m;
		lcd->msg = &msg;
		lcd->msgTime = &msgTime;
		// PRESET, ROUTE
		HHField* pre = at<HHField>(cfx::PRESET);
		pre->value = [=]() { return std::string("PRESET  ") + PRESETS[m ? m->preset : 1].name; };
		pre->count = []() { return NUM_PRESETS; };
		pre->current = [=]() { return m ? m->preset : 1; };
		pre->item = [](int i) { return std::string(PRESETS[i].name); };
		pre->choose = [=](int i) {
			if (m) {
				m->applyPreset(i);
				say(std::string("PRESET ") + PRESETS[i].name + ": PLAY IT WITH FX ON");
			}
		};
		learnField(pre, "preset", "PRESET");
		HHField* rt = at<HHField>(cfx::ROUTE);
		rt->value = [=]() { return std::string("ROUTE  ") + ROUTE_NAMES[m ? m->route : 0]; };
		rt->count = []() { return 3; };
		rt->current = [=]() { return m ? m->route : 0; };
		rt->item = [](int i) {
			const char* T[3] = {"1 > 2 (bus 1 first)", "2 > 1 (bus 2 first)", "1 + 2 (side by side)"};
			return std::string(T[i]);
		};
		rt->choose = [=](int i) {
			if (m)
				m->route = i;
		};
		learnField(rt, "route", "ROUTE");
		// the buses
		const float* SEL[2] = {cfx::SEL0, cfx::SEL1};
		const float* FXF[2] = {cfx::FX0, cfx::FX1};
		const float* CK[2][3] = {{cfx::C0_0, cfx::C0_1, cfx::C0_2}, {cfx::C1_0, cfx::C1_1, cfx::C1_2}};
		const float* ONB[2] = {cfx::ON0, cfx::ON1};
		for (int b = 0; b < 2; b++) {
			BusSel* s = at<BusSel>(SEL[b]);
			s->m = m;
			s->b = b;
			FxField* ff = at<FxField>(FXF[b]);
			ff->m = m;
			ff->b = b;
			ff->value = [=]() { return std::string(FX[m ? m->fxNow[b].load() : (b == 0 ? VINYL : TAPEECHO)].name); };
			ff->count = []() { return NUM_FX; };
			ff->current = [=]() { return m ? m->fxNow[b].load() : 0; };
			ff->item = [](int i) { return std::string(FX[i].name); };
			ff->choose = [=](int i) {
				if (m) {
					m->setFx(b, i);
					m->activeBus = b;
				}
			};
			learnField(ff, string::f("fx%d", b), string::f("BUS %d effect", b + 1));
			for (int k = 0; k < 3; k++)
				knob<HHKnob>(CK[b][k], m, M::C_PARAM + b * 3 + k, string::f("BUS %d CTRL %d", b + 1, k + 1));
			HHButton* on = button(ONB[b], "FX ON", nvgRGB(0x55, 0x53, 0x4f), [=]() { press(m, b); }, string::f("on%d", b),
			                      string::f("BUS %d FX ON", b + 1));
			on->release = [=]() { release(m, b); };
			on->colOf = [=]() { return m && m->busOn[b].load() ? C_RED : nvgRGB(0x55, 0x53, 0x4f); };
			on->rightMenu = [=](ui::Menu* mn) {
				mn->addChild(createMenuLabel(string::f("BUS %d FX ON", b + 1)));
				learn.appendTargetMenu(mn, string::f("on%d", b));
			};
		}
		// the quick keys
		const float* QK[QUICKS] = {cfx::Q0, cfx::Q1, cfx::Q2, cfx::Q3, cfx::Q4, cfx::Q5};
		for (int i = 0; i < QUICKS; i++) {
			std::string id = string::f("quick%d", i);
			HHButton* q = button(QK[i], shortName(QUICK_DEFAULT[i]), nvgRGB(0x55, 0x53, 0x4f), [=]() { press(m, 2 + i); }, id,
			                     string::f("QUICK KEY %d", i + 1));
			q->fontSize = 8.f;
			q->release = [=]() { release(m, 2 + i); };
			q->colOf = [=]() {
				if (!m)
					return nvgRGB(0x55, 0x53, 0x4f);
				int b = m->activeBus.load();
				return (m->fxNow[b].load() == m->quick[i] && m->busOn[b].load()) ? C_RED : nvgRGB(0x55, 0x53, 0x4f);
			};
			quickBtn[i] = q;
			q->rightMenu = [=](ui::Menu* mn) {
				if (!m)
					return;
				mn->addChild(createMenuLabel(string::f("Quick key %d", i + 1)));
				ChopFx* mm = m;
				mn->addChild(createSubmenuItem("Effect", FX[m->quick[i]].name, [=](ui::Menu* sub) {
					for (int g = 0; g < NUM_GROUPS; g++) {
						sub->addChild(createMenuLabel(GROUP_NAMES[g]));
						for (int f = 0; f < NUM_FX; f++)
							if (FX[f].group == g)
								sub->addChild(createCheckMenuItem(std::string("   ") + FX[f].name, "", [=]() { return mm->quick[i] == f; },
								                                  [=]() { mm->quick[i] = f; }));
					}
				}));
				mn->addChild(createMenuItem("Back to the factory key", FX[QUICK_DEFAULT[i]].name,
				                            [=]() { mm->quick[i] = QUICK_DEFAULT[i]; }));
				learn.appendTargetMenu(mn, id);
			};
		}
		// jacks
		auto in = [&](const float* p, int id) { addInput(createInputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		in(cfx::INL_IN, M::INL_INPUT);
		in(cfx::INR_IN, M::INR_INPUT);
		in(cfx::CLK_IN, M::CLK_INPUT);
		in(cfx::RST_IN, M::RST_INPUT);
		const float* BI[2][5] = {{cfx::B0_0_IN, cfx::B0_1_IN, cfx::B0_2_IN, cfx::B0_3_IN, cfx::B0_4_IN},
		                         {cfx::B1_0_IN, cfx::B1_1_IN, cfx::B1_2_IN, cfx::B1_3_IN, cfx::B1_4_IN}};
		for (int b = 0; b < 2; b++)
			for (int i = 0; i < 5; i++)
				in(BI[b][i], M::BUS_INPUT + b * 5 + i);
		addOutput(createOutputCentered<PJ301MPort>(Vec(cfx::OUTL_OUT[0], cfx::OUTL_OUT[1]), m, M::OUTL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(cfx::OUTR_OUT[0], cfx::OUTR_OUT[1]), m, M::OUTR_OUTPUT));
	}
	HHButton* quickBtn[QUICKS] = {};

	void step() override {
		ChopFx* m = dynamic_cast<ChopFx*>(module);
		if (m) {
			learn.step();
			for (int i = 0; i < QUICKS; i++)
				if (quickBtn[i])
					quickBtn[i]->text = shortName(m->quick[i]);
			// a knob turned: its bus is the one the LCD shows
			for (int b = 0; b < 2; b++)
				for (int k = 0; k < 3; k++) {
					float v = m->params[ChopFx::C_PARAM + b * 3 + k].getValue();
					if (v != seen[b][k]) {
						if (seen[b][k] >= 0.f)
							m->activeBus = b;
						seen[b][k] = v;
					}
				}
		}
		if (msgTime > 0.f)
			msgTime -= APP->window->getLastFrameDuration();
		if (!learn.learning.empty()) {
			msg = "MIDI LEARN " + learn.labelOf(learn.learning) + ": MOVE A CONTROL";
			msgTime = 0.2f;
		}
		ModuleWidget::step();
	}
	float seen[2][3] = {{-1.f, -1.f, -1.f}, {-1.f, -1.f, -1.f}};

	void appendContextMenu(Menu* menu) override {
		ChopFx* m = dynamic_cast<ChopFx*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tempo"));
		menu->addChild(createSubmenuItem("Tempo without a clock", string::f("%.0f BPM", m->freeBpm), [=](Menu* sub) {
			const int T[] = {70, 75, 80, 85, 88, 90, 92, 95, 100, 110, 120, 130, 140, 150, 160};
			for (int t : T)
				sub->addChild(createCheckMenuItem(string::f("%d BPM", t), "", [=]() { return (int) m->freeBpm == t; },
				                                  [=]() { m->freeBpm = (float) t; }));
		}));
		menu->addChild(createMenuLabel(m->clocked ? string::f("Clock at CLK: %.1f BPM", 60.f * APP->engine->getSampleRate() / m->tempo.spb)
		                                          : std::string("No clock at CLK (16ths)")));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Quick keys back to the factory", "", [=]() {
			for (int i = 0; i < QUICKS; i++)
				m->quick[i] = QUICK_DEFAULT[i];
		}));
		learn.appendModuleMenu(menu, [=](const std::string& s) { say(s); });
	}
};

Model* modelChopFx = createModel<ChopFx, ChopFxWidget>("GenraChopFx");
