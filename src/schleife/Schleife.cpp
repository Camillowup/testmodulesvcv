// SPDX-License-Identifier: GPL-3.0-only
// SCHLEIFE – a four-track looper in the GENRA look (after the Boss RC-505), in step with GENRA's clock.
//
//  - Each track records from its own input (IN 1–4) or, when nothing is plugged in there, from MAIN IN.
//  - One button per track (REC/DUB), as on the Boss: on an empty track it waits for the next bar and records BARS bars
//    (1, 2, 4, 8 or 16), then plays. A press while it plays switches overdub on, the next press off (at once, not on
//    the bar). Without a clock at CLOCK it records freely: a press starts, the next ends the loop.
//  - UNDO takes back the last overdub (or a CLEAR); pressed again it brings it back (redo). CLEAR empties a track,
//    CLR ALL (twice within two seconds) all four; both can be taken back with UNDO.
//  - CLOCK and RESET from GENRA (resolution in the field at the top and in the menu, as GENRA's CLOCK output). Every
//    loop starts again on its own bar line, so it stays with GENRA even when the tempo moves a little; RESET starts all
//    loops from the beginning. When the clock stops (GENRA stopped), the loops are silent and wait.
//  - OUT 1–4 the tracks after LEVEL, MIX L/R their sum after MASTER (MIX takes it without a cable too).
//  - The loops are saved with the patch (WAV files in the patch's storage); right-click: export a track, all tracks or
//    the mix as WAV.
// Panel: tools/schleife_panel.py (res/Schleife.svg + SchleifeLayout.hpp).
#include "../plugin.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/ui/GenraUI.hpp"
#include "../../genra/ui/Look.hpp"
#include "SchleifeLayout.hpp"
#include <osdialog.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace schleife {

namespace L = schleifel;
namespace ui = genra_ui;

static const int NT = 4;
static const float MAX_SEC = 48.f; // the longest loop a track holds
static const int NBARS = 5;
static const int BARS_OF[NBARS] = {1, 2, 4, 8, 16};
static const char* const BARS_NAME[NBARS] = {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
static const int NCLOCK = 4;
static const int PPB[NCLOCK] = {16, 8, 4, 96}; // clock pulses per 4/4 bar
static const char* const CLOCK_NAME[NCLOCK] = {"16THS · 4 PPQN", "8THS · 2 PPQN", "QUARTERS · 1 PPQN", "24 PPQN"};
static const float CLR_ALL_WAIT = 2.f; // CLR ALL: the second press within this time

static const NVGcolor ACC = nvgRGB(0x8f, 0xd3, 0xff);
static const NVGcolor C_PLAY = nvgRGB(0x5d, 0x8f, 0xa8), C_DUB = nvgRGB(0xb8, 0x87, 0x3d), C_REC = nvgRGB(0xb8, 0x4a, 0x4a);
static const NVGcolor C_BAR = nvgRGB(0x1c, 0x1e, 0x24), C_DARK = nvgRGB(0x07, 0x08, 0x0a);

enum State { EMPTY, ARMED, REC, PLAY, DUB };
static const char* const STATE_NAME[5] = {"EMPTY", "WAIT", "REC", "PLAY", "DUB"};

/** What a buffer holds: its length, the bars it was recorded with, free (no clock) and where it started. */
struct Take {
	int len = 0;
	int bars = 0;
	bool free = false;
	int64_t startPulse = 0;
};

struct Track {
	std::vector<float> a, b; // the loop and the one UNDO brings back
	Take take, undoTake;
	bool hasUndo = false;
	int state = EMPTY;
	int pos = 0;
	int recPulses = 0;
	bool dubAfter = false; // a press while recording: overdub as soon as the recording is done
	bool dirty = false;    // changed since the last save (the patch storage file is written again)
	float flashUndo = 0.f, flashClear = 0.f;
};

// ---------------------------------------------------------------- WAV (32-bit float, mono)
static void put32(FILE* f, uint32_t v) {
	uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
	std::fwrite(b, 1, 4, f);
}
static void put16(FILE* f, uint16_t v) {
	uint8_t b[2] = {(uint8_t) v, (uint8_t) (v >> 8)};
	std::fwrite(b, 1, 2, f);
}

/** Writes n samples (times scale) as a mono 32-bit float WAV. */
static bool writeWav(const std::string& path, const float* d, int n, int sr, float scale) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	uint32_t bytes = (uint32_t) n * 4;
	std::fwrite("RIFF", 1, 4, f);
	put32(f, 36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16);
	put16(f, 3); // IEEE float
	put16(f, 1);
	put32(f, (uint32_t) sr);
	put32(f, (uint32_t) sr * 4);
	put16(f, 4);
	put16(f, 32);
	std::fwrite("data", 1, 4, f);
	put32(f, bytes);
	std::vector<float> buf(4096);
	for (int i = 0; i < n; i += 4096) {
		int k = std::min(4096, n - i);
		for (int j = 0; j < k; j++)
			buf[j] = d[i + j] * scale;
		std::fwrite(buf.data(), 4, k, f);
	}
	std::fclose(f);
	return true;
}

