// SPDX-License-Identifier: GPL-3.0-only
#include "Export.hpp"
#include <osdialog.h>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#if defined ARCH_WIN
#include <windows.h>
#endif

namespace hhx {

static std::string settingsPath() { return asset::user("Genra/plugin-settings.json"); }

static std::string documents() {
#if defined ARCH_WIN
	typedef HRESULT(WINAPI * GetPathW)(HWND, int, HANDLE, DWORD, LPWSTR);
	if (HMODULE sh = LoadLibraryW(L"shell32.dll")) {
		GetPathW get = (GetPathW) (void*) GetProcAddress(sh, "SHGetFolderPathW");
		wchar_t buf[MAX_PATH] = {};
		bool ok = get && get(NULL, 0x0005 /* CSIDL_PERSONAL */, NULL, 0, buf) == 0 && buf[0];
		FreeLibrary(sh);
		if (ok)
			return string::UTF16toUTF8(buf);
	}
	if (const char* up = std::getenv("USERPROFILE"))
		return system::join(up, "Documents");
	return asset::user("");
#else
	if (const char* home = std::getenv("HOME"))
		return system::join(home, "Documents");
	return asset::user("");
#endif
}

static std::string setting(const char* key) {
	std::string dir;
	if (json_t* root = json_load_file(settingsPath().c_str(), 0, NULL)) {
		if (json_t* j = json_object_get(root, key))
			if (json_is_string(j))
				dir = json_string_value(j);
		json_decref(root);
	}
	return dir;
}

static void setSetting(const char* key, const std::string& v) {
	json_t* root = json_load_file(settingsPath().c_str(), 0, NULL);
	if (!root || !json_is_object(root)) {
		if (root)
			json_decref(root);
		root = json_object();
	}
	json_object_set_new(root, key, json_string(v.c_str()));
	system::createDirectories(system::getDirectory(settingsPath()));
	json_dump_file(root, settingsPath().c_str(), JSON_INDENT(2));
	json_decref(root);
}

std::string midiFolder() {
	std::string d = setting("midiExportFolder");
	return d.empty() ? system::join(documents(), "Midi Files from Genra") : d;
}
std::string chopFolder() {
	std::string d = setting("chopExportFolder");
	return d.empty() ? system::join(documents(), "Chops from Genra") : d;
}

static void pick(const char* key, const std::string& cur) {
	char* p = osdialog_file(OSDIALOG_OPEN_DIR, cur.c_str(), NULL, NULL);
	if (p) {
		setSetting(key, p);
		std::free(p);
	}
}
void pickMidiFolder() { pick("midiExportFolder", midiFolder()); }
void pickChopFolder() { pick("chopExportFolder", chopFolder()); }
std::string sampleFolder() { return setting("hiphopSampleFolder"); }
void pickSampleFolder() {
	std::string cur = sampleFolder();
	pick("hiphopSampleFolder", cur.empty() ? documents() : cur);
}

std::string cleanName(const std::string& s) {
	std::string o;
	for (char c : s) {
		if (c == ' ' || c == '&' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
		    c == '>' || c == '|')
			c = '-';
		o += c;
	}
	return o.empty() ? std::string("genra") : o;
}

std::string freeName(const std::string& dir, const std::string& name, const std::string& ext) {
	std::time_t t = std::time(nullptr);
	char day[16] = "";
	if (std::tm* lt = std::localtime(&t))
		std::strftime(day, sizeof(day), "%Y-%m-%d", lt);
	std::string base = string::f("%s_%s", day, cleanName(name).c_str());
	std::string f = base + ext;
	for (int n = 2; system::exists(system::join(dir, f)) && n < 1000; n++)
		f = string::f("%s-%d%s", base.c_str(), n, ext.c_str());
	return f;
}

// ---------------------------------------------------------------- MIDI
namespace {
struct Ev {
	int tick;
	int order; // note-offs before note-ons at the same tick
	std::vector<uint8_t> bytes;
};
struct TrackW {
	std::vector<Ev> ev;
	void add(int tick, int order, std::initializer_list<uint8_t> b) { ev.push_back({std::max(0, tick), order, b}); }
	void note(int tick, int len, int ch, int note, int vel) {
		note = clamp(note, 0, 127);
		add(tick, 1, {(uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) clamp(vel, 1, 127)});
		add(tick + std::max(1, len), 0, {(uint8_t) (0x80 | ch), (uint8_t) note, 0});
	}
	std::vector<uint8_t> bytes(const char* name) {
		std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
			return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
		});
		std::vector<uint8_t> d;
		auto vlq = [&](uint32_t v) {
			uint8_t buf[5];
			int n = 0;
			buf[n++] = v & 0x7f;
			while (v >>= 7)
				buf[n++] = (uint8_t) (0x80 | (v & 0x7f));
			while (n)
				d.push_back(buf[--n]);
		};
		size_t nl = strlen(name);
		vlq(0);
		d.push_back(0xff), d.push_back(0x03), d.push_back((uint8_t) nl);
		d.insert(d.end(), name, name + nl);
		int last = 0;
		for (Ev& e : ev) {
			vlq((uint32_t) (e.tick - last));
			last = e.tick;
			d.insert(d.end(), e.bytes.begin(), e.bytes.end());
		}
		vlq(0);
		d.push_back(0xff), d.push_back(0x2f), d.push_back(0x00);
		std::vector<uint8_t> out = {'M', 'T', 'r', 'k'};
		uint32_t n = (uint32_t) d.size();
		out.push_back(n >> 24), out.push_back(n >> 16), out.push_back(n >> 8), out.push_back(n);
		out.insert(out.end(), d.begin(), d.end());
		return out;
	}
};
} // namespace

