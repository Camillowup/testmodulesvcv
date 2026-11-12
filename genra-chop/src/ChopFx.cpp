// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP FX: two effect buses to play live, after the idea of the SP-404, in GENRA CHOP's look (36 HP).
//
// Eight sources come in on their own (IN L/R, KICK, SNR, CH, OH, BASS, MEL L/R, PADS L/R); a selector over each says
// where it goes: DRY, bus 1, bus 2 or both. Every source comes out again on its own jack (for WING-LINK: one fader each),
// with the effects on the returns (RET 1 / RET 2): an effect that adds (echoes, reverb) leaves the source dry on its own
// jack and puts what it adds on the return; an effect that changes the sound (filter, vinyl, looper …) takes the source
// over to the return while it plays (crossfaded with FX ON). MIX = everything. ROUTE 1 > 2: what bus 1 makes goes on
// through bus 2 (and 2 > 1 the other way); 1 + 2: side by side.
// Each bus: an effect out of 39 in six groups, CTRL 1-3 with a little screen above each (what it does, its value),
// CV under each, FX ON (tap: on / off, hold: only while held), an ON gate and an FX input. Eight quick keys put an
// effect on the bus last touched. Eight DJ pads act on all the outputs while held (any effect, right-click). DJ FILTER
// (low-pass left, high-pass right) on all the outputs. PRESET: whole setups. Tempo: 16ths at CLK, else the menu's.
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

static const int QUICKS = 8, DJS = 8, SOURCES = 8, GROUPS = 10;
static const int QUICK_DEFAULT[QUICKS] = {FILTDRIVE, RESONATOR, DELAY, ISOLATOR, LOOPER, STOPPER, VINYL, REVERB};
enum Src { S_IN, S_KICK, S_SNR, S_CH, S_OH, S_BASS, S_MEL, S_PADS };
static const char* const SRC_NAMES[SOURCES] = {"IN", "KICK", "SNR", "CH", "OH", "BASS", "MEL", "PADS"};
static const bool SRC_STEREO[SOURCES] = {true, false, false, false, false, false, true, true};
enum Sel { SEL_DRY, SEL_1, SEL_2, SEL_12, NUM_SELS };
static const char* const SEL_NAMES[NUM_SELS] = {"DRY", "1", "2", "1+2"};

// the DJ pads: an effect with its three controls and a name
struct DjPad {
	int fx = DELAY;
	float c[3] = {0.5f, 0.5f, 0.5f};
	std::string name;
};
static DjPad djFactory(int i) {
	DjPad p;
	auto set = [&](int f, float a, float b, float d, const char* n) {
		p.fx = f, p.c[0] = a, p.c[1] = b, p.c[2] = d, p.name = n;
	};
	switch (i) {
		case 0: set(DELAY, 7.f / 11.f, 0.6f, 1.f, "ECHO OUT"); break; // MIX 1: only the echo while held
		case 1: set(LOOPER, 3.f / 6.f, 0.5f, 1.f, "STUTTER 1/4"); break;
		case 2: set(LOOPER, 2.f / 6.f, 0.5f, 1.f, "STUTTER 1/8"); break;
		case 3: set(LOOPER, 1.f / 6.f, 0.5f, 1.f, "STUTTER 1/16"); break;
		case 4: set(TAPESTOP, 0.3f, 0.5f, 1.f, "TAPE STOP"); break;
		case 5: set(BACKSPIN, 0.3f, 0.5f, 1.f, "BACKSPIN"); break;
		case 6: set(LOWCUT, 0.4f, 0.6f, 0.3f, "LOW CUT"); break;
		default: set(HIGHCUT, 0.4f, 0.6f, 0.3f, "HIGH CUT"); break;
	}
	return p;
}