static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t get16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

/** Reads a mono (or the first channel of a) 32-bit float or 16-bit WAV; its sample rate into sr. */
static bool readWav(const std::string& path, std::vector<float>& out, int& sr, float scale) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::fseek(f, 0, SEEK_END);
	long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> d(size > 0 ? (size_t) size : 0);
	size_t got = size > 0 ? std::fread(d.data(), 1, (size_t) size, f) : 0;
	std::fclose(f);
	if (got < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
		return false;
	int fmt = 0, ch = 1, bits = 0;
	size_t p = 12;
	while (p + 8 <= got) {
		uint32_t len = get32(&d[p + 4]);
		const uint8_t* c = &d[p + 8];
		if (!std::memcmp(&d[p], "fmt ", 4) && p + 8 + 16 <= got) {
			fmt = get16(c);
			ch = std::max(1, (int) get16(c + 2));
			sr = (int) get32(c + 4);
			bits = get16(c + 14);
		}
		else if (!std::memcmp(&d[p], "data", 4)) {
			size_t n = std::min((size_t) len, got - p - 8);
			if (fmt == 3 && bits == 32) {
				size_t frames = n / (4 * ch);
				out.resize(frames);
				for (size_t i = 0; i < frames; i++) {
					float v;
					std::memcpy(&v, c + i * 4 * ch, 4);
					out[i] = v * scale;
				}
				return true;
			}
			if (fmt == 1 && bits == 16) {
				size_t frames = n / (2 * ch);
				out.resize(frames);
				for (size_t i = 0; i < frames; i++)
					out[i] = (int16_t) get16(c + i * 2 * ch) / 32768.f * scale;
				return true;
			}
			return false;
		}
		p += 8 + len + (len & 1);
	}
	return false;
}

// ---------------------------------------------------------------- the module
struct Schleife : Module, follow::Host {
	enum ParamId {
		BARS_PARAM,
		LEVEL_PARAM = BARS_PARAM + NT,
		REC_PARAM = LEVEL_PARAM + NT,
		UNDO_PARAM = REC_PARAM + NT,
		CLEAR_PARAM = UNDO_PARAM + NT,
		MASTER_PARAM = CLEAR_PARAM + NT,
		CLRALL_PARAM,
		NUM_PARAMS
	};
	enum InputId { CLOCK_INPUT, RESET_INPUT, MAIN_INPUT, TRACK_INPUT, NUM_INPUTS = TRACK_INPUT + NT };
	enum OutputId { TRACK_OUTPUT, MIXL_OUTPUT = TRACK_OUTPUT + NT, MIXR_OUTPUT, NUM_OUTPUTS };
	enum LightId {
		STAT_LIGHT,
		REC_LIGHT = STAT_LIGHT + 3 * NT,
		UNDO_LIGHT = REC_LIGHT + 3 * NT,
		CLEAR_LIGHT = UNDO_LIGHT + NT,
		CLRALL_LIGHT = CLEAR_LIGHT + NT,
		NUM_LIGHTS
	};

	Track tr[NT];
	int maxLen = 0;
	float sampleRate = 48000.f;
	int clockMode = 0;

	// the clock
	dsp::SchmittTrigger clockT, resetT;
	dsp::BooleanTrigger recB[NT], undoB[NT], clearB[NT], clrAllB;
	int64_t pulse = -1;     // pulses since the last reset (the first one after a reset is 0)
	int64_t sinceClock = 0; // samples since the last pulse
	int64_t period = 0;     // samples between the last two pulses
	bool seenPulse = false;
	bool running = true;    // the clock runs (or there is no clock: free)
	float runGain = 1.f;
	float clrAllArm = 0.f;  // CLR ALL: time left for the second press
	dsp::ClockDivider lightDiv;

	// what onAdd loads from the patch storage (read in dataFromJson)
	Take loadTake[NT];
	int loadRate = 0;

