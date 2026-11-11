// SPDX-License-Identifier: GPL-3.0-only
// LIED: from a line of lyrics to what is sung. Plain C++ (no Rack), so the host test can run it.
//
// Every word is spoken by eSpeak at once (so it sounds like a word), at a constant pitch, and then cut into its
// syllables where the phonemes say: one syllable per vowel, a single consonant between two vowels goes with the next
// one. A word with hyphens ("sin-gen") is cut where the hyphens are; if eSpeak hears a different number of syllables
// there, the pieces are spoken one by one instead. Each piece (a Unit) knows its vowel (held while the gate is) and its
// pitch marks (the module sings it at any pitch with them, PSOLA).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lied {

struct Ph {
	int pos;
	bool vowel;
	std::string name;
};

struct Unit {
	std::vector<float> s;   // samples at the core's rate (22050)
	std::vector<int> marks; // pitch marks, increasing
	int v0 = 0, v1 = 0;     // the vowel that is held [v0, v1) (none: v1 <= v0)
	float t0 = 147.f;       // period in samples
	std::string text;       // as shown
	std::string phon;       // its phonemes (eSpeak's names)
	float env[48] = {};     // the vowel's spectrum 0 .. 5 kHz, 0..1 (the display)
	float len() const { return (float) s.size(); }
};

struct Line {
	std::string text;                          // as typed
	std::vector<Unit> syl, word;               // STEP SYLL, STEP WORD
	Unit whole;                                // STEP FREE
	std::vector<std::pair<int, int>> sylAt;    // the bytes of text to light per syllable
	std::vector<std::pair<int, int>> wordAt;
	std::string key;                           // what it was made from (text, voice)
	bool empty() const { return syl.empty(); }
};

/** Speaks text: fills samples and phonemes, returns false on an error. */
using SpeakFn = std::function<bool(const std::string& text, std::vector<float>& out, std::vector<Ph>& ph)>;

// ------------------------------------------------------------------------------------------------ UTF-8 letters
inline uint32_t utf8At(const std::string& s, size_t i, int& len) {
	unsigned char c = (unsigned char) s[i];
	len = 1;
	if (c < 0x80)
		return c;
	int n = (c >= 0xf0) ? 4 : (c >= 0xe0) ? 3 : (c >= 0xc0) ? 2 : 1;
	uint32_t cp = c & (0x7f >> n);
	for (int k = 1; k < n && i + k < s.size(); k++)
		cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3f);
	len = n;
	return cp;
}

inline bool isVowelCp(uint32_t c) {
	if (c < 0x80)
		return std::string("aeiouyAEIOUY").find((char) c) != std::string::npos;
	// Latin-1 and Latin Extended-A vowels (ä ö ü à á â ã å æ è é ê ë ì í î ï ò ó ô õ ø ù ú û ý ÿ œ ą ę ě ů ő ű …)
	static const uint32_t V[] = {0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	                             0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xe0, 0xe1, 0xe2, 0xe3,
	                             0xe4, 0xe5, 0xe6, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5,
	                             0xf6, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xff, 0x104, 0x105, 0x118, 0x119, 0x11a, 0x11b,
	                             0x150, 0x151, 0x152, 0x153, 0x16e, 0x16f, 0x170, 0x171};
	for (uint32_t v : V)
		if (v == c)
			return true;
	return false;
}

inline bool isLetterCp(uint32_t c) {
	if (c < 0x80)
		return std::isalnum((int) c) || c == '\'';
	return c >= 0xc0 && c != 0xd7 && c != 0xf7 && c < 0x2000 && c != 0x2019 ? true : c == 0x2019;
}

struct Piece {
	int a, b; // bytes [a, b) of the line
};

/** Orthographic syllables of the word line[a, b): at the hyphens if there are any (explicit), else one per group of
 * vowels, cut before a single consonant and before the last of several (ch, sch, ck, ph, th, sh, qu count as one). */