/** Names short enough for a key. */
static std::string shortName(int f) {
	switch (f) {
		case CASSETTE: return "CASSETTE";
		case WOWFLUT: return "WOW";
		case FILTDRIVE: return "FILTER";
		case SUPERFILT: return "SUPER FLT";
		case RESONATOR: return "RESO";
		case TALK: return "TALK";
		case SATURATOR: return "SATURATE";
		case VINYL: return "VINYL";
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
	enum ParamId { IN_PARAM, OUT_PARAM, ENUMS(FX_PARAM, 2), ENUMS(C_PARAM, 6), FILT_PARAM, NUM_PARAMS };
	enum InputId {
		INL_INPUT, INR_INPUT, KICK_INPUT, SNR_INPUT, CH_INPUT, OH_INPUT, BASS_INPUT, MELL_INPUT, MELR_INPUT, PADSL_INPUT,
		PADSR_INPUT, CLK_INPUT, RST_INPUT,
		ENUMS(CV_INPUT, 6), // bus * 3 + k
		ENUMS(ON_INPUT, 2),
		ENUMS(FXCV_INPUT, 2),
		NUM_INPUTS
	};
	enum OutputId {
		INL_OUTPUT, INR_OUTPUT, KICK_OUTPUT, SNR_OUTPUT, CH_OUTPUT, OH_OUTPUT, BASS_OUTPUT, MELL_OUTPUT, MELR_OUTPUT,
		PADSL_OUTPUT, PADSR_OUTPUT,
		ENUMS(RET_OUTPUT, 4), // RET1 L/R, RET2 L/R
		MIXL_OUTPUT, MIXR_OUTPUT,
		NUM_OUTPUTS
	};
	// a source's first jack (in and out are in the same order); stereo sources have R right after
	static int srcJack(int s) {
		static const int J[SOURCES] = {0, 2, 3, 4, 5, 6, 7, 9};
		return J[s];
	}

	// where the sound and the clock come from (saved): today the cables; later also GENRA CHOP without cables
	enum Source { SRC_CABLES = 0, NUM_SOURCE_KINDS };
	int audioSource = SRC_CABLES, clockSource = SRC_CABLES;

	Bus bus[2];
	Bus dj[GROUPS] = {Bus(17), Bus(17), Bus(17), Bus(17), Bus(17), Bus(17), Bus(17), Bus(17), Bus(17), Bus(17)};
	SVF djf[GROUPS][2];
	std::atomic<bool> latch[2];
	std::atomic<bool> busOn[2];
	std::atomic<int> fxNow[2];
	std::atomic<bool> retrig[2];
	std::atomic<int> sel[SOURCES];
	int route = ROUTE_12;
	int quick[QUICKS];
	DjPad djPad[DJS];
	std::atomic<int> djHeld{-1}; // the DJ pad held (-1: none)
	std::atomic<int> djTrig{0};  // +1 when a pad is pressed (the one-shots start again)
	int djSeen = 0;
	int preset = 1;
	float freeBpm = 90.f;
	std::atomic<int> activeBus{0};
	ml::CtrlIn ctrl[ml::CTRLS];

	// the tempo
	Tempo tempo;
	dsp::SchmittTrigger clkTrig, rstTrig;
	float sinceClk = 0.f;
	int pulse = -1;
	bool clocked = false;
	int block = 0;
	float srSeen = 0.f;
	float filtSeen = -1.f;

	ChopFx() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(IN_PARAM, 0.f, 2.f, 1.f, "Input level", "%", 0.f, 100.f);
		configParam(OUT_PARAM, 0.f, 2.f, 1.f, "Output level", "%", 0.f, 100.f);
		configParam(FILT_PARAM, 0.f, 1.f, 0.5f, "DJ filter (left: low-pass, right: high-pass, centre: off)");
		for (int b = 0; b < 2; b++) {
			configParam(FX_PARAM + b, 0.f, (float) (NUM_FX - 1), b == 0 ? (float) VINYL : (float) TAPEECHO,
			            string::f("Bus %d effect", b + 1))->snapEnabled = true;
			for (int k = 0; k < 3; k++) {
				configParam<CtrlQuantity>(C_PARAM + b * 3 + k, 0.f, 1.f, 0.5f, string::f("Bus %d CTRL %d", b + 1, k + 1));
				configInput(CV_INPUT + b * 3 + k, string::f("Bus %d CTRL %d CV (±5 V = ±half the knob)", b + 1, k + 1));
			}
			configInput(ON_INPUT + b, string::f("Bus %d ON (gate)", b + 1));
			configInput(FXCV_INPUT + b, string::f("Bus %d effect (0-10 V over the list)", b + 1));
			latch[b] = false;
			busOn[b] = false;
			fxNow[b] = b == 0 ? VINYL : TAPEECHO;
			retrig[b] = false;
		}
		for (int s = 0; s < SOURCES; s++) {
			int j = srcJack(s);
			if (SRC_STEREO[s]) {
				configInput(j, string::f("%s left (mono: both)", SRC_NAMES[s]));
				configInput(j + 1, string::f("%s right", SRC_NAMES[s]));
				configOutput(j, string::f("%s left (dry, or through the bus while its effect replaces the sound)", SRC_NAMES[s]));
				configOutput(j + 1, string::f("%s right", SRC_NAMES[s]));
			}
			else {
				configInput(j, SRC_NAMES[s]);
				configOutput(j, string::f("%s (dry, or through the bus while its effect replaces the sound)", SRC_NAMES[s]));
			}
			sel[s] = s == S_IN ? SEL_1 : SEL_DRY;
		}
		configInput(CLK_INPUT, "Clock (16ths, e.g. GENRA CHOP's CLK out)");
		configInput(RST_INPUT, "Reset (the bar starts)");
		configOutput(RET_OUTPUT + 0, "Return bus 1 left (what the effect makes)");
		configOutput(RET_OUTPUT + 1, "Return bus 1 right");
		configOutput(RET_OUTPUT + 2, "Return bus 2 left (what the effect makes)");
		configOutput(RET_OUTPUT + 3, "Return bus 2 right");
		configOutput(MIXL_OUTPUT, "Mix left (everything)");
		configOutput(MIXR_OUTPUT, "Mix right (everything)");
		configBypass(INL_INPUT, MIXL_OUTPUT);
		configBypass(INR_INPUT, MIXR_OUTPUT);
		for (int i = 0; i < QUICKS; i++)
			quick[i] = QUICK_DEFAULT[i];
		for (int i = 0; i < DJS; i++)
			djPad[i] = djFactory(i);
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
		retrig[b] = true;
	}

	void readSource(int s, float& L, float& R) {
		switch (audioSource) {
			default: { // SRC_CABLES
				int j = srcJack(s);
				L = inputs[j].getVoltage() / 5.f;
				R = SRC_STEREO[s] && inputs[j + 1].isConnected() ? inputs[j + 1].getVoltage() / 5.f : L;
			}
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

	void stepTempo(float sr) {
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
	}

	void readControls(float sr) {
		for (int b = 0; b < 2; b++) {
			int f = (int) std::round(params[FX_PARAM + b].getValue());
			Input& fxIn = inputs[FXCV_INPUT + b];
			if (fxIn.isConnected())
				f = (int) std::round(clamp(fxIn.getVoltage(), 0.f, 10.f) / 10.f * (NUM_FX - 1));
			f = clamp(f, 0, NUM_FX - 1);
			bus[b].want = f;
			fxNow[b] = f;
			for (int k = 0; k < 3; k++) {
				float v = params[C_PARAM + b * 3 + k].getValue();
				Input& cv = inputs[CV_INPUT + b * 3 + k];
				if (cv.isConnected())
					v += cv.getVoltage() / 10.f;
				bus[b].c[k] = clamp(v, 0.f, 1.f);
			}
			Input& gate = inputs[ON_INPUT + b];
			bool on = latch[b].load() || (gate.isConnected() && gate.getVoltage() >= 1.f);
			bus[b].on = on;
			busOn[b] = on;
			if (retrig[b].exchange(false))
				bus[b].retrigger = true;
		}
		// the DJ pads: one effect on every group of outputs while a pad is held
		int held = djHeld.load();
		int tr = djTrig.load();
		bool again = tr != djSeen;
		djSeen = tr;
		for (int g = 0; g < GROUPS; g++) {
			if (held >= 0) {
				dj[g].want = djPad[held].fx;
				for (int k = 0; k < 3; k++)
					dj[g].c[k] = djPad[held].c[k];
			}
			dj[g].on = held >= 0;
			if (again && held >= 0)
				dj[g].retrigger = true;
		}
		// DJ FILTER: low-pass left of the centre, high-pass right of it
		float fk = params[FILT_PARAM].getValue();
		if (fk != filtSeen) {
			filtSeen = fk;
			float d = (fk - 0.5f) * 2.f;
			float fc = d < 0.f ? 20000.f * std::pow(100.f / 20000.f, -d) : 20.f * std::pow(5000.f / 20.f, d);
			for (int g = 0; g < GROUPS; g++)
				for (int c = 0; c < 2; c++)
					djf[g][c].set(fc, 1.1f, sr);
		}
	}

	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;
		if (sr != srSeen) {
			srSeen = sr;
			for (Bus& b : bus)
				b.setSampleRate(sr);
			for (Bus& b : dj)
				b.setSampleRate(sr);
			filtSeen = -1.f;
		}
		stepTempo(sr);
		if (--block <= 0) {
			block = 16;
			readControls(sr);
		}
		// ---- the sources, and what goes to the buses
		float gin = params[IN_PARAM].getValue();
		float src[SOURCES][2];
		int sl[SOURCES];
		for (int s = 0; s < SOURCES; s++) {
			readSource(s, src[s][0], src[s][1]);
			src[s][0] *= gin, src[s][1] *= gin;
			sl[s] = sel[s].load();
		}
		bool serial = route != ROUTE_PAR;
		int first = route == ROUTE_21 ? 1 : 0, second = 1 - first;
		float in[2][2] = {{0.f, 0.f}, {0.f, 0.f}};
		for (int s = 0; s < SOURCES; s++) {
			int x = sl[s];
			bool to[2] = {x == SEL_1 || x == SEL_12, x == SEL_2 || x == SEL_12};
			if (serial && x == SEL_12)
				to[first] = true, to[second] = false; // in a chain, "both" is: through the first, then the second
			for (int b = 0; b < 2; b++)
				if (to[b])
					in[b][0] += src[s][0], in[b][1] += src[s][1];
		}
		float ret[2][2], keep[2];
		if (!serial) {
			for (int b = 0; b < 2; b++) {
				float L = in[b][0], R = in[b][1];
				bus[b].process(L, R, tempo);
				keep[b] = bus[b].keep();
				ret[b][0] = L - in[b][0] * keep[b];
				ret[b][1] = R - in[b][1] * keep[b];
			}
		}
		else {
			// first bus, then what it made on through the second
			float L = in[first][0], R = in[first][1];
			bus[first].process(L, R, tempo);
			keep[first] = bus[first].keep();
			float ra[2] = {L - in[first][0] * keep[first], R - in[first][1] * keep[first]};
			float ib[2] = {in[second][0] + ra[0], in[second][1] + ra[1]};
			float L2 = ib[0], R2 = ib[1];
			bus[second].process(L2, R2, tempo);
			keep[second] = bus[second].keep();
			ret[second][0] = L2 - ib[0] * keep[second];
			ret[second][1] = R2 - ib[1] * keep[second];
			ret[first][0] = ra[0] * keep[second];
			ret[first][1] = ra[1] * keep[second];
		}
		// ---- the groups of outputs: the sources (dry, or what is left while an effect replaces them), the returns
		float g[GROUPS][2];
		for (int s = 0; s < SOURCES; s++) {
			int x = sl[s];
			float k = 1.f;
			if (!serial)
				k = (x == SEL_1 ? keep[0] : x == SEL_2 ? keep[1] : x == SEL_12 ? keep[0] * keep[1] : 1.f);
			else if (x == SEL_1 || x == SEL_2 || x == SEL_12) {
				int b = x == SEL_12 ? first : (x == SEL_1 ? 0 : 1);
				k = keep[b];
			}
			g[s][0] = src[s][0] * k;
			g[s][1] = src[s][1] * k;
		}
		for (int b = 0; b < 2; b++)
			g[SOURCES + b][0] = ret[b][0], g[SOURCES + b][1] = ret[b][1];
		// ---- the DJ pads and the DJ filter, on every group alike
		bool filt = std::fabs(filtSeen - 0.5f) > 0.02f;
		bool lp = filtSeen < 0.5f;
		for (int i = 0; i < GROUPS; i++) {
			bool mono = i < SOURCES && !SRC_STEREO[i];
			if (mono)
				g[i][1] = g[i][0];
			dj[i].process(g[i][0], g[i][1], tempo);
			if (filt)
				for (int c = 0; c < 2; c++) {
					float l, bp, h;
					djf[i][c].run(g[i][c], l, bp, h);
					g[i][c] = lp ? l : h;
				}
		}
		// ---- out
		float gout = params[OUT_PARAM].getValue() * 5.f;
		auto ceil = [](float v) {
			return std::fabs(v) < 8.f ? v : (v > 0.f ? 1.f : -1.f) * (8.f + 3.f * std::tanh((std::fabs(v) - 8.f) / 3.f));
		};
		float mixL = 0.f, mixR = 0.f;
		for (int s = 0; s < SOURCES; s++) {
			int j = srcJack(s);
			outputs[j].setVoltage(ceil(g[s][0] * gout));
			if (SRC_STEREO[s])
				outputs[j + 1].setVoltage(ceil(g[s][1] * gout));
			mixL += g[s][0], mixR += g[s][1];
		}
		for (int b = 0; b < 2; b++) {
			outputs[RET_OUTPUT + b * 2].setVoltage(ceil(g[SOURCES + b][0] * gout));
			outputs[RET_OUTPUT + b * 2 + 1].setVoltage(ceil(g[SOURCES + b][1] * gout));
			mixL += g[SOURCES + b][0], mixR += g[SOURCES + b][1];
		}
		outputs[MIXL_OUTPUT].setVoltage(ceil(mixL * gout));
		outputs[MIXR_OUTPUT].setVoltage(ceil(mixR * gout));
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
		json_t* s = json_array();
		for (int i = 0; i < SOURCES; i++)
			json_array_append_new(s, json_integer(sel[i].load()));
		json_object_set_new(r, "sel", s);
		json_t* d = json_array();
		for (int i = 0; i < DJS; i++)
			json_array_append_new(d, json_pack("{s:i,s:f,s:f,s:f,s:s}", "fx", djPad[i].fx, "c1", djPad[i].c[0], "c2",
			                                   djPad[i].c[1], "c3", djPad[i].c[2], "name", djPad[i].name.c_str()));
		json_object_set_new(r, "dj", d);
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
		audioSource = clamp(gi("audioSource", SRC_CABLES), 0, NUM_SOURCE_KINDS - 1);
		clockSource = clamp(gi("clockSource", SRC_CABLES), 0, NUM_SOURCE_KINDS - 1);
		latch[0] = json_is_true(json_object_get(r, "on1"));
		latch[1] = json_is_true(json_object_get(r, "on2"));
		activeBus = clamp(gi("activeBus", 0), 0, 1);
		json_t* q = json_object_get(r, "quick");
		for (int i = 0; i < QUICKS && i < (int) json_array_size(q); i++)
			quick[i] = clamp((int) json_integer_value(json_array_get(q, i)), 0, NUM_FX - 1);
		json_t* s = json_object_get(r, "sel");
		for (int i = 0; i < SOURCES && i < (int) json_array_size(s); i++)
			sel[i] = clamp((int) json_integer_value(json_array_get(s, i)), 0, NUM_SELS - 1);
		json_t* d = json_object_get(r, "dj");
		for (int i = 0; i < DJS && i < (int) json_array_size(d); i++) {
			json_t* o = json_array_get(d, i);
			DjPad p = djFactory(i);
			if (json_t* j = json_object_get(o, "fx"))
				p.fx = clamp((int) json_integer_value(j), 0, NUM_FX - 1);
			const char* ck[3] = {"c1", "c2", "c3"};
			for (int k = 0; k < 3; k++)
				if (json_t* j = json_object_get(o, ck[k]))
					p.c[k] = clamp((float) json_number_value(j), 0.f, 1.f);
			if (json_t* j = json_object_get(o, "name"))
				p.name = json_string_value(j);
			djPad[i] = p;
		}
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < QUICKS; i++)
			quick[i] = QUICK_DEFAULT[i];
		for (int i = 0; i < DJS; i++)
			djPad[i] = djFactory(i);
		for (int s = 0; s < SOURCES; s++)
			sel[s] = s == S_IN ? SEL_1 : SEL_DRY;
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
		std::string l1, l2, l3;
		int act = m ? m->activeBus.load() : 0;
		for (int b = 0; b < 2; b++) {
			int fx = m ? m->fxNow[b].load() : (b == 0 ? VINYL : TAPEECHO);
			bool on = m && m->busOn[b].load();
			std::string cv = (m && m->inputs[ChopFx::FXCV_INPUT + b].isConnected()) ? " CV" : "";
			l1 += string::f("%c%d %s%s %s   ", b == act ? '>' : ' ', b + 1, FX[fx].name, cv.c_str(), on ? "ON" : "--");
		}
		for (int s = 0; s < SOURCES; s++) {
			int x = m ? m->sel[s].load() : (s == S_IN ? SEL_1 : SEL_DRY);
			if (x != SEL_DRY)
				l2 += string::f("%s>%s  ", SRC_NAMES[s], SEL_NAMES[x]);
		}
		if (l2.empty())
			l2 = "ALL DRY";
		if (m) {
			float bpm = 60.f * APP->engine->getSampleRate() / std::max(1.f, m->tempo.spb);
			l3 = string::f("TEMPO %.0f BPM (%s)", bpm, m->clocked ? "CLK" : "MENU");
			int h = m->djHeld.load();
			if (h >= 0)
				l3 += "   DJ: " + m->djPad[h].name;
		}
		if (msg && msgTime && *msgTime > 0.f)
			l3 = string::uppercase(*msg);
		nvgSave(vg);
		nvgIntersectScissor(vg, 0, 0, box.size.x - 3.f, box.size.y);
		label(vg, f, 5.f, 8.5f, l1, 11.f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 5.f, 18.5f, l2, 10.f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 5.f, 27.5f, l3, 9.f, nvgRGB(0x3a, 0x4a, 0x2a), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgRestore(vg);
	}
};

/** The little screen above a knob (as on the MPC X): what it does for the effect, its value, a bar. */
struct FxOled : widget::Widget {
	ChopFx* m = nullptr;
	int b = 0, k = 0;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float w = box.size.x, h = box.size.y;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 1.5f);
		nvgFillColor(vg, nvgRGB(0x14, 0x14, 0x13));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(0x55, 0x53, 0x4f));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);
		int fx = m ? m->fxNow[b].load() : (b == 0 ? VINYL : TAPEECHO);
		float v = m ? m->params[ChopFx::C_PARAM + b * 3 + k].getValue() : FX[fx].def[k];
		if (m && m->inputs[ChopFx::CV_INPUT + b * 3 + k].isConnected())
			v = clamp(v + m->inputs[ChopFx::CV_INPUT + b * 3 + k].getVoltage() / 10.f, 0.f, 1.f);
		NVGcolor lit = nvgRGB(0x9f, 0xd3, 0xff);
		labelFit(vg, uiBold(), w / 2, h * 0.27f, FX[fx].ctrl[k], 7.f, w - 4.f, nvgRGB(0xe8, 0xf4, 0xff));
		labelFit(vg, uiBold(), w / 2, h * 0.6f, valueText(fx, k, v), 7.5f, w - 4.f, lit);
		nvgBeginPath(vg);
		nvgRect(vg, 3.f, h - 3.4f, w - 6.f, 1.4f);
		nvgFillColor(vg, nvgRGB(0x2c, 0x2c, 0x2a));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, 3.f, h - 3.4f, (w - 6.f) * v, 1.4f);
		nvgFillColor(vg, lit);
		nvgFill(vg);
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