	Schleife() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < NT; i++) {
			configSwitch(BARS_PARAM + i, 0.f, NBARS - 1, 1.f, string::f("Track %d length (for the next recording)", i + 1),
			             std::vector<std::string>(BARS_NAME, BARS_NAME + NBARS));
			configParam(LEVEL_PARAM + i, 0.f, 1.f, 1.f, string::f("Track %d level", i + 1), "%", 0.f, 100.f);
			configButton(REC_PARAM + i, string::f("Track %d REC/DUB (empty: record from the next bar; playing: overdub on/off)", i + 1));
			configButton(UNDO_PARAM + i, string::f("Track %d UNDO (the last overdub or clear; again: redo)", i + 1));
			configButton(CLEAR_PARAM + i, string::f("Track %d CLEAR (UNDO brings it back)", i + 1));
			configInput(TRACK_INPUT + i, string::f("Track %d input (unplugged: MAIN IN)", i + 1));
			configOutput(TRACK_OUTPUT + i, string::f("Track %d", i + 1));
			configLight(STAT_LIGHT + 3 * i, string::f("Track %d (red: recording, orange: overdub, green: plays)", i + 1));
		}
		configParam(MASTER_PARAM, 0.f, 1.f, 1.f, "Master (MIX L/R)", "%", 0.f, 100.f);
		configButton(CLRALL_PARAM, "CLR ALL (press twice; UNDO of each track brings it back)");
		configInput(CLOCK_INPUT, "Clock (from GENRA; resolution in the field at the top)");
		configInput(RESET_INPUT, "Reset (all loops from the start)");
		configInput(MAIN_INPUT, "Main in (for every track without its own input)");
		configOutput(MIXL_OUTPUT, "Mix left");
		configOutput(MIXR_OUTPUT, "Mix right");
		lightDiv.setDivision(64);
		srcName = "SCHLEIFE";
		audioL = MIXL_OUTPUT;
		audioR = MIXR_OUTPUT;
		follows = false;
	}

	// ------------------------------------------------------------ buffers (never in process())
	void allocate(float sr) {
		int n = (int) (MAX_SEC * sr);
		for (int i = 0; i < NT; i++) {
			tr[i].a.resize(n, 0.f);
			tr[i].b.resize(n, 0.f);
			tr[i].take.len = std::min(tr[i].take.len, n);
			tr[i].undoTake.len = std::min(tr[i].undoTake.len, n);
			tr[i].pos = std::min(tr[i].pos, std::max(0, n - 1));
		}
		maxLen = n;
		sampleRate = sr;
	}

	void onAdd(const AddEvent& e) override {
		allocate(APP->engine->getSampleRate());
		std::string dir = getPatchStorageDirectory();
		for (int i = 0; i < NT; i++) {
			Track& t = tr[i];
			if (loadTake[i].len <= 0)
				continue;
			std::vector<float> d;
			int sr = loadRate > 0 ? loadRate : (int) sampleRate;
			if (!readWav(system::join(dir, string::f("track%d.wav", i + 1)), d, sr, 1.f) || d.empty())
				continue;
			// another sample rate than when it was saved: resample (linear)
			int n = (int) d.size();
			if (sr > 0 && sr != (int) sampleRate) {
				int m = (int) ((double) n * sampleRate / sr);
				std::vector<float> r(std::max(0, m));
				for (int k = 0; k < m; k++) {
					double x = (double) k * sr / sampleRate;
					int x0 = std::min((int) x, n - 1), x1 = std::min(x0 + 1, n - 1);
					float f = (float) (x - x0);
					r[k] = d[x0] + (d[x1] - d[x0]) * f;
				}
				d.swap(r);
				n = (int) d.size();
			}
			n = std::min(n, maxLen);
			std::copy(d.begin(), d.begin() + n, t.a.begin());
			t.take = loadTake[i];
			t.take.len = n;
			t.state = n > 0 ? PLAY : EMPTY;
			t.pos = 0;
			t.hasUndo = false;
		}
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { allocate(e.sampleRate); }

	void onSave(const SaveEvent& e) override {
		std::string dir = createPatchStorageDirectory();
		for (int i = 0; i < NT; i++) {
			std::string path = system::join(dir, string::f("track%d.wav", i + 1));
			Track& t = tr[i];
			if (!hasLoop(i)) {
				if (system::isFile(path))
					system::remove(path);
				continue;
			}
			// (autosave runs often: only what changed, and a track while it overdubs)
			if (t.dirty || t.state == DUB || !system::isFile(path)) {
				t.dirty = false;
				writeWav(path, t.a.data(), t.take.len, (int) sampleRate, 1.f);
			}
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < NT; i++) {
			Track& t = tr[i];
			t.take = Take();
			t.undoTake = Take();
			t.hasUndo = false;
			t.state = EMPTY;
			t.pos = 0;
			t.dubAfter = false;
		}
		clockMode = 0;
	}

	void onRemove(const RemoveEvent& e) override { follow::gone(this); }

	// ------------------------------------------------------------ the tracks
	bool hasLoop(int i) const { return (tr[i].state == PLAY || tr[i].state == DUB) && tr[i].take.len > 0; }
	int barsKnob(int i) { return BARS_OF[clamp((int) std::round(params[BARS_PARAM + i].getValue()), 0, NBARS - 1)]; }
	bool clocked() { return inputs[CLOCK_INPUT].isConnected(); }

	void startRec(int i) {
		Track& t = tr[i];
		t.state = REC;
		t.pos = 0;
		t.recPulses = 0;
		t.dubAfter = false;
		t.take = Take();
		t.take.bars = barsKnob(i);
		t.take.free = !clocked();
		t.take.startPulse = std::max<int64_t>(pulse, 0);
	}

	/** The recording is done: the loop plays (the state before it, empty, is what UNDO brings back). */
	void finishRec(int i) {
		Track& t = tr[i];
		int n = t.pos;
		if (n < 16) {
			t.state = EMPTY;
			t.take = Take();
			return;
		}
		// short fades at both ends against clicks at the loop point
		int f = std::min(n / 4, (int) (0.003f * sampleRate));
		for (int k = 0; k < f; k++) {
			float g = (float) k / f;
			t.a[k] *= g;
			t.a[n - 1 - k] *= g;
		}
		t.take.len = n;
		t.undoTake = Take();
		t.hasUndo = true;
		t.dirty = true;
		t.state = PLAY;
		t.pos = 0;
		if (t.dubAfter)
			startDub(i);
		t.dubAfter = false;
	}

	void startDub(int i) {
		Track& t = tr[i];
		std::copy(t.a.begin(), t.a.begin() + t.take.len, t.b.begin());
		t.undoTake = t.take;
		t.hasUndo = true;
		t.state = DUB;
	}

	void cancelRec(int i) {
		Track& t = tr[i];
		t.state = EMPTY;
		t.take = Take();
		t.pos = 0;
		t.dubAfter = false;
	}

	/** UNDO and redo: the loop and the one before swap. */
	void swapUndo(int i) {
		Track& t = tr[i];
		if (!t.hasUndo)
			return;
		t.a.swap(t.b);
		std::swap(t.take, t.undoTake);
		t.dirty = true;
		t.state = t.take.len > 0 ? PLAY : EMPTY;
		if (t.take.len > 0)
			t.pos %= t.take.len;
		else
			t.pos = 0;
	}

	void clearTrack(int i) {
		Track& t = tr[i];
		if (t.state == ARMED || t.state == REC) {
			cancelRec(i);
			return;
		}
		if (!hasLoop(i))
			return;
		t.a.swap(t.b);
		t.undoTake = t.take;
		t.take = Take();
		t.hasUndo = true;
		t.state = EMPTY;
		t.pos = 0;
	}

	void pressRec(int i) {
		Track& t = tr[i];
		switch (t.state) {
			case EMPTY:
				if (clocked())
					t.state = ARMED;
				else
					startRec(i);
				break;
			case ARMED: t.state = EMPTY; break;
			case REC:
				if (clocked() && !t.take.free)
					t.dubAfter = !t.dubAfter;
				else
					finishRec(i);
				break;
			case PLAY: startDub(i); break;
			case DUB:
				t.state = PLAY;
				t.dirty = true;
				break;
		}
	}

	void pressUndo(int i) {
		Track& t = tr[i];
		t.flashUndo = 0.15f;
		if (t.state == ARMED || t.state == REC) {
			cancelRec(i);
			return;
		}
		if (t.state == DUB)
			t.state = PLAY;
		swapUndo(i);
	}

	// ------------------------------------------------------------ process
	void process(const ProcessArgs& args) override {
		if (maxLen <= 0)
			return;
		bool clk = clocked();

		// buttons (the mouse, MAP-X or MIDI-MAP)
		for (int i = 0; i < NT; i++) {
			if (recB[i].process(params[REC_PARAM + i].getValue() > 0.5f))
				pressRec(i);
			if (undoB[i].process(params[UNDO_PARAM + i].getValue() > 0.5f))
				pressUndo(i);
			if (clearB[i].process(params[CLEAR_PARAM + i].getValue() > 0.5f)) {
				tr[i].flashClear = 0.15f;
				clearTrack(i);
			}
		}
		if (clrAllB.process(params[CLRALL_PARAM].getValue() > 0.5f)) {
			if (clrAllArm > 0.f) {
				for (int i = 0; i < NT; i++)
					clearTrack(i);
				clrAllArm = 0.f;
			}
			else
				clrAllArm = CLR_ALL_WAIT;
		}
		if (clrAllArm > 0.f)
			clrAllArm -= args.sampleTime;

		// the clock. GENRA sends RESET at the start of each of its loops: while the clock runs and the next pulse is a
		// bar line anyway, that changes nothing (an 8- or 16-bar loop goes on). A reset after a stop (RUN) or in the
		// middle of a bar starts all loops from their beginning.
		int ppb = PPB[clamp(clockMode, 0, NCLOCK - 1)];
		if (inputs[RESET_INPUT].isConnected() && resetT.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) &&
		    !(running && seenPulse && clk && (pulse + 1) % ppb == 0)) {
			pulse = -1;
			for (int i = 0; i < NT; i++) {
				Track& t = tr[i];
				if (t.state == REC && !t.take.free)
					t.state = ARMED; // a recording begun before the reset starts again on the next bar
				if (hasLoop(i) && !t.take.free) {
					t.take.startPulse = 0;
					t.pos = 0;
				}
			}
		}
		bool tick = false;
		sinceClock++;
		if (clk && clockT.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			tick = true;
			if (seenPulse)
				period = sinceClock;
			sinceClock = 0;
			seenPulse = true;
			pulse++;
		}
		if (!clk) {
			running = true;
			seenPulse = false;
		}
		else {
			int64_t timeout = period > 0 ? std::max<int64_t>(period * 5 / 2, (int64_t) (0.02f * args.sampleRate))
			                             : (int64_t) args.sampleRate;
			running = seenPulse && sinceClock < timeout;
		}
		float rg = running ? 1.f : 0.f;
		float step = args.sampleTime / 0.005f;
		runGain += clamp(rg - runGain, -step, step);

		if (tick) {
			bool bar = pulse % ppb == 0;
			for (int i = 0; i < NT; i++) {
				Track& t = tr[i];
				if (t.state == ARMED && bar)
					startRec(i);
				else if (t.state == REC && !t.take.free) {
					if (++t.recPulses >= t.take.bars * ppb)
						finishRec(i);
				}
				else if ((t.state == PLAY || t.state == DUB) && !t.take.free && t.take.bars > 0) {
					int64_t loopP = (int64_t) t.take.bars * ppb;
					int64_t rel = pulse - t.take.startPulse;
					if (((rel % loopP) + loopP) % loopP == 0)
						t.pos = 0; // the loop's own bar line: back to its start (stays with GENRA)
				}
			}
		}

		// audio
		float master = params[MASTER_PARAM].getValue();
		master *= master;
		float mix = 0.f;
		float mainIn = inputs[MAIN_INPUT].getVoltage();
		bool moving = running || !clk;
		for (int i = 0; i < NT; i++) {
			Track& t = tr[i];
			float in = inputs[TRACK_INPUT + i].isConnected() ? inputs[TRACK_INPUT + i].getVoltage() : mainIn;
			float out = 0.f;
			if (t.state == REC) {
				if (moving || t.take.free) {
					t.a[t.pos] = in;
					if (++t.pos >= maxLen)
						finishRec(i);
				}
			}
			else if ((t.state == PLAY || t.state == DUB) && t.take.len > 0) {
				bool go = t.take.free || moving;
				if (go) {
					if (t.pos >= t.take.len)
						t.pos = 0;
					out = t.a[t.pos];
					if (t.state == DUB)
						t.a[t.pos] += in;
					if (++t.pos >= t.take.len)
						t.pos = 0;
				}
				if (!t.take.free)
					out *= runGain;
			}
			float lv = params[LEVEL_PARAM + i].getValue();
			out *= lv * lv;
			outputs[TRACK_OUTPUT + i].setVoltage(out);
			mix += out;
		}
		mix *= master;
		outputs[MIXL_OUTPUT].setVoltage(mix);
		outputs[MIXR_OUTPUT].setVoltage(mix);

		if (lightDiv.process())
			updateLights(args.sampleTime * lightDiv.getDivision());
	}

	void setRgb(int id, int state, bool blink) {
		float r = 0.f, g = 0.f;
		switch (state) {
			case ARMED: r = blink ? 1.f : 0.15f; break;
			case REC: r = 1.f; break;
			case DUB: r = 1.f; g = 0.45f; break;
			case PLAY: g = 1.f; break;
			default: break;
		}
		lights[id].setBrightness(r);
		lights[id + 1].setBrightness(g);
		lights[id + 2].setBrightness(0.f);
	}

	void updateLights(float dt) {
		bool blink = std::fmod(system::getTime(), 0.5) < 0.25;
		for (int i = 0; i < NT; i++) {
			Track& t = tr[i];
			int st = t.state;
			if (st == REC && t.dubAfter)
				st = blink ? REC : DUB;
			setRgb(STAT_LIGHT + 3 * i, st, blink);
			setRgb(REC_LIGHT + 3 * i, st, blink);
			t.flashUndo = std::max(0.f, t.flashUndo - dt);
			t.flashClear = std::max(0.f, t.flashClear - dt);
			lights[UNDO_LIGHT + i].setBrightness(t.flashUndo > 0.f ? 1.f : 0.f);
			lights[CLEAR_LIGHT + i].setBrightness(t.flashClear > 0.f ? 1.f : 0.f);
		}
		lights[CLRALL_LIGHT].setBrightness(clrAllArm > 0.f ? (blink ? 1.f : 0.3f) : 0.f);
	}

	// ------------------------------------------------------------ the display
	/** Where the track is in its loop (0..1), -1: nothing to show. */
	float progress(int i) {
		const Track& t = tr[i];
		if (t.state == REC) {
			if (t.take.free)
				return maxLen > 0 ? (float) t.pos / maxLen : 0.f;
			int ppb = PPB[clamp(clockMode, 0, NCLOCK - 1)];
			float sub = period > 0 ? std::min(1.f, (float) sinceClock / period) : 0.f;
			return clamp((t.recPulses + sub) / (float) (t.take.bars * ppb), 0.f, 1.f);
		}
		if ((t.state == PLAY || t.state == DUB) && t.take.len > 0)
			return clamp((float) t.pos / t.take.len, 0.f, 1.f);
		return -1.f;
	}

	// ------------------------------------------------------------ export (UI thread)
	bool exportTrack(int i, const std::string& path) {
		if (!hasLoop(i))
			return false;
		return writeWav(path, tr[i].a.data(), tr[i].take.len, (int) sampleRate, 0.2f);
	}
	bool exportMix(const std::string& path) {
		int n = 0;
		for (int i = 0; i < NT; i++)
			if (hasLoop(i))
				n = std::max(n, tr[i].take.len);
		if (n <= 0)
			return false;
		std::vector<float> m(n, 0.f);
		float master = params[MASTER_PARAM].getValue();
		master *= master;
		for (int i = 0; i < NT; i++) {
			if (!hasLoop(i))
				continue;
			float lv = params[LEVEL_PARAM + i].getValue();
			float g = lv * lv * master;
			int len = tr[i].take.len;
			for (int k = 0; k < n; k++)
				m[k] += tr[i].a[k % len] * g;
		}
		return writeWav(path, m.data(), n, (int) sampleRate, 0.2f);
	}

	// ------------------------------------------------------------ patch
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "clockMode", json_integer(clockMode));
		json_object_set_new(root, "sampleRate", json_integer((int) sampleRate));
		json_t* ta = json_array();
		for (int i = 0; i < NT; i++) {
			json_t* t = json_object();
			bool has = hasLoop(i);
			json_object_set_new(t, "len", json_integer(has ? tr[i].take.len : 0));
			json_object_set_new(t, "bars", json_integer(tr[i].take.bars));
			json_object_set_new(t, "free", json_boolean(tr[i].take.free));
			json_array_append_new(ta, t);
		}
		json_object_set_new(root, "tracks", ta);
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "clockMode"))
			clockMode = clamp((int) json_integer_value(j), 0, NCLOCK - 1);
		if (json_t* j = json_object_get(root, "sampleRate"))
			loadRate = (int) json_integer_value(j);
		if (json_t* ta = json_object_get(root, "tracks"))
			for (int i = 0; i < NT && i < (int) json_array_size(ta); i++) {
				json_t* t = json_array_get(ta, i);
				Take k;
				if (json_t* j = json_object_get(t, "len")) k.len = (int) json_integer_value(j);
				if (json_t* j = json_object_get(t, "bars")) k.bars = (int) json_integer_value(j);
				if (json_t* j = json_object_get(t, "free")) k.free = json_boolean_value(j);
				loadTake[i] = k;
			}
	}
};

