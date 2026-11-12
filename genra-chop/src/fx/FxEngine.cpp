// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP FX: the effects. Own code, no parts from elsewhere.
#include "FxEngine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fx {

static const float PI = 3.14159265358979f;

const char* const GROUP_NAMES[NUM_GROUPS] = {"TEXTURE", "FILTER", "DRIVE", "SPACE", "MOD", "PERFORM"};
const char* const ROUTE_NAMES[3] = {"1 > 2", "2 > 1", "1 + 2"};

// clang-format off
const Info FX[NUM_FX] = {
	{"VINYL SIM",     G_TEXTURE, {"NOISE", "WOW", "AGE"},        {0.4f, 0.3f, 0.4f},  false},
	{"CASSETTE SIM",  G_TEXTURE, {"SAT", "WOBBLE", "TONE"},      {0.4f, 0.3f, 0.4f},  false},
	{"LO-FI",         G_TEXTURE, {"BITS", "RATE", "TONE"},       {0.5f, 0.3f, 0.3f},  false},
	{"WOW & FLUTTER", G_TEXTURE, {"DEPTH", "RATE", "MIX"},       {0.4f, 0.3f, 1.0f},  false},
	{"RADIO",         G_TEXTURE, {"BAND", "WIDTH", "DIRT"},      {0.45f, 0.3f, 0.3f}, false},
	{"DUST",          G_TEXTURE, {"CRACKLE", "HISS", "POPS"},    {0.5f, 0.3f, 0.2f},  false},
	{"WARP",          G_TEXTURE, {"DEPTH", "SPEED", "MIX"},      {0.5f, 0.3f, 1.0f},  false},
	{"FILTER+DRIVE",  G_FILTER,  {"CUTOFF", "RESO", "DRIVE"},    {0.5f, 0.3f, 0.3f},  false},
	{"SUPER FILTER",  G_FILTER,  {"CUTOFF", "RESO", "SWEEP"},    {0.5f, 0.5f, 0.5f},  false},
	{"ISOLATOR",      G_FILTER,  {"LOW", "MID", "HIGH"},         {0.5f, 0.5f, 0.5f},  false},
	{"RESONATOR",     G_FILTER,  {"TUNE", "RING", "MIX"},        {0.0f, 0.6f, 0.5f},  false},
	{"TALK FILTER",   G_FILTER,  {"VOWEL", "RESO", "MIX"},       {0.0f, 0.5f, 1.0f},  false},
	{"AUTO WAH",      G_FILTER,  {"SENS", "RANGE", "RESO"},      {0.5f, 0.6f, 0.4f},  false},
	{"SATURATOR",     G_DRIVE,   {"DRIVE", "TONE", "MIX"},       {0.4f, 0.7f, 1.0f},  false},
	{"FUZZ",          G_DRIVE,   {"FUZZ", "TONE", "LEVEL"},      {0.5f, 0.5f, 0.6f},  false},
	{"WAVEFOLDER",    G_DRIVE,   {"FOLD", "BIAS", "MIX"},        {0.3f, 0.0f, 1.0f},  false},
	{"COMPRESSOR",    G_DRIVE,   {"AMOUNT", "SPEED", "MIX"},     {0.5f, 0.5f, 1.0f},  false},
	{"GATER",         G_DRIVE,   {"RATE", "DEPTH", "SMOOTH"},    {0.6f, 1.0f, 0.2f},  false},
	{"DELAY",         G_SPACE,   {"TIME", "FEEDBACK", "MIX"},    {0.65f, 0.4f, 0.4f}, true},
	{"TAPE ECHO",     G_SPACE,   {"TIME", "FEEDBACK", "MIX"},    {0.65f, 0.5f, 0.4f}, true},
	{"PING PONG",     G_SPACE,   {"TIME", "FEEDBACK", "MIX"},    {0.45f, 0.5f, 0.4f}, true},
	{"REVERSE DELAY", G_SPACE,   {"TIME", "FEEDBACK", "MIX"},    {0.75f, 0.3f, 0.5f}, true},
	{"REVERB",        G_SPACE,   {"SIZE", "TONE", "MIX"},        {0.5f, 0.5f, 0.35f}, true},
	{"CHORUS",        G_MOD,     {"RATE", "DEPTH", "MIX"},       {0.3f, 0.5f, 0.5f},  false},
	{"FLANGER",       G_MOD,     {"RATE", "DEPTH", "FEEDBACK"},  {0.2f, 0.6f, 0.5f},  false},
	{"PHASER",        G_MOD,     {"RATE", "DEPTH", "FEEDBACK"},  {0.25f, 0.7f, 0.4f}, false},
	{"RING MOD",      G_MOD,     {"FREQ", "WOBBLE", "MIX"},      {0.4f, 0.0f, 0.5f},  false},
	{"TREMOLO / PAN", G_MOD,     {"RATE", "DEPTH", "PAN"},       {0.4f, 0.6f, 0.0f},  false},
	{"VIBRATO",       G_MOD,     {"RATE", "DEPTH", "MIX"},       {0.4f, 0.4f, 1.0f},  false},
	{"LOOPER",        G_PERFORM, {"LENGTH", "SPEED", "MIX"},     {0.5f, 0.5f, 1.0f},  false},
	{"SCATTER",       G_PERFORM, {"AMOUNT", "SLICE", "MIX"},     {0.6f, 0.5f, 1.0f},  false},
	{"STOPPER",       G_PERFORM, {"PATTERN", "DEPTH", "SMOOTH"}, {0.0f, 1.0f, 0.2f},  false},
	{"TAPE STOP",     G_PERFORM, {"TIME", "CURVE", "MIX"},       {0.3f, 0.5f, 1.0f},  false},
	{"BACKSPIN",      G_PERFORM, {"TIME", "SPEED", "MIX"},       {0.3f, 0.5f, 1.0f},  false},
	{"TAPE START",    G_PERFORM, {"TIME", "CURVE", "MIX"},       {0.3f, 0.5f, 1.0f},  false},
	{"DOWNER",        G_PERFORM, {"TIME", "DEPTH", "MIX"},       {0.4f, 0.5f, 1.0f},  false},
	{"SUB OCTAVER",   G_PERFORM, {"SUB", "TONE", "OCT"},         {0.5f, 0.4f, 0.0f},  false},
	{"LOW CUT",       G_FILTER,  {"TIME", "RANGE", "RESO"},      {0.4f, 0.6f, 0.3f},  false},
	{"HIGH CUT",      G_FILTER,  {"TIME", "RANGE", "RESO"},      {0.4f, 0.6f, 0.3f},  false},
};
// clang-format on

const float DIV_BEATS[NUM_DIVS] = {0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.375f, 0.5f, 2.f / 3.f, 0.75f, 1.f, 1.5f, 2.f, 4.f};
const char* const DIV_NAMES[NUM_DIVS] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1 BAR"};

