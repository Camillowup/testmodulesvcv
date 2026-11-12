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

std::vector<uint8_t> midiFile(const hh::Render& r, float bpm, float swing, const hh::PadSet* pads, int globalPitch) {
	using namespace hh;
	const int PPQ = 96, STEP = PPQ / 4;
	auto at = [&](int s, float off) {
		float sw = (s % 2) ? (2.f * swing - 1.f) : 0.f;
		return (int) std::lround((s + sw + off) * STEP);
	};
	static const int GM[DRUMS] = {36, 38, 39, 37, 42, 46, 45, 56};
	TrackW tempo, drums, bass, mel, pad, chop;
	uint32_t us = (uint32_t) (60000000.f / std::max(20.f, bpm));
	tempo.add(0, 1, {0xff, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us});
	tempo.add(0, 1, {0xff, 0x58, 0x04, 4, 2, 24, 8});
	for (int v = 0; v < DRUMS; v++)
		for (int s = 0; s < LEN; s++) {
			const Hit& h = r.drum[v][s];
			if (h.vel <= 0.f)
				continue;
			int n = std::max(1, (int) h.ratchet);
			for (int k = 0; k < n; k++)
				drums.note(at(s, h.off) + k * STEP / n, STEP / n / 2 + 1, 9, GM[v], (int) (h.vel * 127.f));
		}
	auto line = [&](TrackW& t, const Note* ln, int ch) {
		for (int s = 0; s < LEN; s++)
			if (ln[s].pitch >= 0)
				t.note(at(s, ln[s].off), (int) (ln[s].len * STEP), ch, ln[s].pitch, std::min(127, (int) (ln[s].vel * 127.f) + (ln[s].acc ? 20 : 0)));
	};
	line(bass, r.bass, 0);
	line(mel, r.mel, 1);
	for (int s = 0; s < LEN; s++) {
		const PadEv& e = r.pad[s];
		for (int i = 0; i < PAD_VOICES; i++)
			if (e.pitch[i] >= 0)
				pad.note(at(s, e.off), (int) (e.len * STEP), 2, e.pitch[i], (int) (e.vel * 127.f));
	}
	// chops: pitch bend range ±12 (RPN 0), then per chop its bend and its direction
	chop.add(0, 1, {0xb3, 101, 0});
	chop.add(0, 1, {0xb3, 100, 0});
	chop.add(0, 1, {0xb3, 6, 12});
	chop.add(0, 1, {0xb3, 38, 0});
	for (int s = 0; s < LEN; s++) {
		const ChopEv& e = r.chop[s];
		if (e.pad < 0)
			continue;
		int semis = clamp(e.pitch + (pads ? pads[e.pad].pitch : 0) + globalPitch, -12, 12);
		int bend = clamp(8192 + semis * 8192 / 12, 0, 16383);
		bool rev = e.rev != (pads && pads[e.pad].rev);
		int t = at(s, e.off);
		chop.add(t, 1, {0xe3, (uint8_t) (bend & 0x7f), (uint8_t) (bend >> 7)});
		chop.add(t, 1, {0xb3, 20, (uint8_t) (rev ? 127 : 0)});
		int n = std::max(1, (int) e.ratchet);
		for (int k = 0; k < n; k++)
			chop.note(t + k * (int) (e.len * STEP) / n, std::max(1, (int) (e.len * STEP) / n - 1), 3, 36 + e.pad,
			          (int) (e.vel * 127.f));
	}
	std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 6, 0, (uint8_t) PPQ};
	for (auto* tp : {&tempo, &drums, &bass, &mel, &pad, &chop}) {
		const char* nm = tp == &tempo ? "GENRA HIP-HOP" : tp == &drums ? "Drums" : tp == &bass ? "Bass" : tp == &mel ? "Melody" : tp == &pad ? "Pads" : "Chop";
		std::vector<uint8_t> b = tp->bytes(nm);
		f.insert(f.end(), b.begin(), b.end());
	}
	return f;
}

} // namespace hhx
