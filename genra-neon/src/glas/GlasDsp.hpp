// SPDX-License-Identifier: GPL-3.0-only
// GLAS – the sound engine: an ANS synthesizer (after Yevgeny Murzin's glass plates). A picture of 128 columns (time)
// by 144 rows (pitch, low at the bottom) is played by a reading head that runs across it in time: every row is a sine,
// as loud as the picture is bright there. SCALE quantises the rows when they play (FREE, the scales, GENRA KEY: the
// key of the notes GENRA plays, GENRA CHORD: the notes held now); LOW and RANGE set where the rows lie, TILT makes it
// darker or brighter. BLUR smears it in time and pitch, SHIMMER lets the high rows flicker, SPREAD puts the rows
// across the stereo field. REC paints the spectrum of the IN signal into the picture at the head. DRONE plays all the
// time, GATE only while a note is held (V/OCT then transposes). Plain C++ without Rack (test/neon_test.cpp).
#pragma once
#include "../neon/NeonDsp.hpp"
#include "../neon/Notes.hpp"
#include "../../genra/mono/MonoDsp.hpp"
#include "../../genra/OutLimit.hpp"
#include <complex>

namespace glas {

using namespace neon;

static const int COLS = 128, ROWS = 144;
template <typename T>
inline T clamp(T x, T a, T b) {
	return std::min(std::max(x, a), b);
}

enum Scale { SC_FREE, SC_CHROM, SC_MAJOR, SC_MINOR, SC_DORIAN, SC_PENTA, SC_GENRA_KEY, SC_GENRA_CHORD, NUM_SCALES };
static const char* const SCALE_NAMES[NUM_SCALES] = {"FREE", "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PENTATONIC",
                                                    "GENRA KEY", "GENRA CHORD"};
static const char* const SCALE_INFO[NUM_SCALES] = {"AS PAINTED", "SEMITONES", "ON C", "ON C", "ON C", "MINOR, ON C",
                                                   "THE SONG'S KEY", "THE CHORD NOW"};
static const unsigned SCALE_MASK[NUM_SCALES] = {0, 0xfff, 0xab5, 0x5ad, 0x6ad, 0x4a9, 0, 0};
enum Dir { D_FWD, D_BWD, D_PING, NUM_DIRS };
static const char* const DIR_NAMES[NUM_DIRS] = {"FORWARD", "BACKWARD", "PING-PONG"};
static const int NUM_LENS = 6;
static const float LEN_BARS[NUM_LENS] = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
static const char* const LEN_NAMES[NUM_LENS] = {"1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS"};
enum PlayMode { PM_DRONE, PM_GATE, NUM_PLAYMODES };
static const char* const PLAYMODE_NAMES[NUM_PLAYMODES] = {"DRONE", "GATE"};
enum Tool { T_PEN, T_SPRAY, T_LINE, T_ERASE, NUM_TOOLS };
static const char* const TOOL_NAMES[NUM_TOOLS] = {"PEN", "SPRAY", "LINE", "ERASE"};
enum Bank { B_BLANK, B_CHOIR, B_DRONE, B_BELLS, B_NOISE, B_GLISS, B_PULSE, B_AURORA, NUM_BANKS };
static const char* const BANK_NAMES[NUM_BANKS] = {"BLANK", "CHOIR", "DRONE", "BELLS", "NOISE", "GLISS", "PULSE",
                                                  "AURORA"};
static const char* const BANK_INFO[NUM_BANKS] = {"EMPTY GLASS", "HELD CHORDS", "LOW AND WIDE", "SHORT HIGH STROKES",
                                                 "SPRAY CLOUDS", "SLIDING LINES", "IN TIME", "SLOW BANDS"};

inline float lowSemis(float k) { return -48.f + 60.f * clampf(k, 0.f, 1.f); }    // C0 .. C5 (semitones from C4)
inline float rangeOct(float k) { return 1.f + 7.f * clampf(k, 0.f, 1.f); }        // 1 .. 8 octaves
inline float speedMul(float k) { return std::pow(2.f, (clampf(k, 0.f, 1.f) - 0.5f) * 4.f); }  // ×1/4 .. ×4

/** The pitch class mask of a scale (bit pc), for the GENRA scales the one given. */
inline unsigned scaleMask(int sc, unsigned genraKey, unsigned genraChord) {
	if (sc == SC_GENRA_KEY)
		return genraKey;
	if (sc == SC_GENRA_CHORD)
		return genraChord;
	return SCALE_MASK[clamp(sc, 0, NUM_SCALES - 1)];
}
/** s semitones (from C4) onto the nearest note of the mask (0: as it is). */
inline float quantise(float s, unsigned mask) {
	if (!mask)
		return s;
	int base = (int) std::floor(s + 0.5f);
	for (int d = 0; d <= 6; d++)
		for (int sg = -1; sg <= 1; sg += 2) {
			int k = base + d * sg;
			if (mask & (1u << (((k % 12) + 12) % 12)))
				return (float) k;
			if (d == 0)
				break;
		}
	return s;
}

/** The picture: 0..255 per cell, column-major (col * ROWS + row), row 0 the lowest. */
struct Canvas {
	uint8_t c[COLS * ROWS] = {};
	uint8_t& at(int col, int row) { return c[clamp(col, 0, COLS - 1) * ROWS + clamp(row, 0, ROWS - 1)]; }
	uint8_t get(int col, int row) const { return c[clamp(col, 0, COLS - 1) * ROWS + clamp(row, 0, ROWS - 1)]; }
	void clear() { std::memset(c, 0, sizeof(c)); }
	/** Adds (or with amt < 0 takes away) a soft dot. */
	void dot(float col, float row, float r, float amt) {
		int c0 = (int) std::floor(col - r), c1 = (int) std::ceil(col + r);
		int r0 = (int) std::floor(row - r), r1 = (int) std::ceil(row + r);
		for (int x = c0; x <= c1; x++)
			for (int y = r0; y <= r1; y++) {
				if (x < 0 || x >= COLS || y < 0 || y >= ROWS)
					continue;
				float d = std::sqrt((x - col) * (x - col) + (y - row) * (y - row));
				if (d > r + 0.5f)
					continue;
				float w = r < 0.75f ? 1.f : clampf(1.2f - d / (r + 0.5f), 0.f, 1.f);
				int v = (int) at(x, y) + (int) std::round(amt * 255.f * w);
				at(x, y) = (uint8_t) clamp(v, 0, 255);
			}
	}
	/** A soft oval: rc columns by rr rows (the display's cells are not square). */
	void blob(float col, float row, float rc, float rr, float amt) {
		rc = std::max(rc, 0.5f), rr = std::max(rr, 0.5f);
		int c0 = (int) std::floor(col - rc), c1 = (int) std::ceil(col + rc);
		int r0 = (int) std::floor(row - rr), r1 = (int) std::ceil(row + rr);
		for (int x = c0; x <= c1; x++)
			for (int y = r0; y <= r1; y++) {
				if (x < 0 || x >= COLS || y < 0 || y >= ROWS)
					continue;
				float dx = (x - col) / rc, dy = (y - row) / rr;
				float d = std::sqrt(dx * dx + dy * dy);
				if (d > 1.15f)
					continue;
				float w = clampf(1.25f - d, 0.f, 1.f);
				int v = (int) at(x, y) + (int) std::round(amt * 255.f * w);
				at(x, y) = (uint8_t) clamp(v, 0, 255);
			}
	}
	void line(float c0, float r0, float c1, float r1, float r, float amt) {
		float len = std::max(std::fabs(c1 - c0), std::fabs(r1 - r0));
		int n = std::max(1, (int) std::ceil(len * 2.f));
		for (int i = 0; i <= n; i++) {
			float t = (float) i / n;
			dot(c0 + (c1 - c0) * t, r0 + (r1 - r0) * t, r, amt / std::max(1.f, 0.6f * r + 1.f));
		}
	}
	/** One of the BANK pictures (seed: a variation, NEW). Rows: 24 a octave at the default range (2 a semitone). */
	void paint(int bank, uint32_t seed) {
		clear();
		Rng g(seed * 747796405u + 2891336453u);
		auto semi = [](float s) { return 24.f + 2.f * s; };  // row of semitone s above C1-ish
		switch (bank) {
			case B_CHOIR: {
				const int prog[4][4] = {{0, 3, 7, 10}, {-4, 0, 3, 7}, {-7, -3, 0, 5}, {-2, 2, 5, 9}};
				int root = 24 + (int) (g.uni() * 5);
				for (int k = 0; k < 4; k++)
					for (int v = 0; v < 4; v++)
						for (int x = k * 32 + 1; x < k * 32 + 31; x++) {
							float vib = 0.35f * std::sin(x * 0.4f + v);
							dot((float) x, semi((float) (root + prog[k][v])) + vib, 0.9f, 0.55f);
							dot((float) x, semi((float) (root + prog[k][v] + 12)) + vib, 0.6f, 0.18f);
						}
				break;
			}
			case B_DRONE: {
				int root = 12 + (int) (g.uni() * 7);
				for (int x = 0; x < COLS; x++) {
					float sw = 0.5f + 0.5f * std::sin(x * 0.05f + g.uni() * 0.01f);
					dot((float) x, semi((float) root), 1.4f, 0.6f);
					dot((float) x, semi((float) root + 7), 1.0f, 0.25f + 0.25f * sw);
					dot((float) x, semi((float) root + 12), 0.8f, 0.2f);
					dot((float) x, semi((float) root + 19) + std::sin(x * 0.11f), 0.6f, 0.12f * sw);
				}
				break;
			}
			case B_BELLS: {
				const int pent[5] = {0, 3, 5, 7, 10};
				for (int i = 0; i < 26; i++) {
					int x = (int) (g.uni() * (COLS - 8));
					int s = 36 + pent[(int) (g.uni() * 5)] + 12 * (int) (g.uni() * 2);
					int len = 3 + (int) (g.uni() * 6);
					for (int k = 0; k < len; k++)
						dot((float) (x + k), semi((float) s), 0.6f, 0.7f * (1.f - (float) k / len));
				}
				break;
			}
			case B_NOISE:
				for (int i = 0; i < 9; i++) {
					float cx = g.uni() * COLS, cy = 20.f + g.uni() * 110.f, rr = 6.f + g.uni() * 12.f;
					for (int k = 0; k < 140; k++) {
						float a = g.uni() * 6.2832f, d = std::sqrt(g.uni()) * rr;
						dot(cx + std::cos(a) * d * 1.4f, cy + std::sin(a) * d, 0.5f, 0.35f);
					}
				}
				break;
			case B_GLISS:
				for (int i = 0; i < 5; i++) {
					float y0 = 20.f + g.uni() * 90.f, y1 = 20.f + g.uni() * 110.f;
					float x0 = g.uni() * 40.f, x1 = 80.f + g.uni() * 48.f;
					for (float x = x0; x <= x1; x += 0.5f) {
						float t = (x - x0) / (x1 - x0);
						dot(x, y0 + (y1 - y0) * t * t * (3.f - 2.f * t), 0.8f, 0.3f);
					}
				}
				break;
			case B_PULSE: {
				const int chord[3] = {0, 7, 15};
				for (int k = 0; k < 16; k++) {
					int x = k * 8;
					for (int v = 0; v < 3; v++)
						for (int d = 0; d < 3; d++)
							dot((float) (x + d), semi((float) (24 + chord[v] + (k % 4 == 3 ? 5 : 0))), 0.8f, 0.7f - 0.2f * d);
				}
				break;
			}
			case B_AURORA:
				for (int b = 0; b < 4; b++) {
					float y = 30.f + b * 24.f + g.uni() * 8.f, ph = g.uni() * 6.f;
					for (int x = 0; x < COLS; x++) {
						float yy = y + 6.f * std::sin(x * 0.04f + ph) + 2.f * std::sin(x * 0.13f + ph * 2.f);
						dot((float) x, yy, 2.2f, 0.12f + 0.08f * std::sin(x * 0.07f + b));
					}
				}
				break;
			default: break;
		}
	}
};

struct Params {
	int playMode = PM_DRONE;
	int len = 2;  // index into LEN_BARS
	int dir = D_FWD;
	float speed = 0.5f, pos = 0.f, posCv = 0.f;
	bool posPatched = false;
	int scale = SC_GENRA_CHORD;
	float low = 0.4f, range = 0.72f, tilt = 0.f;
	float atk = 0.2f, dec = 0.5f, sus = 1.f, rel = 0.5f;
	float spread = 0.5f, blur = 0.2f, shimmer = 0.f;
	float outGain = 1.f;
	bool rec = false;
};

/** A small radix-2 FFT (REC). */
inline void fft(std::complex<float>* a, int n) {
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (int len = 2; len <= n; len <<= 1) {
		float ang = -2.f * PI / len;
		std::complex<float> wl(std::cos(ang), std::sin(ang));
		for (int i = 0; i < n; i += len) {
			std::complex<float> w(1.f, 0.f);
			for (int k = 0; k < len / 2; k++) {
				std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

struct Engine {
	float fs = 48000.f, dt = 1.f / 48000.f;
	Canvas* canvas = nullptr;  // the module's picture (the UI paints in it)
	static const int BLK = 32;
	int blk = 0;
	float amp[ROWS] = {}, ampT[ROWS] = {}, dAmp[ROWS] = {}, slew[ROWS] = {};
	float re[ROWS], im[ROWS], cr[ROWS] = {}, ci[ROWS] = {};
	float gl[ROWS] = {}, gr[ROWS] = {};
	float shim[ROWS] = {};
	int active[ROWS] = {}, nActive = 0;
	double hph = 0.0;
	float head = 0.f;
	Notes notes;
	gmono::Env envA;
	bool gate = false;
	float voct = 0.f, envNow = 0.f, droneEnv = 0.f;
	// the notes GENRA plays: the key (pitch classes heard lately) and the chord (held now)
	float pcw[12] = {};
	unsigned keyMask = 0, chordMask = 0;
	bool lastGates[16] = {};
	Rng rng{0x6a5};
	// REC
	static const int FFTN = 1024;
	float recBuf[FFTN] = {};
	int recW = 0, recHop = 0;
	// out
	float bright = 0.f;

	Engine() {
		for (int r = 0; r < ROWS; r++)
			re[r] = 1.f, im[r] = 0.f;
		setSampleRate(48000.f);
	}
	void setSampleRate(float sr) { fs = sr, dt = 1.f / sr; }
	void allOff() {
		notes.reset();
		for (int r = 0; r < ROWS; r++)
			amp[r] = ampT[r] = dAmp[r] = 0.f;
		envA.v = 0.f;
		envA.stage = 0;
	}
	/** The pitch of row r (semitones from C4), unquantised. */
	static float rowSemis(const Params& p, float r) { return lowSemis(p.low) + r * rangeOct(p.range) * 12.f / (ROWS - 1); }
	/** The row nearest to semitone s. */
	static float semisRow(const Params& p, float s) { return (s - lowSemis(p.low)) * (ROWS - 1) / (rangeOct(p.range) * 12.f); }
	unsigned mask(const Params& p) const { return scaleMask(p.scale, keyMask, chordMask); }

	void process(const Params& p, const Channel* ch, int n, float bpm, bool resetNow, float in, float& L, float& R,
	             float& env, float& brightOut) {
		// ---- the notes: GENRA's key and chord, the gate (GATE mode)
		n = std::min(n, 16);
		unsigned held = 0;
		for (int c = 0; c < n; c++) {
			int pc = (((int) std::lround(ch[c].voct * 12.f)) % 12 + 12) % 12;
			if (ch[c].gate) {
				held |= 1u << pc;
				if (!lastGates[c])
					pcw[pc] += 1.f;
			}
			lastGates[c] = ch[c].gate;
		}
		if (held)
			chordMask = held;
		notes.step(MODE_MONO, ch, n, 1, [&](int, int c, bool) { voct = ch[c].voct; envA.gate = false; },
		           [&](int, int c) { voct = ch[c].voct; gate = true; }, [&](int) { gate = false; });
		float e;
		if (p.playMode == PM_GATE)
			e = envA.process(gate, envTime(p.atk), envTime(p.dec), p.sus, envTime(p.rel), dt);
		else {
			droneEnv += (1.f - droneEnv) * (1.f - std::exp(-dt / std::max(0.02f, envTime(p.atk))));
			e = droneEnv;
		}
		envNow = e;
		env = e;
		// ---- the head
		if (resetNow)
			hph = 0.0;
		float bars = LEN_BARS[clamp(p.len, 0, NUM_LENS - 1)];
		hph += (double) (bpm / 60.f / (4.f * bars) * speedMul(p.speed) * dt);
		hph -= std::floor(hph);
		float h = (float) hph;
		if (p.dir == D_BWD)
			h = 1.f - h;
		else if (p.dir == D_PING)
			h = 1.f - std::fabs(2.f * h - 1.f);
		head = p.posPatched ? clampf(p.pos + p.posCv * 0.1f, 0.f, 1.f) : wrap(h + p.pos);
		// ---- REC: the spectrum of IN into the picture, at the head
		if (p.rec && canvas) {
			recBuf[recW] = in;
			recW = (recW + 1) % FFTN;
			if (++recHop >= FFTN / 2) {
				recHop = 0;
				record(p);
			}
		}
		// ---- the rows, once a block
		if (--blk <= 0) {
			blk = BLK;
			block(p);
		}
		float l = 0.f, r = 0.f;
		for (int k = 0; k < nActive; k++) {
			int i = active[k];
			float c = re[i], s = im[i];
			re[i] = c * cr[i] - s * ci[i];
			im[i] = c * ci[i] + s * cr[i];
			amp[i] += dAmp[i];
			float y = im[i] * amp[i];
			l += y * gl[i];
			r += y * gr[i];
		}
		brightOut = bright;
		L = genra_out::limit(l * e * 2.2f * p.outGain);
		R = genra_out::limit(r * e * 2.2f * p.outGain);
	}

	void block(const Params& p) {
		// the key: the pitch classes of the last notes, fading over about 20 s
		float kdec = std::exp(-BLK * dt / 20.f);
		float mx = 0.f;
		for (int i = 0; i < 12; i++)
			pcw[i] *= kdec, mx = std::max(mx, pcw[i]);
		keyMask = 0;
		for (int i = 0; i < 12 && mx > 0.05f; i++)
			if (pcw[i] >= 0.25f * mx)
				keyMask |= 1u << i;
		unsigned m = mask(p);
		float trans = p.playMode == PM_GATE ? voct * 12.f : 0.f;
		float colF = head * (COLS - 1);
		int c0 = (int) colF, c1 = std::min(c0 + 1, COLS - 1);
		float fr = colF - c0;
		float tiltK = clampf(p.tilt, -1.f, 1.f) * 2.08f;  // ±3 octaves of tilt: ×8 at the ends
		float sl = 1.f - std::exp(-(float) BLK * dt / (0.004f + 1.5f * p.blur * p.blur));
		float sum2 = 0.f, sumA = 0.f;
		nActive = 0;
		for (int r = 0; r < ROWS; r++) {
			float a = 0.f;
			if (canvas) {
				a = (canvas->get(c0, r) * (1.f - fr) + canvas->get(c1, r) * fr) * (1.f / 255.f);
				if (p.blur > 0.01f) {  // a little of the rows next to it
					float nb = (canvas->get(c0, r - 1) + canvas->get(c0, r + 1)) * (1.f / 510.f);
					a = a + (nb - a) * 0.35f * p.blur;
				}
			}
			a = a * a;  // brightness to level: the dim strokes stay quiet
			a *= std::exp(tiltK * ((float) r / (ROWS - 1) - 0.5f));
			if (p.shimmer > 0.01f) {
				shim[r] += (rng.bi() - shim[r]) * 0.08f;
				float hi = (float) r / ROWS;
				a *= clampf(1.f + p.shimmer * hi * 1.6f * shim[r], 0.f, 2.f);
			}
			ampT[r] = ampT[r] + (a - ampT[r]) * sl;
			sumA += ampT[r];
			sum2 += ampT[r] * ampT[r];
		}
		float norm = 1.f / std::sqrt(1.f + sum2 * 0.6f) * 0.9f;
		bright = clampf(sumA / (ROWS * 0.12f), 0.f, 1.f) * 10.f;
		for (int r = 0; r < ROWS; r++) {
			float target = ampT[r] * norm;
			float semis = quantise(rowSemis(p, (float) r), m) + trans;
			if (p.shimmer > 0.01f)
				semis += p.shimmer * 0.25f * shim[r] * ((float) r / ROWS);
			float hz = C4 * std::pow(2.f, semis / 12.f);
			if (hz < 16.f || hz > 0.45f * fs)
				target = 0.f;
			dAmp[r] = (target - amp[r]) / BLK;
			if (target < 1e-5f && amp[r] < 1e-5f) {
				amp[r] = 0.f, dAmp[r] = 0.f;
				continue;
			}
			float w = 2.f * PI * hz * dt;
			cr[r] = std::cos(w), ci[r] = std::sin(w);
			// keep the oscillator on the unit circle
			float mag2 = re[r] * re[r] + im[r] * im[r];
			float k = 1.5f - 0.5f * mag2;
			re[r] *= k, im[r] *= k;
			float pan = 0.5f + 0.45f * p.spread * std::sin(r * 2.39996f);
			gl[r] = std::sqrt(1.f - pan) * 1.41f * 0.7071f;
			gr[r] = std::sqrt(pan) * 1.41f * 0.7071f;
			active[nActive++] = r;
		}
	}

	void record(const Params& p) {
		std::complex<float> a[FFTN];
		for (int i = 0; i < FFTN; i++) {
			float w = 0.5f - 0.5f * std::cos(2.f * PI * i / (FFTN - 1));
			a[i] = std::complex<float>(recBuf[(recW + i) % FFTN] * w, 0.f);
		}
		fft(a, FFTN);
		int col = (int) std::round(head * (COLS - 1));
		for (int r = 0; r < ROWS; r++) {
			float hz = C4 * std::pow(2.f, rowSemis(p, (float) r) / 12.f);
			float bin = hz * FFTN / fs;
			if (bin < 1.f || bin > FFTN / 2 - 2)
				continue;
			int b = (int) bin;
			float f = bin - b;
			float mag = std::abs(a[b]) * (1.f - f) + std::abs(a[b + 1]) * f;
			float db = 20.f * std::log10(mag / (FFTN * 0.25f * 5.f) + 1e-9f);  // 0 dB: a 5 V sine
			float v = clampf((db + 54.f) / 54.f, 0.f, 1.f);
			uint8_t& cell = canvas->at(col, r);
			int nv = (int) std::round(std::sqrt(v) * 255.f);
			if (nv > cell)
				cell = (uint8_t) nv;
		}
	}
};

}  // namespace glas
