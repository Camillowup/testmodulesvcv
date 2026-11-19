// SPDX-License-Identifier: GPL-3.0-only
// GENRA NEON – the note handling the newer neon modules share (WOGE, PULS, GLAS, FLUT): poly (one voice per channel),
// mono and legato (the last note pressed wins, the one before comes back when it is let go). Plain C++ without Rack.
#pragma once
#include <algorithm>

namespace neon {

enum VoiceMode { MODE_POLY, MODE_MONO, MODE_LEGATO, NUM_MODES };
static const char* const MODE_NAMES[NUM_MODES] = {"POLY", "MONO", "LEGATO"};

/** One channel of the GENRA strip (V/OCT, GATE, VEL, ACC, SLIDE). */
struct Channel {
	float voct = 0.f, vel = 0.8f;
	bool gate = false, acc = false, slide = false;
};

/** The notes: call step() once per sample; it says which voice starts, holds or lets go. */
struct Notes {
	static const int MAXCH = 16;
	int order[MAXCH] = {};
	int held = 0;
	bool lastGate[MAXCH] = {};
	int monoCh = -1;
	bool monoGate = false;

	void reset() {
		held = 0;
		monoCh = -1;
		monoGate = false;
		for (bool& g : lastGate)
			g = false;
	}
	void drop(int c) {
		for (int i = 0; i < held; i++)
			if (order[i] == c) {
				for (int j = i; j < held - 1; j++)
					order[j] = order[j + 1];
				held--;
				return;
			}
	}
	/** start(voice, channel, overlap): a note begins (overlap: in mono, a note was still held = legato / glide);
	 * hold(voice, channel): the note is held (pitch, slide, accent may change); off(voice): the voice is let go. */
	template <typename Start, typename Hold, typename Off>
	void step(int mode, const Channel* ch, int n, int maxv, Start start, Hold hold, Off off) {
		n = std::min(n, (int) MAXCH);
		if (mode == MODE_POLY) {
			for (int c = 0; c < std::min(n, maxv); c++) {
				bool g = ch[c].gate;
				if (g && !lastGate[c])
					start(c, c, false);
				if (g)
					hold(c, c);
				else if (lastGate[c])
					off(c);
				lastGate[c] = g;
			}
			for (int c = std::min(n, maxv); c < MAXCH; c++) {
				if (lastGate[c] && c < maxv)
					off(c);
				lastGate[c] = false;
			}
			return;
		}
		for (int c = 0; c < n; c++) {
			bool g = ch[c].gate;
			if (g && !lastGate[c]) {
				drop(c);
				if (held < MAXCH)
					order[held++] = c;
			} else if (!g && lastGate[c])
				drop(c);
			lastGate[c] = g;
		}
		for (int c = n; c < MAXCH; c++) {
			if (lastGate[c])
				drop(c);
			lastGate[c] = false;
		}
		int want = held > 0 ? order[held - 1] : -1;
		if (want >= 0) {
			if (want != monoCh || !monoGate)
				start(0, want, monoGate);
			hold(0, want);
			monoCh = want;
			monoGate = true;
		} else {
			if (monoGate)
				off(0);
			monoGate = false;
			monoCh = -1;
		}
	}
};

/** The tempo from a clock input (CLOCK with ppqn pulses per quarter), and a beat phase that follows it. */
struct Tempo {
	float since = 0.f, bpm = 120.f;
	bool seen = false;
	double beats = 0.0;  // the running beat count (reset: 0)
	int ticks = 0;
	/** dt: sample time; tick: a clock edge came; connected: CLOCK is patched (else 120 BPM, free). */
	void step(float dt, bool tick, bool reset, bool connected, int ppqn) {
		since += dt;
		if (reset) {
			seen = false;
			since = 0.f;
			beats = 0.0;
			ticks = 0;
		}
		if (tick) {
			if (seen && since > 0.005f && since < 4.f)
				bpm = std::min(std::max(60.f / (since * ppqn), 20.f), 400.f);
			seen = true;
			since = 0.f;
			// lock the beat count to the clock: every tick is 1/ppqn beat
			beats = (double) ticks / ppqn;
			ticks++;
		} else
			beats += bpm / 60.0 * dt;
		if (!connected) {
			bpm = 120.f;
			seen = false;
		}
	}
};

}  // namespace neon