inline std::vector<Piece> orthoSyllables(const std::string& line, int a, int b, bool& explicitCut) {
	std::vector<Piece> out;
	explicitCut = false;
	for (int i = a; i < b; i++)
		if (line[i] == '-')
			explicitCut = true;
	if (explicitCut) {
		int s = a;
		for (int i = a; i <= b; i++) {
			if (i == b || line[i] == '-') {
				if (i > s)
					out.push_back({s, i});
				s = i + 1;
			}
		}
		return out;
	}
	struct C {
		int at, len;
		bool vowel, letter;
		char low;
	};
	std::vector<C> cs;
	for (int i = a; i < b;) {
		int n;
		uint32_t cp = utf8At(line, i, n);
		cs.push_back({i, n, isVowelCp(cp), isLetterCp(cp), (char) (cp < 0x80 ? std::tolower((int) cp) : 0)});
		i += n;
	}
	// vowel groups
	std::vector<std::pair<int, int>> groups;  // indices into cs [first, last]
	for (int i = 0; i < (int) cs.size(); i++) {
		if (!cs[i].vowel)
			continue;
		// a "y" right after a vowel or at the start before a vowel is a consonant
		if (cs[i].low == 'y' && i + 1 < (int) cs.size() && cs[i + 1].vowel && (i == 0 || !cs[i - 1].vowel))
			continue;
		if (!groups.empty() && groups.back().second == i - 1)
			groups.back().second = i;
		else
			groups.push_back({i, i});
	}
	if (groups.size() <= 1) {
		out.push_back({a, b});
		return out;
	}
	auto lowAt = [&](int i) { return i < (int) cs.size() ? cs[i].low : 0; };
	std::vector<int> cuts;  // index into cs where a syllable starts
	for (size_t g = 1; g < groups.size(); g++) {
		int c0 = groups[g - 1].second + 1, c1 = groups[g].first;  // consonants [c0, c1)
		// consonant units
		std::vector<int> units;
		for (int i = c0; i < c1;) {
			units.push_back(i);
			char x = lowAt(i), y = lowAt(i + 1), z = lowAt(i + 2);
			if (x == 's' && y == 'c' && z == 'h' && i + 3 <= c1)
				i += 3;
			else if (((x == 'c' && (y == 'h' || y == 'k')) || ((x == 'p' || x == 't' || x == 's') && y == 'h') ||
			          (x == 'q' && y == 'u')) && i + 2 <= c1)
				i += 2;
			else
				i += 1;
		}
		int cut = units.empty() ? c1 : units.back();
		cuts.push_back(cut);
	}
	int s = a;
	for (int c : cuts) {
		out.push_back({s, cs[c].at});
		s = cs[c].at;
	}
	out.push_back({s, b});
	return out;
}

// ------------------------------------------------------------------------------------------------ analysis
inline int clampI(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }

inline std::string stripStress(const std::string& n) {
	std::string s;
	for (char c : n)
		if (c != '\'' && c != ',' && c != '%' && c != '=')
			s += c;
	return s;
}

/** Pitch marks every period t0, each on the nearest peak; the spectrum of the vowel for the display. */
inline void analyse(Unit& u, float t0) {
	u.t0 = t0;
	u.marks.clear();
	int n = (int) u.s.size();
	if (n == 0)
		return;
	int a = u.v1 > u.v0 ? u.v0 : 0, b = u.v1 > u.v0 ? u.v1 : n;
	int m0 = (a + b) / 2;
	int lo = std::max(0, m0 - (int) t0), hi = std::min(n, m0 + (int) t0);
	int anchor = m0;
	float best = -1e9f;
	for (int i = lo; i < hi; i++)
		if (u.s[i] > best) {
			best = u.s[i];
			anchor = i;
		}
	auto peakNear = [&](float c) {
		int w = std::max(1, (int) (t0 * 0.2f));
		int i0 = std::max(0, (int) c - w), i1 = std::min(n - 1, (int) c + w);
		int bi = clampI((int) c, 0, n - 1);
		float bv = -1e9f;
		for (int i = i0; i <= i1; i++)
			if (u.s[i] > bv) {
				bv = u.s[i];
				bi = i;
			}
		return bi;
	};
	std::vector<int> back;
	for (float p = anchor - t0; p > -t0 * 0.5f; p -= t0) {
		int q = p < 0 ? 0 : peakNear(p);
		back.push_back(q);
		p = (float) q;
		if (q == 0)
			break;
	}
	std::reverse(back.begin(), back.end());
	u.marks = back;
	u.marks.push_back(anchor);
	for (float p = anchor + t0; p < n; p += t0) {
		int q = peakNear(p);
		if (q <= u.marks.back())
			q = std::min(n - 1, u.marks.back() + (int) t0);
		u.marks.push_back(q);
		p = (float) q;
	}
	// keep them increasing and apart
	std::vector<int> mk;
	for (int q : u.marks)
		if (mk.empty() || q > mk.back() + (int) (t0 * 0.5f))
			mk.push_back(q);
	u.marks = mk;
	// the vowel's spectrum (display): 512 samples around its middle
	const int N = 512;
	int c = (a + b) / 2;
	float mag[48];
	float mx = 1e-9f;
	for (int k = 0; k < 48; k++) {
		float f = (k + 0.5f) / 48.f * 5000.f;
		float w = 2.f * 3.14159265f * f / 22050.f;
		double re = 0, im = 0;
		for (int i = 0; i < N; i++) {
			int j = c - N / 2 + i;
			if (j < 0 || j >= n)
				continue;
			float win = 0.5f - 0.5f * std::cos(2.f * 3.14159265f * i / (N - 1));
			re += u.s[j] * win * std::cos(w * i);
			im -= u.s[j] * win * std::sin(w * i);
		}
		mag[k] = (float) std::sqrt(re * re + im * im);
	}
	// smooth over about one harmonic spacing (the envelope, not the harmonics)
	float sm[48];
	for (int k = 0; k < 48; k++) {
		float m = 0;
		for (int d = -1; d <= 1; d++)
			m = std::max(m, mag[clampI(k + d, 0, 47)]);
		sm[k] = m;
	}
	for (int k = 0; k < 48; k++) {
		float v = (sm[clampI(k - 1, 0, 47)] + 2 * sm[k] + sm[clampI(k + 1, 0, 47)]) / 4;
		mag[k] = v;
		mx = std::max(mx, v);
	}
	for (int k = 0; k < 48; k++) {
		float db = 20.f * std::log10(mag[k] / mx + 1e-6f);
		u.env[k] = std::max(0.f, 1.f + db / 48.f);
	}
}

