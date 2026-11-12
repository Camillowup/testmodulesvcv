// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: reading, cutting and listening to the sample (see Chop.hpp).
#include "Chop.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace hh {

const char* const CHOP_MODES[NUM_CHOP_MODES] = {"Transient", "Equal 16", "Equal 8"};

// ---------------------------------------------------------------- WAV
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

bool readWav(const std::vector<uint8_t>& buf, SampleData& out, std::string& err, float maxSeconds) {
	const uint8_t* b = buf.data();
	size_t n = buf.size();
	if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) {
		err = "not a WAV file";
		return false;
	}
	int fmt = 0, ch = 0, bits = 0;
	uint32_t rate = 0;
	const uint8_t* data = nullptr;
	size_t dataLen = 0;
	size_t pos = 12;
	while (pos + 8 <= n) {
		uint32_t len = rd32(b + pos + 4);
		const uint8_t* c = b + pos + 8;
		size_t avail = std::min((size_t) len, n - pos - 8);
		if (!memcmp(b + pos, "fmt ", 4) && avail >= 16) {
			fmt = rd16(c);
			ch = rd16(c + 2);
			rate = rd32(c + 4);
			bits = rd16(c + 14);
			if (fmt == 0xFFFE && avail >= 26)
				fmt = rd16(c + 24); // WAVE_FORMAT_EXTENSIBLE: the sub format
		}
		else if (!memcmp(b + pos, "data", 4)) {
			data = c;
			dataLen = avail;
		}
		pos += 8 + len + (len & 1);
	}
	if (!data || ch <= 0 || rate == 0) {
		err = "no audio in the file";
		return false;
	}
	if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && (bits == 32 || bits == 64)))) {
		err = "this WAV format is not supported";
		return false;
	}
	int bps = bits / 8;
	size_t frames = dataLen / (size_t) (bps * ch);
	frames = std::min(frames, (size_t) (maxSeconds * rate));
	out.sampleRate = (float) rate;
	out.L.assign(frames, 0.f);
	out.R.clear();
	if (ch > 1)
		out.R.assign(frames, 0.f);
	for (size_t f = 0; f < frames; f++)
		for (int c = 0; c < std::min(ch, 2); c++) {
			const uint8_t* p = data + (f * ch + c) * bps;
			float v = 0.f;
			if (fmt == 3)
				v = bits == 32 ? *(const float*) (const void*) p : (float) *(const double*) (const void*) p;
			else if (bits == 8)
				v = (p[0] - 128) / 128.f;
			else if (bits == 16)
				v = (int16_t) rd16(p) / 32768.f;
			else if (bits == 24) {
				int32_t x = (int32_t) (p[0] << 8 | p[1] << 16 | (uint32_t) p[2] << 24) >> 8;
				v = x / 8388608.f;
			}
			else
				v = (int32_t) rd32(p) / 2147483648.f;
			if (!std::isfinite(v))
				v = 0.f;
			(c == 0 ? out.L : out.R)[f] = v;
		}
	if (frames < 64) {
		err = "the sample is too short";
		return false;
	}
	return true;
}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
	for (int i = 0; i < 4; i++)
		v.push_back((uint8_t) (x >> (8 * i)));
}
static void put16(std::vector<uint8_t>& v, uint16_t x) {
	v.push_back((uint8_t) x);
	v.push_back((uint8_t) (x >> 8));
}

std::vector<uint8_t> writeWav(const SampleData& s, size_t a, size_t b, bool reverse, int semitones) {
	b = std::min(b, s.frames());
	a = std::min(a, b);
	double speed = std::pow(2.0, semitones / 12.0);
	size_t n = (size_t) ((b - a) / speed);
	std::vector<uint8_t> v;
	v.reserve(44 + n * 6);
	v.insert(v.end(), {'R', 'I', 'F', 'F'});
	put32(v, (uint32_t) (36 + n * 6));
	v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
	put32(v, 16);
	put16(v, 1);
	put16(v, 2);
	put32(v, (uint32_t) s.sampleRate);
	put32(v, (uint32_t) s.sampleRate * 6);
	put16(v, 6);
	put16(v, 24);
	v.insert(v.end(), {'d', 'a', 't', 'a'});
	put32(v, (uint32_t) (n * 6));
	const std::vector<float>& R = s.R.empty() ? s.L : s.R;
	for (size_t i = 0; i < n; i++) {
		double x = i * speed;
		if (reverse)
			x = (b - a) - 1 - x;
		size_t i0 = a + (size_t) std::max(0.0, x);
		size_t i1 = std::min(b - 1, i0 + 1);
		float fr = (float) (x - std::floor(x));
		for (int c = 0; c < 2; c++) {
			const std::vector<float>& ch = c ? R : s.L;
			float y = ch[std::min(i0, b - 1)] * (1.f - fr) + ch[i1] * fr;
			int32_t q = (int32_t) std::lround(std::max(-1.f, std::min(1.f, y)) * 8388607.f);
			v.push_back((uint8_t) q);
			v.push_back((uint8_t) (q >> 8));
			v.push_back((uint8_t) (q >> 16));
		}
	}
	return v;
}