// ---------------------------------------------------------------- the display: four loops
struct Display : TransparentWidget {
	Schleife* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		auto bold = ui::boldFont();
		NVGcontext* vg = args.vg;
		float w = box.size.x, h = box.size.y;
		float rowh = (h - 12.f) / NT;
		float bx = 70.f, bw = w - 70.f - 52.f;
		bool blink = std::fmod(system::getTime(), 0.5) < 0.25;
		for (int i = 0; i < NT; i++) {
			float cy = 6.f + rowh * (i + 0.5f);
			int st = EMPTY, bars = 2;
			float prog = -1.f;
			bool free = false, clk = true;
			float secs = 0.f;
			if (module) {
				const Track& t = module->tr[i];
				st = t.state;
				clk = module->clocked();
				free = t.take.free || (st == EMPTY && !clk) || (st == ARMED && !clk);
				bars = (st == EMPTY || st == ARMED) ? module->barsKnob(i) : t.take.bars;
				prog = module->progress(i);
				secs = (st == REC ? t.pos : t.take.len) / module->sampleRate;
			}
			else {
				static const int DS[NT] = {PLAY, DUB, EMPTY, REC};
				static const float DP[NT] = {0.62f, 0.31f, -1.f, 0.08f};
				static const int DB[NT] = {2, 4, 1, 8};
				st = DS[i];
				prog = DP[i];
				bars = DB[i];
			}
			NVGcolor col = st == REC || st == ARMED ? C_REC : st == DUB ? C_DUB : C_PLAY;
			ui::text(vg, bold, 12.f, cy + 3.f, string::f("T%d", i + 1), 9.f, ACC, NVG_ALIGN_LEFT, 0.8f);
			std::string bt;
			if (free)
				bt = (st == PLAY || st == DUB || st == REC) ? string::f("%.1f S", secs) : "FREE";
			else
				bt = string::f(bars == 1 ? "%d BAR" : "%d BARS", bars);
			ui::text(vg, bold, 30.f, cy + 2.6f, bt, 7.f, ui::DIM, NVG_ALIGN_LEFT, 0.8f);
			// the bar with its bar lines
			nvgBeginPath(vg);
			nvgRoundedRect(vg, bx, cy - 3.5f, bw, 7.f, 1.8f);
			nvgFillColor(vg, C_BAR);
			nvgFill(vg);
			if (!free && bars > 1)
				for (int k = 1; k < bars; k++) {
					nvgBeginPath(vg);
					nvgRect(vg, bx + bw * k / bars - 0.3f, cy - 3.5f, 0.6f, 7.f);
					nvgFillColor(vg, C_DARK);
					nvgFill(vg);
				}
			if (prog >= 0.f) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, bx, cy - 3.5f, std::max(2.f, bw * prog), 7.f, 1.8f);
				nvgFillColor(vg, nvgTransRGBAf(col, 0.8f));
				nvgFill(vg);
				nvgBeginPath(vg);
				nvgRect(vg, bx + bw * prog - 0.6f, cy - 5.3f, 1.2f, 10.6f);
				nvgFillColor(vg, ui::TXT);
				nvgFill(vg);
			}
			std::string sn = STATE_NAME[st];
			NVGcolor sc = (st == EMPTY || st == PLAY) ? ui::DIM : col;
			if (st == ARMED && !blink)
				sc = nvgTransRGBAf(sc, 0.35f);
			if (module && st == REC && module->tr[i].dubAfter)
				sn = "REC>DUB";
			if (module && clk && !module->running && (st == PLAY || st == DUB) && !module->tr[i].take.free)
				sn = "STOP";
			ui::text(vg, bold, w - 12.f, cy + 2.6f, sn, 7.f, sc, NVG_ALIGN_RIGHT, 0.8f);
		}
	}
};