/** The end of the sound (after the last sample above a whisper, plus 20 ms). */
inline int soundEnd(const std::vector<float>& s, int sr) {
	int e = (int) s.size();
	while (e > 0 && std::fabs(s[e - 1]) < 0.003f)
		e--;
	return std::min((int) s.size(), e + sr / 50);
}

inline Unit cut(const std::vector<float>& s, const std::vector<Ph>& ph, int a, int b, int va, int vb, int sr, float t0,
                const std::string& text) {
	Unit u;
	int pad = sr * 6 / 1000;
	int x0 = std::max(0, a - pad), x1 = std::min((int) s.size(), b + pad);
	if (x1 <= x0)
		x1 = std::min((int) s.size(), x0 + 1);
	u.s.assign(s.begin() + x0, s.begin() + x1);
	int fi = std::min(pad, (int) u.s.size() / 4);
	for (int i = 0; i < fi; i++) {
		float g = (float) i / fi;
		if (x0 > 0)
			u.s[i] *= g;
		if (x1 < (int) s.size())
			u.s[u.s.size() - 1 - i] *= g;
	}
	u.v0 = va >= 0 ? std::max(0, va - x0) : 0;
	u.v1 = va >= 0 ? std::min((int) u.s.size(), vb - x0) : 0;
	u.text = text;
	for (const Ph& p : ph)
		if (p.pos >= a && p.pos < b) {
			if (!u.phon.empty())
				u.phon += ' ';
			u.phon += stripStress(p.name);
		}
	analyse(u, t0);
	return u;
}

/** One word spoken, cut into its syllables (as many as it has vowels). Returns the units (at least one). */
inline std::vector<Unit> wordSyllables(const std::vector<float>& s, const std::vector<Ph>& ph, int sr, float t0,
                                       const std::vector<std::string>& texts, Unit* whole) {
	int n = (int) s.size();
	int end = soundEnd(s, sr);
	std::vector<int> nuc;
	for (int i = 0; i < (int) ph.size(); i++)
		if (ph[i].vowel)
			nuc.push_back(i);
	int start = ph.empty() ? 0 : std::max(0, ph[0].pos - sr / 100);
	auto vowelEnd = [&](int i) { return i + 1 < (int) ph.size() ? ph[i + 1].pos : end; };
	if (whole) {
		int vi = nuc.empty() ? -1 : nuc.back();
		*whole = cut(s, ph, start, end, vi >= 0 ? ph[vi].pos : -1, vi >= 0 ? vowelEnd(vi) : 0, sr, t0, "");
	}
	std::vector<Unit> out;
	if (nuc.size() <= 1) {
		int vi = nuc.empty() ? -1 : nuc[0];
		out.push_back(cut(s, ph, start, end, vi >= 0 ? ph[vi].pos : -1, vi >= 0 ? vowelEnd(vi) : 0, sr, t0,
		                  texts.empty() ? "" : texts[0]));
		return out;
	}
	std::vector<int> at;  // where each syllable starts
	at.push_back(start);
	for (size_t k = 1; k < nuc.size(); k++) {
		int i = nuc[k - 1], j = nuc[k];
		at.push_back(j - i - 1 == 0 ? ph[j].pos : ph[j - 1].pos);
	}
	at.push_back(std::max(end, at.back() + 1));
	(void) n;
	for (size_t k = 0; k < nuc.size(); k++) {
		int vi = nuc[k];
		std::string t = k < texts.size() ? texts[k] : "";
		out.push_back(cut(s, ph, at[k], at[k + 1], ph[vi].pos, vowelEnd(vi), sr, t0, t));
	}
	return out;
}