// gater: steps per bar; looper: beats (the last: FREEZE); scatter: beats per slice
static const int NUM_GATE = 6;
static const float GATE_STEPS[NUM_GATE] = {4.f, 8.f, 12.f, 16.f, 24.f, 32.f};
static const char* const GATE_NAMES[NUM_GATE] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const int NUM_LOOP = 7;
static const float LOOP_BEATS[NUM_LOOP] = {0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 0.f};
static const char* const LOOP_NAMES[NUM_LOOP] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "FREEZE"};
static const int NUM_SLICE = 3;
static const float SLICE_BEATS[NUM_SLICE] = {0.25f, 0.5f, 1.f};
static const char* const SLICE_NAMES[NUM_SLICE] = {"1/16", "1/8", "1/4"};
static const int NUM_STOP = 8;
static const char* const STOP_PAT[NUM_STOP] = {"xxxxxxxxxxxx....", "xxxxxx..xxxxxx..", "xxx.xxx.xxx.xx..", "x.x.x.x.x.x.x.x.",
                                               "xx..xx..xx..x.x.", "xxxxxxxx........", "x..x..x...x.x...", "xxxx.xxx..xx.x.."};
static const char* const NOTE_NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const float VOWEL_F[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};

static int pick(float v, int n) { return std::max(0, std::min(n - 1, (int) std::lround(v * (n - 1)))); }
static float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }
static float ftanh(float x) {
	x = clampf(x, -3.f, 3.f);
	return x * (27.f + x * x) / (27.f + 9.f * x * x);
}
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float onePoleA(float fc, float sr) { return 1.f - std::exp(-2.f * PI * std::min(fc, 0.45f * sr) / sr); }
static float logcosh(float x) {
	float a = std::fabs(x);
	return a + std::log1p(std::exp(-2.f * a)) - 0.69314718f;
}
static float cutoff(float v) { return 40.f * std::pow(450.f, v); } // 40 Hz .. 18 kHz

std::string valueText(int f, int k, float v) {
	char b[32];
	auto pct = [&]() {
		snprintf(b, sizeof(b), "%d%%", (int) std::lround(v * 100.f));
		return std::string(b);
	};
	if (f < 0 || f >= NUM_FX)
		return pct();
	switch (f) {
		case LOFI:
			if (k == 0) {
				snprintf(b, sizeof(b), "%d BIT", (int) std::lround(16.f - v * 13.f));
				return b;
			}
			if (k == 1) {
				snprintf(b, sizeof(b), "1/%d", 1 + (int) std::lround(v * v * 39.f));
				return b;
			}
			break;
		case RADIO:
			if (k == 0) {
				snprintf(b, sizeof(b), "%d HZ", (int) (300.f * std::pow(10.f, v)));
				return b;
			}
			break;
		case FILTDRIVE:
		case SUPERFILT:
			if (k == 0) {
				float hz = cutoff(v);
				if (hz >= 1000.f)
					snprintf(b, sizeof(b), "%.1f KHZ", hz / 1000.f);
				else
					snprintf(b, sizeof(b), "%d HZ", (int) hz);
				return b;
			}
			break;
		case ISOLATOR: {
			if (v < 0.02f)
				return "KILL";
			float g = v < 0.5f ? (v / 0.5f) * (v / 0.5f) : 1.f + (v - 0.5f) * 2.f;
			snprintf(b, sizeof(b), "%+.1f DB", 20.f * std::log10(g));
			return b;
		}
		case RESONATOR:
			if (k == 0) {
				int n = (int) std::lround(v * 24.f);
				snprintf(b, sizeof(b), "%s%d", NOTE_NM[n % 12], 2 + n / 12);
				return b;
			}
			break;
		case TALK:
			if (k == 0) {
				const char* V[5] = {"A", "E", "I", "O", "U"};
				float p = v * 4.f;
				int i = std::min(3, (int) p);
				float fr = p - i;
				if (fr < 0.2f)
					return V[i];
				if (fr > 0.8f)
					return V[i + 1];
				snprintf(b, sizeof(b), "%s>%s", V[i], V[i + 1]);
				return b;
			}
			break;
		case COMP:
			if (k == 0) {
				snprintf(b, sizeof(b), "%d DB", (int) std::lround(-6.f - 34.f * v));
				return b;
			}
			break;
		case GATER:
			if (k == 0)
				return GATE_NAMES[pick(v, NUM_GATE)];
			break;
		case DELAY:
		case TAPEECHO:
		case PINGPONG:
		case REVDELAY:
			if (k == 0)
				return DIV_NAMES[pick(v, NUM_DIVS)];
			break;
		case RINGMOD:
			if (k == 0) {
				snprintf(b, sizeof(b), "%d HZ", (int) (20.f * std::pow(100.f, v)));
				return b;
			}
			break;
		case LOOPER:
			if (k == 0)
				return LOOP_NAMES[pick(v, NUM_LOOP)];
			if (k == 1) {
				snprintf(b, sizeof(b), "X%.2f", std::pow(2.f, (v - 0.5f) * 2.f));
				return b;
			}
			break;
		case SCATTER:
			if (k == 1)
				return SLICE_NAMES[pick(v, NUM_SLICE)];
			break;
		case STOPPER:
			if (k == 0) {
				snprintf(b, sizeof(b), "PAT %d", pick(v, NUM_STOP) + 1);
				return b;
			}
			break;
		case TAPESTOP:
		case BACKSPIN:
		case TAPESTART:
			if (k == 0) {
				snprintf(b, sizeof(b), "%.1f S", 0.1f + 2.9f * v * v);
				return b;
			}
			break;
		case DOWNER:
			if (k == 0) {
				snprintf(b, sizeof(b), "%.1f S", 0.3f + 5.7f * v * v);
				return b;
			}
			if (k == 1) {
				snprintf(b, sizeof(b), "-%d ST", (int) std::lround(v * 24.f));
				return b;
			}
			break;
		case SUBOCT:
			if (k == 2)
				return v < 0.5f ? "-1 OCT" : "-2 OCT";
			break;
		case LOWCUT:
		case HIGHCUT:
			if (k == 0) {
				snprintf(b, sizeof(b), "%.1f S", 0.2f + 3.8f * v * v);
				return b;
			}
			if (k == 1) {
				float hz = f == LOWCUT ? 200.f * std::pow(20.f, v) : 3000.f * std::pow(0.05f, v);
				if (hz >= 1000.f)
					snprintf(b, sizeof(b), "%.1f KHZ", hz / 1000.f);
				else
					snprintf(b, sizeof(b), "%d HZ", (int) hz);
				return b;
			}
			break;
	}
	return pct();
}

// ---------------------------------------------------------------- building blocks
void SVF::set(float fc, float q, float sr) {
	float g = std::tan(PI * clampf(fc, 10.f, 0.45f * sr) / sr);
	k = 1.f / std::max(0.3f, q);
	a1 = 1.f / (1.f + g * (g + k));
	a2 = g * a1;
	a3 = g * a2;
}