// ---------------------------------------------------------------- widget
template <int R, int G, int B>
using Bez = VCVLightBezel<ui::AccentLight<R, G, B>>;
#define ACC_ 0x8f, 0xd3, 0xff

struct SchleifeWidget : ModuleWidget {
	SchleifeWidget(Schleife* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Schleife.svg")));
		follow::addOutLight(this, module); // MIX L/R glow while MIX takes them without a cable
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		typedef Schleife M;
		auto at = [](const float* p) { return Vec(p[0], p[1]); };

		Display* d = new Display;
		d->module = module;
		d->box = Rect(L::DISPLAY[0], L::DISPLAY[1], L::DISPLAY[2], L::DISPLAY[3]);
		addChild(d);

		genra_look::PickField* pf = createWidget<genra_look::PickField>(Vec(L::PRESETF[0], L::PRESETF[1]));
		pf->box.size = Vec(L::PRESETF[2], L::PRESETF[3]);
		pf->accent = ACC;
		pf->title = "Clock resolution (as GENRA's CLOCK output)";
		pf->count = []() { return NCLOCK; };
		pf->name = [](int i) { return std::string(CLOCK_NAME[clamp(i, 0, NCLOCK - 1)]); };
		pf->current = [=]() { return module ? module->clockMode : 0; };
		pf->choose = [=](int i) {
			if (module)
				module->clockMode = clamp(i, 0, NCLOCK - 1);
		};
		addChild(pf);

		for (int i = 0; i < NT; i++) {
			addParam(createParamCentered<GKnob>(at(L::BARS[i]), module, M::BARS_PARAM + i));
			addParam(createParamCentered<GSmallKnob>(at(L::LVL[i]), module, M::LEVEL_PARAM + i));
			addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(at(L::REC[i]), module, M::REC_PARAM + i,
			                                                                     M::REC_LIGHT + 3 * i));
			addParam(createLightParamCentered<Bez<ACC_>>(at(L::UNDO[i]), module, M::UNDO_PARAM + i, M::UNDO_LIGHT + i));
			addParam(createLightParamCentered<Bez<ACC_>>(at(L::CLR[i]), module, M::CLEAR_PARAM + i, M::CLEAR_LIGHT + i));
			addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(at(L::STAT[i]), module, M::STAT_LIGHT + 3 * i));
			addInput(createInputCentered<PJ301MPort>(at(L::IN[i]), module, M::TRACK_INPUT + i));
			addOutput(createOutputCentered<PJ301MPort>(at(L::OUT[i]), module, M::TRACK_OUTPUT + i));
		}
		addParam(createParamCentered<GSmallKnob>(at(L::MASTER), module, M::MASTER_PARAM));
		addParam(createLightParamCentered<Bez<ACC_>>(at(L::CLRALL), module, M::CLRALL_PARAM, M::CLRALL_LIGHT));
		addInput(createInputCentered<PJ301MPort>(at(L::CLOCK), module, M::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::RESET), module, M::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MAININ), module, M::MAIN_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::MIXL), module, M::MIXL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(at(L::MIXR), module, M::MIXR_OUTPUT));
	}

	static std::string askFile(const char* name) {
		osdialog_filters* filters = osdialog_filters_parse("WAV:wav");
		char* p = osdialog_file(OSDIALOG_SAVE, NULL, name, filters);
		osdialog_filters_free(filters);
		if (!p)
			return "";
		std::string s = p;
		std::free(p);
		if (string::lowercase(system::getExtension(s)) != ".wav")
			s += ".wav";
		return s;
	}

	void appendContextMenu(Menu* menu) override {
		Schleife* m = dynamic_cast<Schleife*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem(
		    "Clock resolution", std::vector<std::string>(CLOCK_NAME, CLOCK_NAME + NCLOCK),
		    [=]() -> size_t { return (size_t) m->clockMode; }, [=](size_t i) { m->clockMode = (int) i; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Export (WAV)"));
		for (int i = 0; i < NT; i++) {
			bool has = m->hasLoop(i);
			menu->addChild(createMenuItem(string::f("Track %d…", i + 1), has ? "" : "empty", [=]() {
				std::string p = askFile(string::f("schleife-track%d.wav", i + 1).c_str());
				if (!p.empty())
					m->exportTrack(i, p);
			}, !has));
		}
		menu->addChild(createMenuItem("All tracks into a folder…", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
			if (!p)
				return;
			std::string dir = p;
			std::free(p);
			for (int i = 0; i < NT; i++)
				m->exportTrack(i, system::join(dir, string::f("schleife-track%d.wav", i + 1)));
		}));
		menu->addChild(createMenuItem("Mix (with LEVEL and MASTER)…", "", [=]() {
			std::string p = askFile("schleife-mix.wav");
			if (!p.empty())
				m->exportMix(p);
		}));
		menu->addChild(createMenuLabel(string::f("Loops are saved with the patch (up to %.0f s each)", MAX_SEC)));
	}
};

} // namespace schleife

Model* modelSchleife = createModel<schleife::Schleife, schleife::SchleifeWidget>("GenraSchleife");