// ---------------------------------------------------------------- spectrum
static void fft(std::vector<std::complex<float>>& a) {
	size_t n = a.size();
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		float ang = -2.f * (float) M_PI / (float) len;
		std::complex<float> wl(std::cos(ang), std::sin(ang));
		for (size_t i = 0; i < n; i += len) {
			std::complex<float> w(1.f, 0.f);
			for (size_t j = 0; j < len / 2; j++) {
				std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
				a[i + j] = u + v;
				a[i + j + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

/** Magnitudes of a Hann-windowed frame of x at `at` (zeros past the ends). */
static void spectrum(const std::vector<float>& x, size_t at, size_t n, std::vector<float>& mag) {
	std::vector<std::complex<float>> a(n);
	for (size_t i = 0; i < n; i++) {
		size_t k = at + i;
		float w = 0.5f - 0.5f * std::cos(2.f * (float) M_PI * i / (n - 1));
		a[i] = std::complex<float>(k < x.size() ? x[k] * w : 0.f, 0.f);
	}
	fft(a);
	mag.resize(n / 2);
	for (size_t i = 0; i < n / 2; i++)
		mag[i] = std::abs(a[i]);
}

// ---------------------------------------------------------------- cutting
static std::vector<float> monoOf(const SampleData& s) {
	std::vector<float> m(s.L);
	if (!s.R.empty())
		for (size_t i = 0; i < m.size(); i++)
			m[i] = 0.5f * (m[i] + s.R[i]);
	return m;
}

/** Onsets by spectral flux: (frame position, strength), strongest first. */
static std::vector<std::pair<size_t, float>> onsets(const std::vector<float>& m, float sr) {
	const size_t N = 1024, HOP = 256;
	std::vector<float> flux;
	std::vector<float> mag, prev(N / 2, 0.f);
	for (size_t at = 0; at + N / 2 < m.size(); at += HOP) {
		spectrum(m, at, N, mag);
		float f = 0.f;
		for (size_t i = 1; i < N / 2; i++) {
			float l = std::log1p(10.f * mag[i]);
			f += std::max(0.f, l - prev[i]);
			prev[i] = l;
		}
		flux.push_back(f);
	}
	std::vector<std::pair<size_t, float>> out;
	int minGap = std::max(1, (int) (0.08f * sr / HOP));
	for (int i = 1; i + 1 < (int) flux.size(); i++) {
		bool peak = true;
		for (int k = -3; k <= 3 && peak; k++)
			if (k && i + k >= 0 && i + k < (int) flux.size() && flux[i + k] > flux[i])
				peak = false;
		if (!peak)
			continue;
		std::vector<float> w;
		for (int k = -16; k <= 16; k++)
			if (i + k >= 0 && i + k < (int) flux.size())
				w.push_back(flux[i + k]);
		std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
		float med = w[w.size() / 2];
		if (flux[i] > med * 1.5f + 0.5f) {
			if (!out.empty() && (int) (out.back().first / HOP) > i - minGap) {
				if (flux[i] > out.back().second)
					out.back() = {(size_t) i * HOP, flux[i]};
				continue;
			}
			out.push_back({(size_t) i * HOP, flux[i]});
		}
	}
	std::sort(out.begin(), out.end(), [](const std::pair<size_t, float>& a, const std::pair<size_t, float>& b) {
		return a.second > b.second;
	});
	return out;
}

static void cut(ChopSet& cs, const std::vector<float>& m) {
	size_t total = cs.data.frames();
	std::vector<size_t> st;
	if (cs.mode == CM_EQUAL16 || cs.mode == CM_EQUAL8) {
		int n = cs.mode == CM_EQUAL16 ? 16 : 8;
		for (int i = 0; i < n; i++)
			st.push_back(total * i / n);
	}
	else {
		st.push_back(0);
		std::vector<std::pair<size_t, float>> on = onsets(m, cs.data.sampleRate);
		size_t minLen = (size_t) (0.06f * cs.data.sampleRate);
		for (auto& o : on) {
			if ((int) st.size() >= PADS)
				break;
			bool close = false;
			for (size_t s : st)
				if ((o.first > s ? o.first - s : s - o.first) < minLen)
					close = true;
			if (!close)
				st.push_back(o.first);
		}
		std::sort(st.begin(), st.end());
		// fewer than 16: split the longest in half (while they are long enough)
		while ((int) st.size() < PADS) {
			size_t best = 0, bestLen = 0;
			for (size_t i = 0; i < st.size(); i++) {
				size_t e = i + 1 < st.size() ? st[i + 1] : total;
				if (e - st[i] > bestLen)
					bestLen = e - st[i], best = i;
			}
			if (bestLen < (size_t) (0.3f * cs.data.sampleRate))
				break;
			st.insert(st.begin() + best + 1, st[best] + bestLen / 2);
		}
	}
	cs.info.count = (int) st.size();
	for (int i = 0; i < PADS; i++) {
		if (i < cs.info.count) {
			cs.start[i] = st[i];
			cs.end[i] = i + 1 < cs.info.count ? st[i + 1] : total;
		}
		else
			cs.start[i] = cs.end[i] = 0;
	}
}

// ---------------------------------------------------------------- tones, chords, key
static const float MAJ_PROFILE[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
static const float MIN_PROFILE[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

static float corr(const float* a, const float* b) {
	float ma = 0.f, mb = 0.f;
	for (int i = 0; i < 12; i++)
		ma += a[i], mb += b[i];
	ma /= 12.f, mb /= 12.f;
	float s = 0.f, sa = 0.f, sb = 0.f;
	for (int i = 0; i < 12; i++) {
		s += (a[i] - ma) * (b[i] - mb);
		sa += (a[i] - ma) * (a[i] - ma);
		sb += (b[i] - mb) * (b[i] - mb);
	}
	return sa > 0.f && sb > 0.f ? s / std::sqrt(sa * sb) : 0.f;
}

void findHarmony(ChopSet& cs) {
	float sum[12] = {};
	for (int p = 0; p < cs.info.count; p++) {
		float* c = cs.chroma[p];
		float mx = 0.f, mean = 0.f;
		for (int i = 0; i < 12; i++)
			mx = std::max(mx, c[i]), mean += c[i] / 12.f;
		ChopInfo& ci = cs.info.chop[p];
		for (int i = 0; i < 12; i++)
			sum[i] += c[i] * ci.seconds;
		// flat fingerprint: a hit, no chord
		if (mx <= 0.f || mx < mean * 1.7f) {
			ci.chord.quality = Q_NONE;
			ci.conf = 0.f;
			if (ci.kind == 0)
				ci.kind = 2;
			continue;
		}
		static const int QS[] = {Q_MAJ, Q_MIN, Q_DOM7, Q_MAJ7, Q_MIN7, Q_MIN9, Q_DIM, Q_SUS4};
		float best = -1.f, second = -1.f;
		Chord bc;
		for (int r = 0; r < 12; r++)
			for (int q : QS) {
				float t[12] = {};
				int nt = 0;
				for (int k = 0; k < 5; k++)
					if (QUALITY_TONES[q][k] >= 0)
						t[(r + QUALITY_TONES[q][k]) % 12] = k == 0 ? 1.4f : 1.f, nt++;
				float dot = 0.f, nn = 0.f, cc = 0.f;
				for (int i = 0; i < 12; i++)
					dot += c[i] * t[i], nn += t[i] * t[i], cc += c[i] * c[i];
				float score = dot / std::sqrt(nn * cc) - 0.02f * (nt - 3); // the simpler chord wins a tie
				if (score > best) {
					if (bc.root != r)
						second = best;
					best = score;
					bc.root = r;
					bc.quality = q;
				}
				else if (score > second && r != bc.root)
					second = score;
			}
		ci.chord = bc;
		ci.conf = std::max(0.f, std::min(1.f, (best - std::max(0.f, second)) * 6.f + (best - 0.6f)));
	}
	// the key: the major / minor profile that fits the whole sample best
	float best = -2.f, second = -2.f;
	Key bk;
	for (int r = 0; r < 12; r++)
		for (int mn = 0; mn < 2; mn++) {
			float prof[12];
			for (int i = 0; i < 12; i++)
				prof[(i + r) % 12] = mn ? MIN_PROFILE[i] : MAJ_PROFILE[i];
			float c = corr(sum, prof);
			if (c > best)
				second = best, best = c, bk.root = r, bk.minor = mn == 1;
			else if (c > second)
				second = c;
		}
	cs.info.key = bk;
	cs.info.keyConf = std::max(0.f, std::min(1.f, (best - second) * 5.f + best * 0.5f));
}

void analyse(ChopSet& cs, int mode) {
	cs.mode = mode;
	std::vector<float> m = monoOf(cs.data);
	float sr = cs.data.sampleRate;
	cut(cs, m);
	// the display's waveform
	cs.peaks.assign(512, 0.f);
	for (size_t i = 0; i < m.size(); i++) {
		size_t k = i * 512 / m.size();
		cs.peaks[k] = std::max(cs.peaks[k], std::fabs(m[i]));
	}
	// per chop: the 12 tones, how long it rings, how bright
	const size_t N = 4096;
	std::vector<float> mag;
	for (int p = 0; p < cs.info.count; p++) {
		ChopInfo& ci = cs.info.chop[p];
		size_t a = cs.start[p], b = cs.end[p];
		ci.seconds = (b - a) / sr;
		float* c = cs.chroma[p];
		for (int i = 0; i < 12; i++)
			c[i] = 0.f;
		float cent = 0.f, centW = 0.f;
		size_t hop = N / 2;
		int frames = 0;
		for (size_t at = a; at < b && frames < 24; at += hop, frames++) {
			spectrum(m, at, N, mag);
			for (size_t k = 1; k < N / 2; k++) {
				float f = k * sr / N;
				float w = mag[k];
				if (f < 8000.f)
					cent += f * w, centW += w;
				if (f < 60.f || f > 2200.f)
					continue;
				float midi = 69.f + 12.f * std::log2(f / 440.f);
				int pc = ((int) std::lround(midi) % 12 + 12) % 12;
				float dev = std::fabs(midi - std::round(midi)); // between two notes: counts less
				c[pc] += w * w * (1.f - dev);
			}
			if (at + hop >= b)
				break;
		}
		for (int i = 0; i < 12; i++)
			c[i] = std::sqrt(c[i]);
		ci.centroid = centW > 0.f ? cent / centW : 0.f;
		// how it rings: the energy in the last third against the first
		float e0 = 0.f, e1 = 0.f;
		size_t len = b - a;
		for (size_t i = a; i < a + len / 3; i++)
			e0 += m[i] * m[i];
		for (size_t i = b - len / 3; i < b; i++)
			e1 += m[i] * m[i];
		bool decays = e1 < e0 * 0.15f;
		ci.kind = (ci.seconds < 0.25f || decays) ? 1 : 0;
	}
	findHarmony(cs);
	// as a break: which chop is the kick, the snare, the hats (by brightness)
	int order[PADS];
	int n = cs.info.count;
	for (int i = 0; i < n; i++)
		order[i] = i;
	std::sort(order, order + n, [&](int x, int y) { return cs.info.chop[x].centroid < cs.info.chop[y].centroid; });
	for (int v = 0; v < DRUMS; v++)
		cs.voicePad[v] = -1;
	if (n > 0) {
		cs.voicePad[KICK] = order[0];
		cs.voicePad[TOM] = order[std::min(n - 1, 1)];
		cs.voicePad[SNARE] = order[n / 2];
		cs.voicePad[CLAP] = order[std::min(n - 1, n / 2 + 1)];
		cs.voicePad[RIM] = order[std::max(0, n / 2 - 1)];
		cs.voicePad[CH] = order[n - 1];
		cs.voicePad[OH] = order[std::max(0, n - 2)];
		cs.voicePad[PERC] = order[std::max(0, n - 3)];
	}
}

} // namespace hh