static const int REV_BASE[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int REV_AP[4] = {142, 107, 379, 277};

Bus::Bus(int ringPow) {
	ring[0].init(ringPow);
	ring[1].init(ringPow);
	setSampleRate(44100.f);
}

void Bus::setSampleRate(float s) {
	sr = s;
	float sc = sr / 44100.f;
	for (int i = 0; i < 8; i++) {
		rv[i].assign((size_t) (REV_BASE[i] * 1.7f * sc) + 8, 0.f);
		rvW[i] = 0;
	}
	for (int i = 0; i < 4; i++) {
		rvApLen[i] = std::max(1, (int) (REV_AP[i] * sc));
		rvAp[i].assign((size_t) rvApLen[i] + 1, 0.f);
		rvApW[i] = 0;
	}
	cs[0] = -1.f; // coefficients anew
	if (cur >= 0)
		reset();
}

/** The effects that put the live input into the ring themselves are the echoes (and the flanger, with feedback); for
 *  every other the bus writes the live input, so a loop or a spin has what was just played. */
bool Bus::writesLive() const {
	return !(cur == DELAY || cur == TAPEECHO || cur == PINGPONG || cur == REVDELAY || cur == FLANGER);
}

void Bus::reset() {
	for (int ch = 0; ch < 2; ch++) {
		for (int i = 0; i < 3; i++)
			f[ch][i].reset();
		for (int i = 0; i < 4; i++)
			lp[ch][i] = 0.f;
		for (int i = 0; i < 6; i++)
			ap[ch][i] = 0.f;
		hp[ch][0] = hp[ch][1] = dc[ch][0] = dc[ch][1] = held[ch] = adaa[ch] = env[ch] = 0.f;
		subState[ch] = subLast[ch] = 0.f;
		subSign[ch] = 1.f;
		rp[ch] = 0.0;
	}
	if (!writesLive()) {
		// the echoes start empty
		ring[0].clear();
		ring[1].clear();
	}
	if (cur == RESONATOR)
		std::memset(comb, 0, sizeof(comb));
	if (cur == REVERB) {
		for (auto& v : rv)
			std::fill(v.begin(), v.end(), 0.f);
		for (auto& v : rvAp)
			std::fill(v.begin(), v.end(), 0.f);
		for (float& x : rvLp)
			x = 0.f;
	}
	ph = ph2 = 0.0;
	frozen = false;
	holdCount = 0;
	loopLen = 0;
	sliceLeft = 0;
	slicePlay = 0;
	rate = 1.f;
	t0 = 2.f; // the one-shots wait for ON
	oscC = 1.f, oscS = 0.f;
	fdel = -1.f;
	cs[0] = -1.f;
	blockLeft = 0; // the coefficients at once
}

/** Coefficients from the controls (and the tempo, the LFOs of the filters): every 16 samples, only when needed. */
void Bus::prep(const Tempo& t) {
	for (int i = 0; i < 3; i++)
		cs[i] = c[i];
	spbSeen = t.spb;
	const float a = c[0], b = c[1], d = c[2];
	switch (cur) {
		case VINYL:
			k1 = a;
			k2 = b * b * 0.0025f * sr;
			for (int ch = 0; ch < 2; ch++) {
				f[ch][0].set(20.f + 200.f * d, 0.707f, sr);
				f[ch][1].set(16000.f * std::pow(3000.f / 16000.f, d), 0.707f, sr);
			}
			break;
		case CASSETTE:
			k1 = 1.f + 5.f * a;
			k2 = b * 0.002f * sr;
			k3 = b * 0.00025f * sr;
			k4 = d * 0.004f;
			for (int ch = 0; ch < 2; ch++)
				f[ch][1].set(15000.f * std::pow(4000.f / 15000.f, d), 0.707f, sr);
			break;
		case LOFI:
			k1 = std::pow(2.f, 15.f - a * 13.f);
			k2 = 1.f + std::round(b * b * 39.f);
			for (int ch = 0; ch < 2; ch++)
				f[ch][1].set(20000.f * std::pow(2000.f / 20000.f, d), 0.707f, sr);
			break;
		case WOWFLUT:
			k1 = a * 0.004f * sr;
			k2 = 0.2f * std::pow(40.f, b);
			break;
		case RADIO:
			for (int ch = 0; ch < 2; ch++)
				f[ch][0].set(300.f * std::pow(10.f, a), 4.f - 3.4f * b, sr);
			k1 = 1.f + 3.f * d;
			break;
		case WARP:
			k1 = a * 0.006f * sr;
			k2 = 0.3f + 1.2f * b;
			break;
		case FILTDRIVE:
			for (int ch = 0; ch < 2; ch++)
				f[ch][0].set(cutoff(a), 0.5f + 11.5f * b * b, sr);
			k1 = 1.f + 7.f * d;
			break;
		case SUPERFILT: {
			float sweep = d * 3.f * std::sin(2.f * PI * (float) t.barPos);
			for (int ch = 0; ch < 2; ch++)
				f[ch][0].set(cutoff(a) * std::pow(2.f, sweep), 0.6f + 14.f * b * b, sr);
			break;
		}
		case ISOLATOR: {
			for (int ch = 0; ch < 2; ch++) {
				f[ch][0].set(250.f, 0.707f, sr);
				f[ch][1].set(2500.f, 0.707f, sr);
			}
			auto gn = [](float v) { return v < 0.02f ? 0.f : (v < 0.5f ? (v / 0.5f) * (v / 0.5f) : 1.f + (v - 0.5f) * 2.f); };
			k1 = gn(a), k2 = gn(b), k3 = gn(d);
			break;
		}
		case RESONATOR: {
			float hz = 65.406f * std::pow(2.f, std::round(a * 24.f) / 12.f);
			k1 = sr / hz;
			k2 = 0.5f + 0.495f * b;
			k3 = d;
			break;
		}
		case TALK: {
			float p = a * 4.f;
			int i = std::min(3, (int) p);
			float fr = p - i;
			for (int ch = 0; ch < 2; ch++)
				for (int q = 0; q < 3; q++)
					f[ch][q].set(lerp(VOWEL_F[i][q], VOWEL_F[i + 1][q], fr), 4.f + 16.f * b, sr);
			k3 = d;
			break;
		}
		case AUTOWAH: {
			float e = clampf((env[0] + env[1]) * 0.5f * (1.f + 20.f * a), 0.f, 1.f);
			float fc = 250.f * std::pow(2.f, b * 5.f * e);
			for (int ch = 0; ch < 2; ch++)
				f[ch][0].set(fc, 1.f + 9.f * d, sr);
			break;
		}
		case SATURATOR:
			k1 = 1.f + 19.f * a * a;
			k2 = 1.f / std::sqrt(1.f + 0.25f * (k1 - 1.f));
			k3 = onePoleA(3000.f * std::pow(20000.f / 3000.f, b), sr);
			break;
		case FUZZ:
			k1 = 2.f + 58.f * a * a;
			k2 = onePoleA(1500.f * std::pow(10.f, b), sr);
			k3 = 1.2f * d;
			break;
		case FOLDER:
			k1 = 1.f + 7.f * a;
			k2 = b * 2.f;
			break;
		case COMP:
			k1 = -6.f - 34.f * a;                               // threshold dB
			k2 = 2.f + 8.f * a;                                 // ratio
			k3 = std::exp(-1.f / ((0.030f - 0.029f * b) * sr)); // attack
			k4 = std::exp(-1.f / ((0.6f - 0.55f * b) * sr));     // release
			k5 = std::pow(10.f, -k1 * (1.f - 1.f / k2) * 0.5f / 20.f);
			break;
		case GATER:
			k1 = GATE_STEPS[pick(a, NUM_GATE)];
			k2 = b;
			k3 = onePoleA(1.f / (0.0005f + 0.015f * d) / (2.f * PI), sr);
			break;
		case DELAY:
		case TAPEECHO:
		case PINGPONG:
		case REVDELAY: {
			float beats = DIV_BEATS[pick(a, NUM_DIVS)];
			float maxD = (float) ring[0].size() * (cur == REVDELAY ? 0.45f : 0.95f);
			k1 = clampf(beats * t.spb, 16.f, maxD);
			if (fdel < 0.f)
				fdel = k1;
			k2 = 0.95f * b;
			if (cur == TAPEECHO)
				k3 = onePoleA(3500.f, sr);
			break;
		}
		case REVERB: {
			float sc = sr / 44100.f * (0.6f + 1.0f * a);
			float t60 = 0.4f + 6.f * a * a;
			for (int i = 0; i < 8; i++) {
				rvLen[i] = std::max(8, std::min((int) rv[i].size() - 2, (int) (REV_BASE[i] * sc)));
				// per line: the gain that makes it die away in t60
				lp[i / 4][i % 4] = std::pow(0.001f, rvLen[i] / (t60 * sr));
			}
			k1 = onePoleA(2000.f * std::pow(8.f, b), sr);
			break;
		}
		case CHORUS:
			k1 = 0.1f * std::pow(30.f, a);
			k2 = b * 0.008f * sr;
			break;
		case FLANGER:
			k1 = 0.05f * std::pow(40.f, a);
			k2 = (0.0002f + b * 0.006f) * sr;
			k3 = 0.9f * d;
			break;
		case PHASER: {
			k1 = 0.05f * std::pow(80.f, a);
			float m = 0.5f + 0.5f * std::sin(2.f * PI * (float) ph);
			float fc = 200.f * std::pow(2.f, b * 4.f * m);
			float tn = std::tan(PI * std::min(fc, 0.45f * sr) / sr);
			k2 = (tn - 1.f) / (tn + 1.f);
			k3 = 0.8f * d;
			break;
		}
		case RINGMOD: {
			float hz = 20.f * std::pow(100.f, a) * (1.f + b * 0.5f * std::sin(2.f * PI * (float) ph2));
			k1 = std::cos(2.f * PI * hz / sr);
			k2 = std::sin(2.f * PI * hz / sr);
			break;
		}
		case TREMOLO:
			k1 = 0.5f * std::pow(24.f, a);
			break;
		case VIBRATO:
			k1 = 1.f + 9.f * a;
			k2 = b * 0.003f * sr;
			break;
		case LOOPER: {
			int i = pick(a, NUM_LOOP);
			k1 = LOOP_BEATS[i] > 0.f ? LOOP_BEATS[i] * t.spb : 0.25f * sr; // FREEZE: a quarter second, held
			k1 = std::min(k1, (float) ring[0].size() * 0.45f);
			k2 = std::pow(2.f, (b - 0.5f) * 2.f);
			k3 = i == NUM_LOOP - 1 ? 1.f : 0.f;
			break;
		}
		case SCATTER:
			k1 = SLICE_BEATS[pick(b, NUM_SLICE)];
			break;
		case STOPPER:
			k1 = (float) pick(a, NUM_STOP);
			k3 = onePoleA(1.f / (0.0005f + 0.015f * d) / (2.f * PI), sr);
			break;
		case TAPESTOP:
		case BACKSPIN:
		case TAPESTART:
			k1 = 1.f / ((0.1f + 2.9f * a * a) * sr);
			k2 = cur == BACKSPIN ? 1.f + 3.f * b : 0.4f + 2.6f * b;
			break;
		case DOWNER:
			k1 = 1.f / ((0.3f + 5.7f * a * a) * sr);
			k2 = std::pow(2.f, -std::round(b * 24.f) / 12.f);
			break;
		case SUBOCT:
			for (int ch = 0; ch < 2; ch++) {
				f[ch][0].set(150.f, 0.707f, sr);
				f[ch][1].set(60.f + 340.f * b, 0.707f, sr);
			}
			break;
		case LOWCUT:
		case HIGHCUT: {
			// the sweep: from wide open to the RANGE target over TIME (t0: 0..1, from FX ON)
			k1 = 1.f / ((0.2f + 3.8f * a * a) * sr);
			float p = std::min(1.f, t0);
			p = p * p * (3.f - 2.f * p);
			float target = cur == LOWCUT ? 200.f * std::pow(20.f, b) : 3000.f * std::pow(0.05f, b);
			float fc = cur == LOWCUT ? 20.f * std::pow(target / 20.f, p) : 20000.f * std::pow(target / 20000.f, p);
			for (int ch = 0; ch < 2; ch++)
				f[ch][0].set(fc, 0.6f + 6.f * d * d, sr);
			break;
		}
	}
}

static bool alwaysPrep(int f) {
	return f == SUPERFILT || f == AUTOWAH || f == PHASER || f == RINGMOD || f == LOWCUT || f == HIGHCUT;
}

void Bus::process(float& L, float& R, const Tempo& t) {
	// ---- the effect changes: out, change, in (5 ms each)
	if (want != cur) {
		if (cur < 0 || sleeping || g <= 0.f) {
			cur = want;
			reset();
			sw = 1.f;
		}
		else {
			sw -= 1.f / (0.005f * sr);
			if (sw <= 0.f) {
				cur = want;
				reset();
				sw = 0.f;
			}
		}
	}
	else if (sw < 1.f)
		sw = std::min(1.f, sw + 1.f / (0.005f * sr));
	if (on && !wasOn)
		trig = true;
	if (retrigger) {
		trig = true;
		retrigger = false;
	}
	wasOn = on;
	// ---- ON / OFF: 8 ms
	float gt = on ? 1.f : 0.f;
	if (g < gt)
		g = std::min(gt, g + 1.f / (0.008f * sr));
	else if (g > gt)
		g = std::max(gt, g - 1.f / (0.008f * sr));
	// the live input goes into the ring (loops, spins and wobbles read it), also while asleep
	if (writesLive() && !frozen) {
		ring[0].push(L);
		ring[1].push(R);
	}
	const Info& in = FX[cur];
	float amt = g * sw;
	if (on || amt > 0.f)
		sleeping = false;
	if (sleeping) {
		keepNow = 1.f;
		return;
	}
	// ---- coefficients
	if (--blockLeft <= 0) {
		blockLeft = 16;
		if (alwaysPrep(cur) || cs[0] != c[0] || cs[1] != c[1] || cs[2] != c[2] || spbSeen != t.spb)
			prep(t);
	}
	if (in.tail) {
		float wL = 0.f, wR = 0.f;
		runSpace(L * amt, R * amt, wL, wR, t);
		float mix = c[2];
		float dry = 1.f - amt * std::max(0.f, 2.f * mix - 1.f);
		float wet = std::min(1.f, 2.f * mix);
		keepNow = dry;
		L = L * dry + wL * wet;
		R = R * dry + wR * wet;
		if (!on) {
			if (std::fabs(wL) + std::fabs(wR) < 1e-4f) { // -80 dB: gone
				if (++quiet > (int) (0.25f * sr))
					sleeping = true;
			}
			else
				quiet = 0;
		}
		else
			quiet = 0;
		return;
	}
	if (amt <= 0.f) {
		keepNow = 1.f;
		if (!on) {
			sleeping = true;
			if (frozen)
				frozen = false; // the looper lets go of the ring
		}
		return;
	}
	keepNow = 1.f - amt;
	float yL = L, yR = R;
	runInsert(yL, yR, t);
	L += (yL - L) * amt;
	R += (yR - R) * amt;
}

// ---------------------------------------------------------------- the effects that replace the signal
void Bus::runInsert(float& L, float& R, const Tempo& t) {
	float x[2] = {L, R};
	float y[2] = {L, R};
	const float a = c[0], b = c[1], d = c[2];
	(void) a;
	switch (cur) {
		case VINYL: {
			ph += 0.55 / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			float wow = k2 * (0.5f + 0.5f * std::sin(2.f * PI * (float) ph));
			// crackle (mono, as a record's), a little hiss
			float cr = 0.f;
			if (noise() > 1.f - k1 * 0.0012f)
				held[0] = noise() * 0.35f * k1;
			cr = held[0];
			held[0] *= 0.55f;
			float hiss = noise() * 0.0025f * k1;
			for (int ch = 0; ch < 2; ch++) {
				float v = ring[ch].tap(1.f + 0.003f * sr + wow);
				float l, bp, h;
				f[ch][0].run(v, l, bp, h);
				f[ch][1].run(h, l, bp, h);
				y[ch] = l + cr + hiss;
			}
			break;
		}
		case CASSETTE: {
			ph += 0.8 / sr;
			ph2 += 9.0 / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			if (ph2 >= 1.0)
				ph2 -= 1.0;
			float m = k2 * (0.5f + 0.5f * std::sin(2.f * PI * (float) ph)) + k3 * (0.5f + 0.5f * std::sin(2.f * PI * (float) ph2));
			float norm = 1.f / std::pow(k1, 0.6f);
			for (int ch = 0; ch < 2; ch++) {
				float v = ring[ch].tap(1.f + 0.004f * sr + m);
				v = ftanh(v * k1) * norm;
				float l, bp, h;
				f[ch][1].run(v, l, bp, h);
				y[ch] = l + noise() * k4;
			}
			break;
		}
		case LOFI:
			if (--holdCount <= 0) {
				holdCount = (int) k2;
				held[0] = std::round(x[0] * k1) / k1;
				held[1] = std::round(x[1] * k1) / k1;
			}
			for (int ch = 0; ch < 2; ch++) {
				float l, bp, h;
				f[ch][1].run(held[ch], l, bp, h);
				y[ch] = l;
			}
			break;
		case WOWFLUT:
		case WARP: {
			ph += (cur == WOWFLUT ? k2 : k2 * 0.5f) / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			float s = 0.5f + 0.5f * std::sin(2.f * PI * (float) ph);
			float m = cur == WOWFLUT ? s : s * s * s * s; // a warped record: one dip per turn
			for (int ch = 0; ch < 2; ch++) {
				float v = ring[ch].tap(1.f + (cur == WOWFLUT ? 0.006f : 0.008f) * sr + k1 * m);
				y[ch] = lerp(x[ch], v, d);
			}
			break;
		}
		case RADIO: {
			float mono = 0.5f * (x[0] + x[1]);
			float l, bp, h;
			f[0][0].run(mono, l, bp, h);
			float v = ftanh(bp * f[0][0].k * 1.6f * k1) / std::sqrt(k1) + noise() * 0.012f * d;
			y[0] = y[1] = v;
			break;
		}
		case DUST: {
			float add = 0.f;
			if (noise() > 1.f - a * 0.0015f)
				held[0] = noise() * 0.3f;
			add += held[0];
			held[0] *= 0.5f;
			if (noise() > 1.f - d * 0.00004f)
				held[1] = (noise() > 0.f ? 1.f : -1.f) * 0.6f * d;
			lp[0][0] += (held[1] - lp[0][0]) * 0.02f; // a pop: low and round
			held[1] *= 0.995f;
			add += lp[0][0];
			add += noise() * 0.004f * b;
			y[0] = x[0] + add;
			y[1] = x[1] + add;
			break;
		}
		case FILTDRIVE:
		case SUPERFILT:
			for (int ch = 0; ch < 2; ch++) {
				float v = cur == FILTDRIVE ? ftanh(x[ch] * k1) / std::sqrt(k1) : x[ch];
				float l, bp, h;
				f[ch][0].run(v, l, bp, h);
				y[ch] = l;
			}
			break;
		case ISOLATOR:
			for (int ch = 0; ch < 2; ch++) {
				float lo, bp, h, l2, hi;
				f[ch][0].run(x[ch], lo, bp, h);
				f[ch][1].run(x[ch], l2, bp, hi);
				float mid = x[ch] - lo - hi;
				y[ch] = lo * k1 + mid * k2 + hi * k3;
			}
			break;
		case RESONATOR: {
			static const float RATIO[3] = {1.f, 1.5f, 2.f};
			for (int ch = 0; ch < 2; ch++) {
				float sum = 0.f;
				for (int q = 0; q < 3; q++) {
					float len = std::min(4000.f, k1 / RATIO[q]);
					float pos = (float) combW - len;
					while (pos < 0.f)
						pos += 4096.f;
					int i0 = (int) pos;
					float fr = pos - i0;
					float o = comb[ch][q][i0 & 4095] + (comb[ch][q][(i0 + 1) & 4095] - comb[ch][q][i0 & 4095]) * fr;
					ap[ch][q] += (o - ap[ch][q]) * 0.5f; // damping
					comb[ch][q][combW & 4095] = x[ch] * 0.3f + ap[ch][q] * k2;
					sum += o;
				}
				y[ch] = lerp(x[ch], ftanh(sum * 0.5f), k3);
			}
			combW = (combW + 1) & 4095;
			break;
		}
		case TALK:
			for (int ch = 0; ch < 2; ch++) {
				static const float GN[3] = {1.f, 0.7f, 0.35f};
				float sum = 0.f;
				for (int q = 0; q < 3; q++) {
					float l, bp, h;
					f[ch][q].run(x[ch], l, bp, h);
					sum += bp * f[ch][q].k * GN[q];
				}
				y[ch] = lerp(x[ch], sum * 1.4f, k3);
			}
			break;
		case AUTOWAH:
			for (int ch = 0; ch < 2; ch++) {
				float e = std::fabs(x[ch]);
				env[ch] += (e - env[ch]) * (e > env[ch] ? 0.004f : 0.0002f);
				float l, bp, h;
				f[ch][0].run(x[ch], l, bp, h);
				y[ch] = bp * f[ch][0].k * 1.5f + l * 0.3f;
			}
			break;
		case SATURATOR:
			for (int ch = 0; ch < 2; ch++) {
				float u = x[ch] * k1, u1 = adaa[ch];
				float s = std::fabs(u - u1) < 1e-4f ? ftanh(0.5f * (u + u1)) : (logcosh(u) - logcosh(u1)) / (u - u1);
				adaa[ch] = u;
				lp[ch][0] += (s * k2 - lp[ch][0]) * k3;
				y[ch] = lerp(x[ch], lp[ch][0], d);
			}
			break;
		case FUZZ:
			for (int ch = 0; ch < 2; ch++) {
				// a hard clip with a bias (asymmetric), anti-aliased, then the DC out
				auto F = [](float v) { return std::fabs(v) <= 1.f ? 0.5f * v * v : std::fabs(v) - 0.5f; };
				float u = x[ch] * k1 + 0.25f, u1 = adaa[ch];
				float s = std::fabs(u - u1) < 1e-4f ? clampf(0.5f * (u + u1), -1.f, 1.f) : (F(u) - F(u1)) / (u - u1);
				adaa[ch] = u;
				float o = s - dc[ch][0] + 0.995f * dc[ch][1];
				dc[ch][0] = s, dc[ch][1] = o;
				lp[ch][0] += (o - lp[ch][0]) * k2;
				y[ch] = lp[ch][0] * 0.6f * k3;
			}
			break;
		case FOLDER:
			for (int ch = 0; ch < 2; ch++) {
				float u = x[ch] * k1 + k2, u1 = adaa[ch];
				float s = std::fabs(u - u1) < 1e-4f ? std::sin(0.5f * (u + u1)) : (std::cos(u1) - std::cos(u)) / (u - u1);
				adaa[ch] = u;
				float o = s - dc[ch][0] + 0.995f * dc[ch][1];
				dc[ch][0] = s, dc[ch][1] = o;
				y[ch] = lerp(x[ch], o * 0.7f, d);
			}
			break;
		case COMP: {
			float pk = std::max(std::fabs(x[0]), std::fabs(x[1]));
			env[0] = pk > env[0] ? k3 * env[0] + (1.f - k3) * pk : k4 * env[0] + (1.f - k4) * pk;
			float db = 20.f * std::log10(std::max(1e-6f, env[0]));
			float over = db - k1;
			float gr = over > 0.f ? -over * (1.f - 1.f / k2) : 0.f;
			float gain = std::pow(10.f, gr / 20.f) * k5;
			for (int ch = 0; ch < 2; ch++)
				y[ch] = lerp(x[ch], x[ch] * gain, d);
			break;
		}
		case GATER: {
			double p = t.barPos * k1;
			float ph1 = (float) (p - std::floor(p));
			float gate = ph1 < 0.5f ? 1.f : 0.f;
			env[0] += (gate - env[0]) * k3;
			float amp = 1.f - k2 * (1.f - env[0]);
			y[0] = x[0] * amp;
			y[1] = x[1] * amp;
			break;
		}
		case CHORUS:
		case VIBRATO: {
			float rateHz = cur == CHORUS ? k1 : k1;
			ph += rateHz / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			for (int ch = 0; ch < 2; ch++) {
				float lfo = std::sin(2.f * PI * (float) (ph + (cur == CHORUS ? ch * 0.25 : 0.0)));
				float base = (cur == CHORUS ? 0.012f : 0.004f) * sr;
				float v = ring[ch].tap(1.f + base + k2 * (0.5f + 0.5f * lfo));
				y[ch] = cur == CHORUS ? lerp(x[ch], 0.5f * (x[ch] + v) * 1.3f, d) : lerp(x[ch], v, d);
			}
			break;
		}
		case FLANGER: {
			ph += k1 / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			for (int ch = 0; ch < 2; ch++) {
				float lfo = 0.5f + 0.5f * std::sin(2.f * PI * (float) (ph + ch * 0.2));
				float v = ring[ch].tap(1.f + 0.0002f * sr + k2 * lfo);
				ring[ch].push(x[ch] + v * k3);
				y[ch] = 0.6f * (x[ch] + v);
			}
			break;
		}
		case PHASER: {
			ph += k1 / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			for (int ch = 0; ch < 2; ch++) {
				float v = x[ch] + ap[ch][5] * k3;
				for (int s = 0; s < 4; s++) {
					float o = k2 * v + ap[ch][s];
					ap[ch][s] = v - k2 * o;
					v = o;
				}
				ap[ch][5] = v;
				y[ch] = 0.5f * (x[ch] + v) * 1.3f;
			}
			break;
		}
		case RINGMOD: {
			ph2 += 0.4 / sr;
			if (ph2 >= 1.0)
				ph2 -= 1.0;
			float nc = oscC * k1 - oscS * k2, ns = oscS * k1 + oscC * k2;
			float nn = 1.5f - 0.5f * (nc * nc + ns * ns); // keep it on the circle
			oscC = nc * nn, oscS = ns * nn;
			for (int ch = 0; ch < 2; ch++)
				y[ch] = lerp(x[ch], x[ch] * oscS, d);
			break;
		}
		case TREMOLO: {
			ph += k1 / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			float s = std::sin(2.f * PI * (float) ph);
			float trem = 1.f - b * (0.5f - 0.5f * s);
			float gl = 1.f - b * (0.5f + 0.5f * s), gr = 1.f - b * (0.5f - 0.5f * s);
			y[0] = x[0] * lerp(trem, gl, d);
			y[1] = x[1] * lerp(trem, gr, d);
			break;
		}
		case LOOPER: {
			if (trig) {
				// from now: one pass recorded (heard as it is), then it repeats
				trig = false;
				frozen = false;
				loopStart = ring[0].w;
				loopLen = std::max(64, (int) k1);
				rp[0] = 0.0;
			}
			if (!frozen) {
				if (loopLen > 0 && ring[0].w - loopStart >= loopLen)
					frozen = true;
				break; // still recording: the live sound
			}
			double len = loopLen;
			float fade = std::min(0.002f * sr, (float) len * 0.25f);
			for (int k = 0; k < (k3 > 0.f ? 2 : 1); k++) {
				double pos = std::fmod(rp[0] + k * len * 0.5, len);
				float w = k3 > 0.f ? 0.5f - 0.5f * std::cos(2.f * PI * (float) (pos / len)) // FREEZE: two smooth heads
				                   : std::min(1.f, std::min((float) pos / fade, (float) (len - pos) / fade));
				for (int ch = 0; ch < 2; ch++)
					y[ch] = (k == 0 ? 0.f : y[ch]) + ring[ch].at((double) loopStart + pos) * w;
			}
			rp[0] += k2;
			if (rp[0] >= len * 64.0)
				rp[0] = std::fmod(rp[0], len);
			y[0] = lerp(x[0], y[0], d);
			y[1] = lerp(x[1], y[1], d);
			break;
		}
		case SCATTER: {
			// the bar just played, cut into slices; some come again out of order or backwards
			double steps = t.barPos * 4.0 / k1;
			int idx = (int) steps;
			if (idx != sliceAt || trig) {
				trig = false;
				sliceAt = idx;
				int sliceLen = std::max(64, (int) (k1 * t.spb));
				int n = std::max(1, (int) (4.f / k1));
				int64_t barLen = (int64_t) sliceLen * n;
				slicePlay = 0;
				if (noise() * 0.5f + 0.5f < a && barLen + sliceLen < (int64_t) ring[0].size() * 9 / 10) {
					int from = (int) ((noise() * 0.5f + 0.5f) * n) % n;
					rp[0] = (double) (ring[0].w - barLen + (int64_t) from * sliceLen);
					sliceRev = noise() * 0.5f + 0.5f < a * 0.35f;
					if (sliceRev)
						rp[0] += sliceLen - 1;
					slicePlay = 1;
					sliceLeft = sliceLen;
					loopLen = sliceLen;
				}
			}
			if (slicePlay && sliceLeft > 0) {
				float fade = 0.002f * sr;
				float w = std::min(1.f, std::min((float) (loopLen - sliceLeft) / fade, (float) sliceLeft / fade));
				for (int ch = 0; ch < 2; ch++)
					y[ch] = lerp(x[ch], ring[ch].at(rp[0]) * w, d);
				rp[0] += sliceRev ? -1.0 : 1.0;
				sliceLeft--;
			}
			break;
		}
		case STOPPER: {
			int st = (int) (t.barPos * 16.0) & 15;
			float gate = STOP_PAT[(int) k1][st] == 'x' ? 1.f : 0.f;
			env[0] += (gate - env[0]) * k3;
			float amp = 1.f - b * (1.f - env[0]);
			y[0] = x[0] * amp;
			y[1] = x[1] * amp;
			break;
		}
		case TAPESTOP:
		case BACKSPIN:
		case TAPESTART:
		case DOWNER: {
			if (trig) {
				trig = false;
				rp[0] = (double) (ring[0].w - 1);
				t0 = 0.f;
				rate = cur == TAPESTART ? 0.f : (cur == BACKSPIN ? -k2 : 1.f);
				env[0] = 0.f; // TAPE START: the way back to the live sound
			}
			float out[2] = {0.f, 0.f};
			float gain = 1.f;
			if (cur == TAPESTOP) {
				t0 = std::min(1.f, t0 + k1);
				rate = std::pow(1.f - t0, k2);
				gain = std::sqrt(rate);
			}
			else if (cur == BACKSPIN) {
				t0 = std::min(1.f, t0 + k1);
				rate = -k2 * std::pow(1.f - t0, 1.5f);
				gain = std::min(1.f, std::fabs(rate));
			}
			else if (cur == TAPESTART) {
				t0 = std::min(1.f, t0 + k1);
				rate = 1.f - std::pow(1.f - t0, k2);
				gain = std::sqrt(rate);
			}
			else {
				t0 = std::min(1.f, t0 + k1);
				rate = lerp(1.f, k2, t0);
			}
			double lag = (double) ring[0].w - rp[0];
			if (lag < 1.0 || lag > (double) ring[0].size() - 8.0) {
				if (cur == DOWNER && lag > 1.0)
					rp[0] = (double) (ring[0].w - 1); // too far behind: start over from now
				else
					gain = 0.f;
			}
			if (gain > 0.f)
				for (int ch = 0; ch < 2; ch++)
					out[ch] = ring[ch].at(rp[0]) * gain;
			rp[0] += rate;
			if (rp[0] > (double) (ring[0].w - 1))
				rp[0] = (double) (ring[0].w - 1);
			if (cur == TAPESTART && t0 >= 1.f) {
				// up to speed: over to the live sound in 30 ms
				env[0] = std::min(1.f, env[0] + 1.f / (0.03f * sr));
				for (int ch = 0; ch < 2; ch++)
					out[ch] = lerp(out[ch], x[ch], env[0]);
			}
			y[0] = lerp(x[0], out[0], d);
			y[1] = lerp(x[1], out[1], d);
			break;
		}
		case LOWCUT:
		case HIGHCUT:
			if (trig) {
				trig = false;
				t0 = 0.f;
				prep(t);
			}
			t0 = std::min(1.f, t0 + k1);
			for (int ch = 0; ch < 2; ch++) {
				float l, bp, h;
				f[ch][0].run(x[ch], l, bp, h);
				y[ch] = cur == LOWCUT ? h : l;
			}
			break;
		case SUBOCT:
			for (int ch = 0; ch < 2; ch++) {
				float l, bp, h;
				f[ch][0].run(x[ch], l, bp, h);
				// a flip-flop on every upward zero crossing: half the frequency (twice: a quarter)
				if (subLast[ch] <= 0.f && l > 0.f) {
					subState[ch] = -subState[ch] + (subState[ch] == 0.f ? 1.f : 0.f);
					if (d < 0.5f || subState[ch] > 0.f)
						subSign[ch] = -subSign[ch];
				}
				subLast[ch] = l;
				float e = std::fabs(l);
				env[ch] += (e - env[ch]) * (e > env[ch] ? 0.01f : 0.0008f);
				float sq = subSign[ch] * env[ch] * 1.6f;
				float s2, b2, h2;
				f[ch][1].run(sq, s2, b2, h2);
				y[ch] = x[ch] + s2 * 1.5f * a;
			}
			break;
	}
	L = y[0];
	R = y[1];
}

// ---------------------------------------------------------------- the effects that ring out (wet only)
float Bus::reverbTick(float inL, float inR, float& outR) {
	// two allpasses per side, then 8 lines through a Householder matrix
	float s[2] = {inL, inR};
	for (int ch = 0; ch < 2; ch++)
		for (int k = 0; k < 2; k++) {
			int i = ch * 2 + k;
			std::vector<float>& A = rvAp[i];
			int n = rvApLen[i];
			float bufv = A[(size_t) rvApW[i]];
			float v = s[ch] + bufv * 0.6f;
			A[(size_t) rvApW[i]] = v;
			if (++rvApW[i] >= n)
				rvApW[i] = 0;
			s[ch] = bufv - v * 0.6f;
		}
	float o[8], sum = 0.f;
	for (int i = 0; i < 8; i++) {
		int r = rvW[i] - rvLen[i];
		if (r < 0)
			r += (int) rv[i].size();
		o[i] = rv[i][(size_t) r];
		rvLp[i] += (o[i] - rvLp[i]) * k1;
		o[i] = rvLp[i] * lp[i / 4][i % 4];
		sum += o[i];
	}
	sum *= 0.25f;
	float outL = 0.f;
	outR = 0.f;
	for (int i = 0; i < 8; i++) {
		float v = o[i] - sum + (i < 4 ? s[0] : s[1]);
		rv[i][(size_t) rvW[i]] = v;
		if (++rvW[i] >= (int) rv[i].size())
			rvW[i] = 0;
		if (i % 2 == 0)
			outL += o[i];
		else
			outR += o[i];
	}
	outR *= 0.4f;
	return outL * 0.4f;
}

void Bus::runSpace(float inL, float inR, float& wL, float& wR, const Tempo& t) {
	(void) t;
	if (cur == REVERB) {
		wL = reverbTick(inL, inR, wR);
		return;
	}
	// the delay time glides (a turn of TIME bends like tape)
	fdel += (k1 - fdel) * (300.f / sr);
	switch (cur) {
		case DELAY: {
			float a0 = ring[0].tap(fdel), a1 = ring[1].tap(fdel);
			ring[0].push(inL + a0 * k2);
			ring[1].push(inR + a1 * k2);
			wL = a0, wR = a1;
			break;
		}
		case TAPEECHO: {
			ph += 0.7 / sr;
			if (ph >= 1.0)
				ph -= 1.0;
			float d = fdel + 0.0007f * sr * std::sin(2.f * PI * (float) ph);
			float a[2] = {ring[0].tap(d), ring[1].tap(d)};
			float ins[2] = {inL, inR};
			for (int ch = 0; ch < 2; ch++) {
				lp[ch][0] += (a[ch] - lp[ch][0]) * k3;
				ring[ch].push(ftanh(ins[ch] + lp[ch][0] * k2 * 1.05f));
			}
			wL = lp[0][0], wR = lp[1][0];
			break;
		}
		case PINGPONG: {
			float a0 = ring[0].tap(fdel), a1 = ring[1].tap(fdel);
			ring[0].push(0.5f * (inL + inR) + a1 * k2);
			ring[1].push(a0);
			wL = a0, wR = a1;
			break;
		}
		case REVDELAY: {
			// every chunk of TIME played backwards; two heads half a chunk apart, crossfaded
			ring[0].push(inL + lp[0][0] * k2);
			ring[1].push(inR + lp[1][0] * k2);
			int n = std::max(64, (int) fdel);
			int64_t w = ring[0].w;
			float out[2] = {0.f, 0.f};
			for (int k = 0; k < 2; k++) {
				int64_t u = (w + (int64_t) k * n / 2) % n;
				float d = 2.f * (float) u + 1.f;
				float win = 1.f - std::fabs(2.f * (float) u / n - 1.f);
				for (int ch = 0; ch < 2; ch++)
					out[ch] += ring[ch].tap(d) * win;
			}
			lp[0][0] = out[0], lp[1][0] = out[1];
			wL = out[0], wR = out[1];
			break;
		}
		default:
			wL = wR = 0.f;
	}
}

// ---------------------------------------------------------------- presets: bus 1, bus 2, route
// clang-format off
const Preset PRESETS[] = {
	{"Init",              {FILTDRIVE, DELAY},     {{0.5f, 0.3f, 0.3f},  {0.65f, 0.4f, 0.4f}},  ROUTE_12},
	{"Dusty Basement",    {VINYL, TAPEECHO},      {{0.5f, 0.3f, 0.45f}, {0.65f, 0.45f, 0.35f}}, ROUTE_12},
	{"Lo-Fi Tape",        {CASSETTE, REVERB},     {{0.5f, 0.35f, 0.5f}, {0.45f, 0.35f, 0.3f}},  ROUTE_12},
	{"Boom Bap Crunch",   {SATURATOR, COMP},      {{0.45f, 0.6f, 1.f},  {0.6f, 0.4f, 0.8f}},    ROUTE_12},
	{"Telephone Hook",    {RADIO, DELAY},         {{0.5f, 0.2f, 0.3f},  {0.55f, 0.35f, 0.35f}}, ROUTE_12},
	{"Dilla Wobble",      {WOWFLUT, SUPERFILT},   {{0.45f, 0.25f, 1.f}, {0.6f, 0.4f, 0.3f}},    ROUTE_12},
	{"Underwater",        {FILTDRIVE, CHORUS},    {{0.25f, 0.5f, 0.2f}, {0.25f, 0.7f, 0.6f}},   ROUTE_12},
	{"Spaced Out",        {PINGPONG, REVERB},     {{0.45f, 0.55f, 0.4f}, {0.75f, 0.4f, 0.45f}}, ROUTE_12},
	{"Stutter Break",     {LOOPER, ISOLATOR},     {{0.33f, 0.5f, 1.f},  {0.5f, 0.5f, 0.5f}},    ROUTE_12},
	{"Chopped & Screwed", {DOWNER, REVERB},       {{0.5f, 0.3f, 1.f},   {0.55f, 0.3f, 0.3f}},   ROUTE_12},
	{"Trap Gate",         {GATER, DELAY},         {{0.6f, 0.9f, 0.15f}, {0.45f, 0.35f, 0.3f}},  ROUTE_12},
	{"Robot Talk",        {TALK, RINGMOD},        {{0.3f, 0.6f, 1.f},   {0.3f, 0.2f, 0.35f}},   ROUTE_12},
	{"Tape Death",        {TAPESTOP, CASSETTE},   {{0.35f, 0.5f, 1.f},  {0.4f, 0.4f, 0.4f}},    ROUTE_12},
	{"Phase Dreams",      {PHASER, REVERB},       {{0.2f, 0.7f, 0.5f},  {0.6f, 0.5f, 0.35f}},   ROUTE_12},
	{"Bitcrushed",        {LOFI, FUZZ},           {{0.6f, 0.4f, 0.3f},  {0.3f, 0.5f, 0.6f}},    ROUTE_12},
	{"Scatterbrain",      {SCATTER, FLANGER},     {{0.6f, 0.5f, 1.f},   {0.2f, 0.5f, 0.5f}},    ROUTE_12},
	{"Sub Bump",          {SUBOCT, COMP},         {{0.5f, 0.35f, 0.f},  {0.5f, 0.55f, 0.7f}},   ROUTE_12},
	{"Backspin Drop",     {BACKSPIN, REVERB},     {{0.3f, 0.5f, 1.f},   {0.5f, 0.4f, 0.3f}},    ROUTE_12},
	{"Resonant Keys",     {RESONATOR, CHORUS},    {{0.0f, 0.7f, 0.45f}, {0.3f, 0.4f, 0.4f}},    ROUTE_12},
	{"Two Worlds",        {VINYL, REVDELAY},      {{0.4f, 0.3f, 0.4f},  {0.75f, 0.3f, 0.5f}},   ROUTE_PAR},
};
// clang-format on
const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

} // namespace fx