/** A bus's title bar: a click makes it the bus the quick keys work on (a red line under it). */
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

/** Adds an effect list (in its groups) to a menu. */
static void effectMenu(ui::Menu* menu, std::function<int()> cur, std::function<void(int)> choose) {
	for (int g = 0; g < NUM_GROUPS; g++) {
		menu->addChild(createMenuLabel(GROUP_NAMES[g]));
		for (int f = 0; f < NUM_FX; f++)
			if (FX[f].group == g)
				menu->addChild(createCheckMenuItem(std::string("   ") + FX[f].name, "", [=]() { return cur() == f; },
				                                   [=]() { choose(f); }));
	}
}

struct ChopFxWidget : ModuleWidget, ml::LearnHost {
	ml::Learner learn;
	ml::Learner* learner() override { return &learn; }
	std::string msg;
	float msgTime = 0.f;
	// FX ON and the quick keys: tap = latch, hold = only while held
	double pressAt[2 + QUICKS] = {};
	bool turnedOn[2 + QUICKS] = {};
	int pressBus[2 + QUICKS] = {};
	HHButton* quickBtn[QUICKS] = {};
	HHButton* djBtn[DJS] = {};
	HHButton* selBtn[SOURCES] = {};
	float seen[2][3] = {{-1.f, -1.f, -1.f}, {-1.f, -1.f, -1.f}};

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
		b->rightMenu = [=](ui::Menu* mn) {
			mn->addChild(createMenuLabel(lab));
			learn.appendTargetMenu(mn, id);
		};
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
			m->retrig[b] = true;
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
		knob<HHKnobSmall>(cfx::FILT, m, M::FILT_PARAM, "DJ FILTER");
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
			const char* T[3] = {"1 > 2 (bus 1's effect on through bus 2)", "2 > 1 (bus 2's effect on through bus 1)",
			                    "1 + 2 (side by side)"};
			return std::string(T[i]);
		};
		rt->choose = [=](int i) {
			if (m)
				m->route = i;
		};
		learnField(rt, "route", "ROUTE");
		// the buses
		const float* FXF[2] = {cfx::FX0, cfx::FX1};
		const float* CK[2][3] = {{cfx::C0_0, cfx::C0_1, cfx::C0_2}, {cfx::C1_0, cfx::C1_1, cfx::C1_2}};
		const float* OL[2][3] = {{cfx::OLED0_0, cfx::OLED0_1, cfx::OLED0_2}, {cfx::OLED1_0, cfx::OLED1_1, cfx::OLED1_2}};
		const float* CVJ[2][3] = {{cfx::CV0_0_IN, cfx::CV0_1_IN, cfx::CV0_2_IN}, {cfx::CV1_0_IN, cfx::CV1_1_IN, cfx::CV1_2_IN}};
		const float* ONB[2] = {cfx::ON0, cfx::ON1};
		for (int b = 0; b < 2; b++) {
			// the title bar above the field: select the bus
			float tr[4] = {FXF[b][0] - 4.7f, FXF[b][1] - 19.f, FXF[b][2] + 9.4f, 14.f};
			BusSel* s = new BusSel;
			s->box = rectOf(tr);
			s->m = m;
			s->b = b;
			addChild(s);
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
			for (int k = 0; k < 3; k++) {
				FxOled* o = at<FxOled>(OL[b][k]);
				o->m = m;
				o->b = b;
				o->k = k;
				knob<HHKnob>(CK[b][k], m, M::C_PARAM + b * 3 + k, string::f("BUS %d CTRL %d", b + 1, k + 1));
				addInput(createInputCentered<PJ301MPort>(Vec(CVJ[b][k][0], CVJ[b][k][1]), m, M::CV_INPUT + b * 3 + k));
			}
			HHButton* on = button(ONB[b], "FX ON", nvgRGB(0x55, 0x53, 0x4f), [=]() { press(m, b); }, string::f("on%d", b),
			                      string::f("BUS %d FX ON", b + 1));
			on->release = [=]() { release(m, b); };
			on->colOf = [=]() { return m && m->busOn[b].load() ? C_RED : nvgRGB(0x55, 0x53, 0x4f); };
		}
		// the DJ pads: while held, on every output
		const float* DJR[DJS] = {cfx::DJ0, cfx::DJ1, cfx::DJ2, cfx::DJ3, cfx::DJ4, cfx::DJ5, cfx::DJ6, cfx::DJ7};
		for (int i = 0; i < DJS; i++) {
			std::string id = string::f("dj%d", i);
			HHButton* d = button(DJR[i], "", C_PAD, [=]() {
				if (!m)
					return;
				m->djHeld = i;
				m->djTrig.fetch_add(1);
			}, id, string::f("DJ PAD %d", i + 1));
			d->textCol = C_INK;
			d->fontSize = 8.f;
			d->release = [=]() {
				if (m && m->djHeld.load() == i)
					m->djHeld = -1;
			};
			d->colOf = [=]() { return m && m->djHeld.load() == i ? nvgRGB(0xf2, 0xc9, 0x6a) : C_PAD; };
			d->rightMenu = [=](ui::Menu* mn) {
				if (!m)
					return;
				ChopFx* mm = m;
				mn->addChild(createMenuLabel(string::f("DJ pad %d", i + 1)));
				mn->addChild(createSubmenuItem("Effect", FX[m->djPad[i].fx].name, [=](ui::Menu* sub) {
					sub->addChild(createMenuLabel("DJ: the factory pads"));
					for (int k = 0; k < DJS; k++) {
						DjPad f = djFactory(k);
						sub->addChild(createMenuItem(std::string("   ") + f.name, "", [=]() { mm->djPad[i] = f; }));
					}
					effectMenu(sub, [=]() { return mm->djPad[i].fx; }, [=](int f) {
						DjPad p;
						p.fx = f;
						for (int c = 0; c < 3; c++)
							p.c[c] = FX[f].def[c];
						p.name = FX[f].name;
						mm->djPad[i] = p;
					});
				}));
				for (int b = 0; b < 2; b++)
					mn->addChild(createMenuItem(string::f("Copy settings from BUS %d", b + 1), "", [=]() {
						DjPad p;
						p.fx = mm->fxNow[b].load();
						for (int c = 0; c < 3; c++)
							p.c[c] = mm->params[ChopFx::C_PARAM + b * 3 + c].getValue();
						p.name = FX[p.fx].name;
						mm->djPad[i] = p;
					}));
				mn->addChild(createMenuItem("Back to the factory pad", djFactory(i).name, [=]() { mm->djPad[i] = djFactory(i); }));
				learn.appendTargetMenu(mn, id);
			};
			djBtn[i] = d;
		}
		// the quick keys
		const float* QK[QUICKS] = {cfx::Q0, cfx::Q1, cfx::Q2, cfx::Q3, cfx::Q4, cfx::Q5, cfx::Q6, cfx::Q7};
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
				ChopFx* mm = m;
				mn->addChild(createMenuLabel(string::f("Quick key %d", i + 1)));
				mn->addChild(createSubmenuItem("Effect", FX[m->quick[i]].name, [=](ui::Menu* sub) {
					effectMenu(sub, [=]() { return mm->quick[i]; }, [=](int f) { mm->quick[i] = f; });
				}));
				mn->addChild(createMenuItem("Back to the factory key", FX[QUICK_DEFAULT[i]].name,
				                            [=]() { mm->quick[i] = QUICK_DEFAULT[i]; }));
				learn.appendTargetMenu(mn, id);
			};
		}
		// the sources: the selector (DRY / 1 / 2 / 1+2), the jacks in, the jacks out
		const float* SELR[SOURCES] = {cfx::SEL_IN, cfx::SEL_KICK, cfx::SEL_SNR, cfx::SEL_CH,
		                              cfx::SEL_OH, cfx::SEL_BASS, cfx::SEL_MEL, cfx::SEL_PADS};
		for (int s = 0; s < SOURCES; s++) {
			std::string id = string::f("sel%d", s);
			HHButton* sb = button(SELR[s], "DRY", nvgRGB(0x55, 0x53, 0x4f), [=]() {
				if (m)
					m->sel[s] = (m->sel[s].load() + 1) % NUM_SELS;
			}, id, string::f("%s goes to", SRC_NAMES[s]));
			sb->fontSize = 7.5f;
			sb->colOf = [=]() { return m && m->sel[s].load() != SEL_DRY ? C_RED : nvgRGB(0x55, 0x53, 0x4f); };
			sb->rightMenu = [=](ui::Menu* mn) {
				if (!m)
					return;
				ChopFx* mm = m;
				mn->addChild(createMenuLabel(string::f("%s goes to", SRC_NAMES[s])));
				const char* T[NUM_SELS] = {"DRY (straight out)", "Bus 1", "Bus 2", "Bus 1 and bus 2"};
				for (int x = 0; x < NUM_SELS; x++)
					mn->addChild(createCheckMenuItem(T[x], "", [=]() { return mm->sel[s].load() == x; }, [=]() { mm->sel[s] = x; }));
				learn.appendTargetMenu(mn, id);
			};
			selBtn[s] = sb;
		}
		auto in = [&](const float* p, int id) { addInput(createInputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		auto out = [&](const float* p, int id) { addOutput(createOutputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		const float* JIN[11] = {cfx::INL_IN, cfx::INR_IN, cfx::KICK_IN, cfx::SNR_IN, cfx::CH_IN, cfx::OH_IN,
		                        cfx::BASS_IN, cfx::MELL_IN, cfx::MELR_IN, cfx::PADSL_IN, cfx::PADSR_IN};
		const float* JOUT[11] = {cfx::INL_OUT, cfx::INR_OUT, cfx::KICK_OUT, cfx::SNR_OUT, cfx::CH_OUT, cfx::OH_OUT,
		                         cfx::BASS_OUT, cfx::MELL_OUT, cfx::MELR_OUT, cfx::PADSL_OUT, cfx::PADSR_OUT};
		for (int j = 0; j < 11; j++) {
			in(JIN[j], M::INL_INPUT + j);
			out(JOUT[j], M::INL_OUTPUT + j);
		}
		in(cfx::CLK_IN, M::CLK_INPUT);
		in(cfx::RST_IN, M::RST_INPUT);
		in(cfx::B0_ON_IN, M::ON_INPUT + 0);
		in(cfx::B0_FX_IN, M::FXCV_INPUT + 0);
		in(cfx::B1_ON_IN, M::ON_INPUT + 1);
		in(cfx::B1_FX_IN, M::FXCV_INPUT + 1);
		const float* RT[4] = {cfx::RET0_OUT, cfx::RET1_OUT, cfx::RET2_OUT, cfx::RET3_OUT};
		for (int i = 0; i < 4; i++)
			out(RT[i], M::RET_OUTPUT + i);
		out(cfx::OUTL_OUT, M::MIXL_OUTPUT);
		out(cfx::OUTR_OUT, M::MIXR_OUTPUT);
	}

	void step() override {
		ChopFx* m = dynamic_cast<ChopFx*>(module);
		if (m) {
			learn.step();
			for (int i = 0; i < QUICKS; i++)
				if (quickBtn[i])
					quickBtn[i]->text = shortName(m->quick[i]);
			for (int i = 0; i < DJS; i++)
				if (djBtn[i])
					djBtn[i]->text = m->djPad[i].name;
			for (int s = 0; s < SOURCES; s++)
				if (selBtn[s])
					selBtn[s]->text = SEL_NAMES[m->sel[s].load()];
			// a knob turned: its bus is the one the quick keys work on
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
		menu->addChild(createMenuItem("DJ pads back to the factory", "", [=]() {
			for (int i = 0; i < DJS; i++)
				m->djPad[i] = djFactory(i);
		}));
		menu->addChild(createMenuItem("All sources DRY", "", [=]() {
			for (int s = 0; s < SOURCES; s++)
				m->sel[s] = SEL_DRY;
		}));
		learn.appendModuleMenu(menu, [=](const std::string& s) { say(s); });
	}
};

Model* modelChopFx = createModel<ChopFx, ChopFxWidget>("GenraChopFx");