/** The whole line: words, syllables, the line in one go. t0: the period eSpeak speaks at (sr / f0). */
inline bool makeLine(Line& L, const std::string& text, const SpeakFn& speak, int sr, float t0) {
	L = Line();
	L.text = text;
	std::vector<float> s;
	std::vector<Ph> ph;
	// the words: runs of anything but spaces
	int i = 0, n = (int) text.size();
	while (i < n) {
		while (i < n && (text[i] == ' ' || text[i] == '\t'))
			i++;
		if (i >= n)
			break;
		int a = i;
		while (i < n && text[i] != ' ' && text[i] != '\t')
			i++;
		int b = i;
		// the letters of the word (no punctuation around it)
		int la = a, lb = b;
		while (la < lb) {
			int k;
			uint32_t cp = utf8At(text, la, k);
			if (isLetterCp(cp) && cp != '\'')
				break;
			la += k;
		}
		while (lb > la) {
			int k = 1;
			while (lb - k > la && ((unsigned char) text[lb - k] & 0xc0) == 0x80)
				k++;
			uint32_t cp = utf8At(text, lb - k, k);
			if (isLetterCp(cp) && cp != '\'')
				break;
			lb -= k;
		}
		if (lb <= la)
			continue;
		bool expl = false;
		std::vector<Piece> ortho = orthoSyllables(text, la, lb, expl);
		std::string spoken;
		for (int k = la; k < lb; k++)
			if (text[k] != '-')
				spoken += text[k];
		std::vector<std::string> texts;
		for (const Piece& p : ortho)
			texts.push_back(text.substr(p.a, p.b - p.a));
		if (!speak(spoken, s, ph) || s.empty())
			continue;
		Unit whole;
		std::vector<Unit> sy = wordSyllables(s, ph, sr, t0, texts, &whole);
		if (expl && sy.size() != ortho.size()) {
			// the hyphens win: each piece spoken by itself
			sy.clear();
			for (size_t k = 0; k < ortho.size(); k++) {
				if (!speak(texts[k], s, ph) || s.empty())
					continue;
				Unit u;
				wordSyllables(s, ph, sr, t0, {texts[k]}, &u);  // the piece whole, its (last) vowel held
				u.text = texts[k];
				sy.push_back(u);
				L.sylAt.push_back({ortho[k].a, ortho[k].b});
			}
		} else {
			for (size_t k = 0; k < sy.size(); k++) {
				if (sy.size() == ortho.size())
					L.sylAt.push_back({ortho[k].a, ortho[k].b});
				else
					L.sylAt.push_back({la, lb});
				if (sy.size() != ortho.size())
					sy[k].text = text.substr(la, lb - la);
			}
		}
		for (Unit& u : sy)
			L.syl.push_back(std::move(u));
		whole.text = text.substr(la, lb - la);
		L.word.push_back(std::move(whole));
		L.wordAt.push_back({la, lb});
	}
	if (L.syl.empty())
		return false;
	// the line in one go (FREE)
	if (speak(text, s, ph) && !s.empty()) {
		int st = ph.empty() ? 0 : std::max(0, ph[0].pos - sr / 100);
		L.whole = cut(s, ph, st, soundEnd(s, sr), -1, 0, sr, t0, text);
	}
	// one loudness for the line: its loudest point at 0.8
	float pk = 1e-6f;
	auto peak = [&](const Unit& u) {
		for (float x : u.s)
			pk = std::max(pk, std::fabs(x));
	};
	for (const Unit& u : L.syl)
		peak(u);
	for (const Unit& u : L.word)
		peak(u);
	float g = std::min(8.f, 0.8f / pk);
	auto scale = [&](Unit& u) {
		for (float& x : u.s)
			x *= g;
	};
	for (Unit& u : L.syl)
		scale(u);
	for (Unit& u : L.word)
		scale(u);
	float pw = 1e-6f;
	for (float x : L.whole.s)
		pw = std::max(pw, std::fabs(x));
	float gw = std::min(8.f, 0.8f / pw);
	for (float& x : L.whole.s)
		x *= gw;
	return true;
}

}  // namespace lied