std::vector<uint8_t> midiFile(const hh::Render& r, float bpm, float swing, const hh::PadSet* pads, int globalPitch,
                              const hh::Pattern& pat, int onlyTrack) {
	MidiPart one;
	one.r = &r;
	one.pat = &pat;
	return midiSong(std::vector<MidiPart>{one}, bpm, swing, pads, globalPitch, onlyTrack);
}

std::vector<uint8_t> midiSong(const std::vector<MidiPart>& parts, float bpm, float swing, const hh::PadSet* pads,
                              int globalPitch, int onlyTrack) {
	using namespace hh;
	const int PPQ = 96, STEP = PPQ / 4;
	auto want = [&](int t) { return onlyTrack < 0 || onlyTrack == t; };
	static const int GM[DRUMS] = {36, 38, 39, 37, 42, 46, 45, 56};
	TrackW tempo, drums, bass, mel, pad, chop;
	uint32_t us = (uint32_t) (60000000.f / std::max(20.f, bpm));
	tempo.add(0, 1, {0xff, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us});
	tempo.add(0, 1, {0xff, 0x58, 0x04, 4, 2, 24, 8});
	// chops: pitch bend range ±12 (RPN 0), then per chop its bend and its direction
	chop.add(0, 1, {0xb3, 101, 0});
	chop.add(0, 1, {0xb3, 100, 0});
	chop.add(0, 1, {0xb3, 6, 12});
	chop.add(0, 1, {0xb3, 38, 0});
	int base = 0; // steps written so far
	for (const MidiPart& mp : parts) {
		const Render& r = *mp.r;
		const Pattern& pat = *mp.pat;
		int from = mp.from, n = mp.steps > 0 ? mp.steps : LEN;
		if (!mp.marker.empty()) {
			std::vector<uint8_t> mk = {0xff, 0x06, (uint8_t) std::min<size_t>(mp.marker.size(), 120)};
			mk.insert(mk.end(), mp.marker.begin(), mp.marker.begin() + mk[2]);
			tempo.ev.push_back({base * STEP, 1, mk});
		}
		// each track (drum voice) in its own length and swing
		auto lenOf = [&](int t, int v) {
			int L = pat.lenTrack[t];
			if (t == T_DRUMS && v >= 0 && pat.lenDrum[v])
				L = pat.lenDrum[v];
			return (L > 0 && L < LEN) ? L : LEN;
		};
		auto swOf = [&](int t, int v) {
			float sw = pat.swTrack[t];
			if (t == T_DRUMS && v >= 0 && pat.swDrum[v] > 0.f)
				sw = pat.swDrum[v];
			return sw > 0.f ? sw : swing;
		};
		int curT = 0, curV = -1;
		// i: the step in this part (written at base + i), s: the step in the loop it plays
		auto at = [&](int i, int s, float off) {
			int L = lenOf(curT, curV);
			float sw = ((s % L) % 2) ? (2.f * swOf(curT, curV) - 1.f) : 0.f;
			return (int) std::lround((base + i + sw + off) * STEP);
		};
		for (int v = 0; v < DRUMS && want(T_DRUMS); v++)
			for (int i = 0; i < n; i++) {
				int s = (from + i) % LEN;
				curT = T_DRUMS, curV = v;
				const Hit& h = r.drum[v][s % lenOf(T_DRUMS, v)];
				if (h.vel <= 0.f)
					continue;
				int k = std::max(1, (int) h.ratchet);
				for (int q = 0; q < k; q++)
					drums.note(at(i, s, h.off) + q * STEP / k, STEP / k / 2 + 1, 9, GM[v], (int) (h.vel * 127.f));
			}
		auto line = [&](TrackW& t, const Note* ln, int ch, int tr) {
			if (!want(tr))
				return;
			curT = tr, curV = -1;
			for (int i = 0; i < n; i++) {
				int s = (from + i) % LEN;
				const Note& nt = ln[s % lenOf(tr, -1)];
				if (nt.pitch >= 0)
					t.note(at(i, s, nt.off), (int) (nt.len * STEP), ch, nt.pitch,
					       std::min(127, (int) (nt.vel * 127.f) + (nt.acc ? 20 : 0)));
			}
		};
		line(bass, r.bass, 0, T_BASS);
		line(mel, r.mel, 1, T_MEL);
		curT = T_PAD, curV = -1;
		for (int i = 0; i < n && want(T_PAD); i++) {
			int s = (from + i) % LEN;
			const PadEv& e = r.pad[s % lenOf(T_PAD, -1)];
			for (int q = 0; q < PAD_VOICES; q++)
				if (e.pitch[q] >= 0)
					pad.note(at(i, s, e.off), (int) (e.len * STEP), 2, e.pitch[q], (int) (e.vel * 127.f));
		}
		curT = T_CHOP, curV = -1;
		for (int i = 0; i < n && want(T_CHOP); i++) {
			int s = (from + i) % LEN;
			const ChopEv& e = r.chop[s % lenOf(T_CHOP, -1)];
			if (e.pad < 0)
				continue;
			int semis = clamp(e.pitch + (pads ? pads[e.pad].pitch : 0) + globalPitch, -12, 12);
			int bend = clamp(8192 + semis * 8192 / 12, 0, 16383);
			bool rev = e.rev != (pads && pads[e.pad].rev);
			int t = at(i, s, e.off);
			chop.add(t, 1, {0xe3, (uint8_t) (bend & 0x7f), (uint8_t) (bend >> 7)});
			chop.add(t, 1, {0xb3, 20, (uint8_t) (rev ? 127 : 0)});
			int k = std::max(1, (int) e.ratchet);
			for (int q = 0; q < k; q++)
				chop.note(t + q * (int) (e.len * STEP) / k, std::max(1, (int) (e.len * STEP) / k - 1), 3, 36 + e.pad,
				          (int) (e.vel * 127.f));
		}
		base += n;
	}
	std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 6, 0, (uint8_t) PPQ};
	for (auto* tp : {&tempo, &drums, &bass, &mel, &pad, &chop}) {
		const char* nm = tp == &tempo ? "GENRA CHOP" : tp == &drums ? "Drums" : tp == &bass ? "Bass" : tp == &mel ? "Melody" : tp == &pad ? "Pads" : "Chop";
		std::vector<uint8_t> b = tp->bytes(nm);
		f.insert(f.end(), b.begin(), b.end());
	}
	return f;
}

} // namespace hhx
