// SPDX-License-Identifier: GPL-3.0-only
// GENRA MAP-X: a MIDI mapper. Two MIDI inputs (IN 1, IN 2); it understands notes, CCs, program changes and SysEx (VCV's
// MIDI-MAP only CCs), so the buttons and pads of a controller can press GENRA's buttons.
//
//  - The ready-made map (field MAP): Akai MIDImix and Akai MPD218 (factory settings) on the GENRA main module, found in
//    the rack by itself. OFF, both, or only one of them. The controls the map leaves free are listed as FREE.
//    MPD218 bank B plays the looper SCHLEIFE (GENRA Test), when there is one in the rack (2.1.0).
//  - Own mappings: LEARN, move a control, click a parameter of any module (or the other way round). An own mapping always
//    wins over the ready-made map; CLEAR on a control of the ready-made map switches it off (OFF), CLEAR again brings it
//    back. UNDO takes back the last change of the own mappings.
//  - The way a button acts (the lamps): ONCE (a short press of the parameter), ON/OFF (each press toggles), HOLD (on
//    while held), STEP (each press the next value). Knobs and faders (CCs) set the value directly (no lamp lit); a lamp
//    turns a CC into a button (on above the middle). A click on the lit lamp goes back to direct.
//
// Threads: the audio thread reads the MIDI and sets the parameters through Rack's ParamHandles (as VCV MIDI-MAP); the
// table it works from is built on the UI thread (the widget's step) and swapped in whole (two buffers).
#include "plugin.hpp"
#include "../genra/ui/GenraUI.hpp"
#include "../genra/Follow.hpp"
#include "../genra/jiant/JiantUI.hpp"
#include "MapXLayout.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <vector>

namespace L = mapxl;

namespace mapx {

static const NVGcolor ACC = nvgRGB(0x7b, 0x6c, 0xff);
static const int NDEV = 2;    // IN 1, IN 2
static const int MAXH = 112;  // parameter handles (targets)
static const int MAXA = 160;  // active mappings

enum Kind : int8_t { K_NONE, K_NOTE, K_CC, K_PC, K_SYSEX };
enum Mode : int8_t { M_ONCE, M_TOGGLE, M_HOLD, M_STEP, M_DIRECT };
static const char* const MODE_NAME[5] = {"ONCE", "ON/OFF", "HOLD", "STEP", ""};
enum DevType { D_MIX, D_MPD, D_OTHER };
enum Preset { P_OFF, P_BOTH, P_MIX, P_MPD, NUM_PRESETS };
static const char* const PRESET_NAME[NUM_PRESETS] = {"OFF", "MIDI MIX+MPD 218", "MIDI MIX", "MPD 218"};

/** A control of a controller: which input, what kind of message, its number (SysEx: a hash of the bytes). */
struct Src {
	int8_t dev = -1;
	int8_t kind = K_NONE;
	int32_t num = 0;
	bool same(const Src& o) const { return dev == o.dev && kind == o.kind && num == o.num; }
	bool valid() const { return dev >= 0 && kind != K_NONE; }
};

/** An own mapping (saved with the patch). off: switches a control of the ready-made map off. */
struct UserMap {
	Src src;
	int64_t moduleId = -1;
	int paramId = -1;
	int8_t mode = M_DIRECT;
	int8_t dir = 1;
	bool off = false;
};

/** What the audio thread works from. */
struct Active {
	Src src;
	int slot = -1;
	int8_t mode = M_DIRECT;
	int8_t dir = 1;
};

// ---------------------------------------------------------------- the ready-made map: GENRA's parameters
// GENRA's parameter ids (src/genra/GenraModule.hpp in Genra-for-Vcv; they never change, new ones are appended)
enum GenraParam {
	G_GENRE = 0, G_KEY = 1, G_SCALE = 2, G_BPM = 3, G_SWING = 4, G_COMPLEX = 5, G_COLOR = 6, G_MUTATE = 7, G_LENGTH = 8,
	G_NEW = 9, G_RUN = 10, G_CHORDLOCK = 11, G_DENS = 12, G_FILL = 16, G_GHOST = 17, G_MOVE = 18, G_RANGE = 19,
	G_VOICING = 20, G_OCT = 21, G_MUTE = 24, G_LOCK = 28, G_VARIANT = 32, G_CHORDNEW = 33, G_TNEW = 34, G_FILLPAD = 38,
	G_STOP = 39, G_TAP = 40, G_MAKESONG = 41,
	G_TGENRE = 42, // (2.41.2: the genre fields of the four tracks for MIDI-MAP, after GENRA's own parameters)
	G_FREE = -1
};

struct PEntry {
	int8_t dev;  // D_MIX or D_MPD
	int8_t kind;
	int16_t num;
	int16_t param; // G_FREE: listed, not mapped
	int8_t mode;
	int8_t dir;
	const char* name; // what it does (the display)
	int8_t target;    // T_GENRA (0, the default) or T_LOOP (SCHLEIFE)
};
enum Target { T_GENRA, T_LOOP, T_FOLLOW }; // T_GENRA: the main module (GENRA or GENRA TECHNO); T_FOLLOW: param =
                                            // a TECHNO track, the preset of the instrument that follows it

// SCHLEIFE's parameter ids (src/schleife/Schleife.cpp in GENRA Test; new ones are appended there)
enum LoopParam { S_REC = 8, S_UNDO = 12, S_CLEAR = 16, S_CLRALL = 21 };

// Akai MIDImix, factory settings: knobs (top, middle, bottom) and faders CC, MUTE / REC ARM / SOLO+MUTE / BANK notes
static const int MIX_KNOB[8] = {16, 20, 24, 28, 46, 50, 54, 58}; // + 0, 1, 2 the knobs, + 3 the fader
static const int MPD_KNOB[6] = {3, 9, 12, 13, 14, 15};

static void addLoopBank(std::vector<PEntry>& t, int16_t run, int16_t stop, int8_t mode);

static std::vector<PEntry> presetTable() {
	std::vector<PEntry> t;
	static const char* TR[4] = {"DR", "BS", "ML", "PD"};
	// channels 1-4: the four tracks
	for (int i = 0; i < 4; i++) {
		static const int ROW2[4] = {G_FILL, G_MOVE, G_RANGE, G_VOICING};
		static const char* ROW2N[4] = {"FILL", "MOVE", "RANGE", "VOICING"};
		t.push_back({D_MIX, K_CC, (int16_t) MIX_KNOB[i], (int16_t) (G_TGENRE + i), M_DIRECT, 1, nullptr, T_GENRA});
		t.back().name = strdup(string::f("%s GENRE", TR[i]).c_str());
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[i] + 1), (int16_t) ROW2[i], M_DIRECT, 1, ROW2N[i], T_GENRA});
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[i] + 2), (int16_t) (i == 0 ? G_GHOST : G_OCT + i - 1), M_DIRECT, 1, nullptr, T_GENRA});
		t.back().name = i == 0 ? "GHOST" : strdup(string::f("%s OCT", TR[i]).c_str());
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[i] + 3), (int16_t) (G_DENS + i), M_DIRECT, 1, nullptr, T_GENRA});
		t.back().name = strdup(string::f("%s DENS", TR[i]).c_str());
		t.push_back({D_MIX, K_NOTE, (int16_t) (1 + 3 * i), (int16_t) (G_MUTE + i), M_TOGGLE, 1, nullptr, T_GENRA});
		t.back().name = strdup(string::f("%s MUTE", TR[i]).c_str());
		t.push_back({D_MIX, K_NOTE, (int16_t) (3 + 3 * i), (int16_t) (G_TNEW + i), M_ONCE, 1, nullptr, T_GENRA});
		t.back().name = strdup(string::f("%s NEW", TR[i]).c_str());
		t.push_back({D_MIX, K_NOTE, (int16_t) (2 + 3 * i), (int16_t) (G_LOCK + i), M_TOGGLE, 1, nullptr, T_GENRA});
		t.back().name = strdup(string::f("%s LOCK", TR[i]).c_str());
	}
	// channels 5-8: global
	static const int16_t KN[4][3] = {{G_GENRE, G_KEY, G_SCALE}, {G_VARIANT, G_LENGTH, G_FREE}, {G_BPM, G_FREE, G_FREE},
	                                 {G_FREE, G_FREE, G_FREE}};
	static const char* KNN[4][3] = {{"GENRE", "KEY", "SCALE"}, {"VARIANT", "LENGTH", nullptr}, {"BPM", nullptr, nullptr},
	                                {nullptr, nullptr, nullptr}};
	static const int16_t FD[4] = {G_COMPLEX, G_COLOR, G_MUTATE, G_SWING};
	static const char* FDN[4] = {"COMPLEX", "COLOR", "MUTATE", "SWING"};
	static const int16_t MU[4] = {G_NEW, G_CHORDNEW, G_CHORDLOCK, G_MAKESONG};
	static const int8_t MUM[4] = {M_ONCE, M_ONCE, M_TOGGLE, M_ONCE};
	static const char* MUN[4] = {"NEW ALL", "HARM NEW", "HARM LOCK", "MAKE SONG"};
	static const int16_t RE[4] = {G_RUN, G_STOP, G_FILLPAD, G_TAP};
	static const int8_t REM[4] = {M_ONCE, M_ONCE, M_HOLD, M_ONCE};
	static const char* REN[4] = {"RUN", "STOP", "FILL", "TAP"};
	for (int i = 0; i < 4; i++) {
		int c = 4 + i;
		for (int k = 0; k < 3; k++)
			t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + k), KN[i][k], M_DIRECT, 1, KNN[i][k], T_GENRA});
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + 3), FD[i], M_DIRECT, 1, FDN[i], T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (1 + 3 * c), MU[i], MUM[i], 1, MUN[i], T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (3 + 3 * c), RE[i], REM[i], 1, REN[i], T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (2 + 3 * c), G_FREE, M_ONCE, 1, nullptr, T_GENRA});
	}
	t.push_back({D_MIX, K_CC, 62, G_FREE, M_DIRECT, 1, nullptr, T_GENRA});        // master fader
	t.push_back({D_MIX, K_NOTE, 25, G_GENRE, M_STEP, -1, "GENRE <", T_GENRA});    // BANK LEFT
	t.push_back({D_MIX, K_NOTE, 26, G_GENRE, M_STEP, 1, "GENRE >", T_GENRA});     // BANK RIGHT

	// Akai MPD218, preset 1: pads bank A notes 36-51 from the bottom left, knobs CC 3, 9, 12-15
	static const int16_t PAD[16] = {G_RUN, G_STOP, G_FILLPAD, G_TAP, G_TNEW, G_TNEW + 1, G_TNEW + 2, G_TNEW + 3,
	                                G_MUTE, G_MUTE + 1, G_MUTE + 2, G_MUTE + 3, G_NEW, G_CHORDNEW, G_CHORDLOCK, G_MAKESONG};
	static const int8_t PADM[16] = {M_ONCE, M_ONCE, M_HOLD, M_ONCE, M_ONCE, M_ONCE, M_ONCE, M_ONCE,
	                                M_TOGGLE, M_TOGGLE, M_TOGGLE, M_TOGGLE, M_ONCE, M_ONCE, M_TOGGLE, M_ONCE};
	static const char* PADN[16] = {"RUN", "STOP", "FILL", "TAP", "DR NEW", "BS NEW", "ML NEW", "PD NEW",
	                               "DR MUTE", "BS MUTE", "ML MUTE", "PD MUTE", "NEW ALL", "HARM NEW", "HARM LOCK", "MAKE SONG"};
	for (int i = 0; i < 16; i++)
		t.push_back({D_MPD, K_NOTE, (int16_t) (36 + i), PAD[i], PADM[i], 1, PADN[i], T_GENRA});
	static const int16_t MK[6] = {G_GENRE, G_VARIANT, G_COMPLEX, G_COLOR, G_MUTATE, G_SWING};
	static const char* MKN[6] = {"GENRE", "VARIANT", "COMPLEX", "COLOR", "MUTATE", "SWING"};
	for (int i = 0; i < 6; i++)
		t.push_back({D_MPD, K_CC, (int16_t) MPD_KNOB[i], MK[i], M_DIRECT, 1, MKN[i], T_GENRA});

	addLoopBank(t, G_RUN, G_STOP, M_ONCE);
	return t;
}

/** MPD218 bank B (notes 52-67): the looper SCHLEIFE. A column per track: REC/DUB, UNDO, CLEAR from the bottom; the top
 * row CLR ALL (press twice), RUN and STOP of the main module (GENRA: RUN / STOP, GENRA TECHNO: PLAY / STOP), one free. */
static void addLoopBank(std::vector<PEntry>& t, int16_t run, int16_t stop, int8_t mode) {
	static const char* RN[4] = {"T1 REC/DUB", "T2 REC/DUB", "T3 REC/DUB", "T4 REC/DUB"};
	static const char* UN[4] = {"T1 UNDO", "T2 UNDO", "T3 UNDO", "T4 UNDO"};
	static const char* CN[4] = {"T1 CLEAR", "T2 CLEAR", "T3 CLEAR", "T4 CLEAR"};
	for (int i = 0; i < 4; i++) {
		t.push_back({D_MPD, K_NOTE, (int16_t) (52 + i), (int16_t) (S_REC + i), M_ONCE, 1, RN[i], T_LOOP});
		t.push_back({D_MPD, K_NOTE, (int16_t) (56 + i), (int16_t) (S_UNDO + i), M_ONCE, 1, UN[i], T_LOOP});
		t.push_back({D_MPD, K_NOTE, (int16_t) (60 + i), (int16_t) (S_CLEAR + i), M_ONCE, 1, CN[i], T_LOOP});
	}
	t.push_back({D_MPD, K_NOTE, 64, S_CLRALL, M_ONCE, 1, "CLR ALL", T_LOOP});
	t.push_back({D_MPD, K_NOTE, 65, run, mode, 1, "RUN", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 66, stop, mode, 1, "STOP", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 67, G_FREE, M_ONCE, 1, nullptr, T_GENRA});
}

static const std::vector<PEntry>& PRESET() {
	static std::vector<PEntry> t = presetTable();
	return t;
}

// ---------------------------------------------------------------- the ready-made map for GENRA TECHNO (2.2.0)
// GENRA TECHNO's parameter ids (src/TechnoModule.hpp in Technogen; new ones are appended there). Its tracks: 0 drums,
// 1 bass, 2 stab, 3 lead, 4 atmo. Its buttons are read where they are drawn (60 times a second), so MAP-X holds them as
// long as the pad is held (M_HOLD) instead of a short press.
enum TechnoParam {
	X_BPM = 0, X_SWING = 1, X_ENERGY = 2, X_EVOLVE = 3, X_LENGTH = 4, X_DENS = 5, X_FLAV = 10, X_LEN = 15, X_BTN = 20,
	X_FLD = 49, X_MODETGL = 59
};
enum TechnoBtn {
	XB_PLAY = 0, XB_STOP, XB_BUILD, XB_DROP, XB_BREAK, XB_FILL, XB_NEWALL, XB_MUTATE, XB_LOCKALL, XB_UNDO, XB_NEW,
	XB_LOCK = XB_NEW + 5, XB_MUTE = XB_LOCK + 5, XB_PREV = XB_MUTE + 5, XB_NEXT, XB_PATTERN, XB_LIVE
};
enum TechnoField { XF_STYLE = 0, XF_KEY, XF_SCALE, XF_FEEL, XF_BUILD, XF_TYPE };
// the MIDImix channels 1-5 and the pad columns: drums, bass, lead, atmo, stab (stab last)
static const int X_ORDER[5] = {0, 1, 3, 4, 2};
static const char* const X_TR[5] = {"DR", "BS", "ST", "LD", "AT"};
// GENRA TECHNO track -> the track an instrument follows (follow::Track), as GenraTechno::rollPresets
static const int X_TO_FOLLOW[5] = {follow::T_DRUMS, follow::T_BASS, follow::T_STAB, follow::T_MEL, follow::T_PAD};

static const char* xname(const char* what, int track) {
	return strdup(string::f("%s %s", X_TR[track], what).c_str());
}

static std::vector<PEntry> technoTable() {
	std::vector<PEntry> t;
	static const char* FLAVN[5] = {"GROOVE", "ACID", "DUB", "LOOP", "MOVE"};
	// MIDImix channels 1-5: the tracks. Knobs TYPE, the track's own knob, PRESET (of the instrument that follows);
	// fader DENSITY; MUTE, REC ARM = NEW, SOLO+MUTE = LOCK
	for (int c = 0; c < 5; c++) {
		int k = X_ORDER[c];
		t.push_back({D_MIX, K_CC, (int16_t) MIX_KNOB[c], (int16_t) (X_FLD + XF_TYPE + k), M_DIRECT, 1, xname("TYPE", k), T_GENRA});
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + 1), (int16_t) (X_FLAV + k), M_DIRECT, 1, xname(FLAVN[k], k), T_GENRA});
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + 2), (int16_t) k, M_DIRECT, 1, xname("PRESET", k), T_FOLLOW});
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + 3), (int16_t) (X_DENS + k), M_DIRECT, 1, xname("DENS", k), T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (1 + 3 * c), (int16_t) (X_BTN + XB_MUTE + k), M_HOLD, 1, xname("MUTE", k), T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (3 + 3 * c), (int16_t) (X_BTN + XB_NEW + k), M_HOLD, 1, xname("NEW", k), T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (2 + 3 * c), (int16_t) (X_BTN + XB_LOCK + k), M_HOLD, 1, xname("LOCK", k), T_GENRA});
	}
	// channels 6-8: only what is not on the MPD218 (the rest is free for LEARN)
	static const int16_t KN[3][3] = {{X_FLD + XF_KEY, X_FLD + XF_SCALE, G_FREE}, {X_FLD + XF_BUILD, X_LENGTH, G_FREE},
	                                 {G_FREE, G_FREE, G_FREE}};
	static const char* KNN[3][3] = {{"KEY", "SCALE", nullptr}, {"BUILD", "LENGTH", nullptr}, {nullptr, nullptr, nullptr}};
	static const int16_t MU[3] = {X_BTN + XB_NEWALL, X_BTN + XB_MUTATE, X_BTN + XB_LOCKALL};
	static const char* MUN[3] = {"NEW ALL", "MUTATE", "LOCK ALL"};
	static const int16_t RE[3] = {X_BTN + XB_PLAY, X_BTN + XB_STOP, X_BTN + XB_UNDO};
	static const char* REN[3] = {"PLAY", "STOP", "UNDO"};
	for (int i = 0; i < 3; i++) {
		int c = 5 + i;
		for (int k = 0; k < 3; k++)
			t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + k), KN[i][k], M_DIRECT, 1, KNN[i][k], T_GENRA});
		t.push_back({D_MIX, K_CC, (int16_t) (MIX_KNOB[c] + 3), G_FREE, M_DIRECT, 1, nullptr, T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (1 + 3 * c), MU[i], M_HOLD, 1, MUN[i], T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (3 + 3 * c), RE[i], M_HOLD, 1, REN[i], T_GENRA});
		t.push_back({D_MIX, K_NOTE, (int16_t) (2 + 3 * c), G_FREE, M_HOLD, 1, nullptr, T_GENRA});
	}
	t.push_back({D_MIX, K_CC, 62, G_FREE, M_DIRECT, 1, nullptr, T_GENRA});                       // master fader
	t.push_back({D_MIX, K_NOTE, 25, X_BTN + XB_PREV, M_HOLD, 1, "PREV", T_GENRA});               // BANK LEFT
	t.push_back({D_MIX, K_NOTE, 26, X_BTN + XB_NEXT, M_HOLD, 1, "NEXT", T_GENRA});               // BANK RIGHT

	// MPD218 bank A (notes 36-51, from the bottom left): PLAY STOP UNDO MUTATE · NEW of four tracks · MUTE of the same ·
	// NEW ALL, PATTERN/LIVE, free, MUTE STAB
	static const int16_t A[4] = {X_BTN + XB_PLAY, X_BTN + XB_STOP, X_BTN + XB_UNDO, X_BTN + XB_MUTATE};
	static const char* AN[4] = {"PLAY", "STOP", "UNDO", "MUTATE"};
	for (int i = 0; i < 4; i++)
		t.push_back({D_MPD, K_NOTE, (int16_t) (36 + i), A[i], M_HOLD, 1, AN[i], T_GENRA});
	for (int c = 0; c < 4; c++) {
		int k = X_ORDER[c];
		t.push_back({D_MPD, K_NOTE, (int16_t) (40 + c), (int16_t) (X_BTN + XB_NEW + k), M_HOLD, 1, xname("NEW", k), T_GENRA});
		t.push_back({D_MPD, K_NOTE, (int16_t) (44 + c), (int16_t) (X_BTN + XB_MUTE + k), M_HOLD, 1, xname("MUTE", k), T_GENRA});
	}
	t.push_back({D_MPD, K_NOTE, 48, X_BTN + XB_NEWALL, M_HOLD, 1, "NEW ALL", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 49, X_MODETGL, M_HOLD, 1, "PATTERN/LIVE", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 50, G_FREE, M_HOLD, 1, nullptr, T_GENRA});
	t.push_back({D_MPD, K_NOTE, 51, X_BTN + XB_MUTE + 2, M_HOLD, 1, xname("MUTE", 2), T_GENRA});
	// the six knobs
	static const int16_t MK[6] = {X_FLD + XF_STYLE, X_ENERGY, X_EVOLVE, X_SWING, X_FLD + XF_FEEL, X_BPM};
	static const char* MKN[6] = {"STYLE", "ENERGY", "EVOLVE", "SWING", "FEEL", "BPM"};
	for (int i = 0; i < 6; i++)
		t.push_back({D_MPD, K_CC, (int16_t) MPD_KNOB[i], MK[i], M_DIRECT, 1, MKN[i], T_GENRA});

	// bank B: SCHLEIFE, RUN / STOP = TECHNO's PLAY / STOP
	addLoopBank(t, X_BTN + XB_PLAY, X_BTN + XB_STOP, M_HOLD);

	// bank C (notes 68-83): BUILD DROP BREAK FILL (held: they stay) · LOCK of four tracks · NEW STAB, LOCK STAB, LOCK ALL,
	// free · PREV, NEXT, free, free
	static const int16_t C1[4] = {X_BTN + XB_BUILD, X_BTN + XB_DROP, X_BTN + XB_BREAK, X_BTN + XB_FILL};
	static const char* C1N[4] = {"BUILD", "DROP", "BREAK", "FILL"};
	for (int i = 0; i < 4; i++)
		t.push_back({D_MPD, K_NOTE, (int16_t) (68 + i), C1[i], M_HOLD, 1, C1N[i], T_GENRA});
	for (int c = 0; c < 4; c++) {
		int k = X_ORDER[c];
		t.push_back({D_MPD, K_NOTE, (int16_t) (72 + c), (int16_t) (X_BTN + XB_LOCK + k), M_HOLD, 1, xname("LOCK", k), T_GENRA});
	}
	t.push_back({D_MPD, K_NOTE, 76, X_BTN + XB_NEW + 2, M_HOLD, 1, xname("NEW", 2), T_GENRA});
	t.push_back({D_MPD, K_NOTE, 77, X_BTN + XB_LOCK + 2, M_HOLD, 1, xname("LOCK", 2), T_GENRA});
	t.push_back({D_MPD, K_NOTE, 78, X_BTN + XB_LOCKALL, M_HOLD, 1, "LOCK ALL", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 79, G_FREE, M_HOLD, 1, nullptr, T_GENRA});
	t.push_back({D_MPD, K_NOTE, 80, X_BTN + XB_PREV, M_HOLD, 1, "PREV", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 81, X_BTN + XB_NEXT, M_HOLD, 1, "NEXT", T_GENRA});
	t.push_back({D_MPD, K_NOTE, 82, G_FREE, M_HOLD, 1, nullptr, T_GENRA});
	t.push_back({D_MPD, K_NOTE, 83, G_FREE, M_HOLD, 1, nullptr, T_GENRA});
	return t;
}

static const std::vector<PEntry>& TECHNO() {
	static std::vector<PEntry> t = technoTable();
	return t;
}

/** Which ready-made map (right-click): from what is in the rack, or one of the two for good. */
enum Engine { E_AUTO, E_GENRA, E_TECHNO, NUM_ENGINES };
static const char* const ENGINE_NAME[NUM_ENGINES] = {"Automatic (what is in the rack)", "GENRA", "GENRA TECHNO"};

/** The name of a control on the panel of a known controller (else NOTE / CC / PC / SYSEX and the number). */
static std::string srcName(const Src& s, int devType) {
	if (devType == D_MIX) {
		if (s.kind == K_NOTE) {
			if (s.num >= 1 && s.num <= 24) {
				int ch = (s.num - 1) / 3 + 1, r = (s.num - 1) % 3;
				return string::f(r == 0 ? "MUTE %d" : r == 1 ? "SOLO+M %d" : "REC %d", ch);
			}
			if (s.num == 25) return "BANK <";
			if (s.num == 26) return "BANK >";
			if (s.num == 27) return "SOLO";
		}
		if (s.kind == K_CC) {
			for (int c = 0; c < 8; c++)
				if (s.num >= MIX_KNOB[c] && s.num <= MIX_KNOB[c] + 3)
					return s.num - MIX_KNOB[c] == 3 ? string::f("FADER %d", c + 1) : string::f("KNOB %d%c", c + 1, 'A' + s.num - MIX_KNOB[c]);
			if (s.num == 62) return "MASTER";
		}
	}
	if (devType == D_MPD) {
		if (s.kind == K_NOTE && s.num >= 36 && s.num < 36 + 48) {
			int p = s.num - 36;
			return p < 16 ? string::f("PAD %d", p + 1) : string::f("PAD %c%d", 'A' + p / 16, p % 16 + 1);
		}
		if (s.kind == K_CC)
			for (int i = 0; i < 6; i++)
				if (s.num == MPD_KNOB[i])
					return string::f("KNOB %d", i + 1);
	}
	std::string in = string::f("IN%d ", s.dev + 1);
	switch (s.kind) {
		case K_NOTE: return in + string::f("NOTE %d", (int) s.num);
		case K_CC: return in + string::f("CC %d", (int) s.num);
		case K_PC: return in + string::f("PC %d", (int) s.num);
		case K_SYSEX: return in + "SYSEX";
		default: return "";
	}
}

static std::string upperNoSpace(const std::string& s) {
	std::string o;
	for (char c : s)
		if (!std::isspace((unsigned char) c) && c != '-' && c != '_')
			o += (char) std::toupper((unsigned char) c);
	return o;
}

static int devTypeOfName(const std::string& name) {
	std::string n = upperNoSpace(name);
	if (n.find("MIDIMIX") != std::string::npos)
		return D_MIX;
	if (n.find("MPD218") != std::string::npos)
		return D_MPD;
	return D_OTHER;
}

/** A short name for a parameter: its name up to " (", upper case. */
static std::string shortParamName(engine::ParamQuantity* pq) {
	if (!pq)
		return "?";
	std::string n = pq->name;
	size_t b = n.find(" (");
	if (b != std::string::npos && b > 0)
		n = n.substr(0, b);
	n = string::uppercase(n);
	if (n.size() > 16)
		n = n.substr(0, 16);
	return n;
}

} // namespace mapx
using namespace mapx;

struct MapX : Module {
	enum ParamId { LEARN_PARAM, CLEAR_PARAM, UNDO_PARAM, PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LEARN_LIGHT, LIGHTS_LEN };

	midi::InputQueue midiIn[NDEV];
	ParamHandle handles[MAXH];

	// the table of the audio thread (two buffers; the UI thread fills the other one and swaps)
	Active act[2][MAXA];
	int actN[2] = {0, 0};
	std::atomic<int> cur{0};
	int seenCur = -1;
	bool down[MAXA] = {};
	int onceLeft[MAXH] = {};

	// the last message (for the display, LEARN and the selection)
	std::atomic<int> lastDev{-1}, lastKind{0}, lastNum{0}, lastCh{0}, lastVal{0};
	std::atomic<uint32_t> lastSeq{0};

	// the PRESET knobs (GENRA TECHNO map): the audio thread notes the knob's value, the UI thread loads the preset of the
	// instrument that follows the track. presetKey: the control (packed Src, -1: none)
	std::atomic<int64_t> presetKey[5];
	std::atomic<float> presetVal[5];
	std::atomic<uint32_t> presetSeq[5];
	uint32_t presetSeen[5] = {};

	// saved
	std::vector<UserMap> users;
	int preset = P_BOTH;
	int engineSel = E_AUTO; // which ready-made map (right-click)
	bool autoPicked = false;

	// UI side
	std::vector<std::vector<UserMap>> undo;
	bool learnOn = false;
	bool dirty = true;

	MapX() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(LEARN_PARAM, "LEARN: move a control, then click a parameter (or the other way round)");
		configButton(CLEAR_PARAM, "CLEAR: the selected mapping (a control of the ready-made map: off / back on)");
		configButton(UNDO_PARAM, "UNDO: the last change of the own mappings");
		for (int i = 0; i < MAXH; i++) {
			handles[i].color = ACC;
			handles[i].text = "MAP-X";
			APP->engine->addParamHandle(&handles[i]);
		}
		for (int d = 0; d < NDEV; d++)
			midiIn[d].setChannel(-1);
		for (int k = 0; k < 5; k++) {
			presetKey[k].store(-1);
			presetVal[k].store(0.f);
			presetSeq[k].store(0);
		}
	}

	static int64_t packSrc(const Src& s) { return ((int64_t) s.dev << 40) | ((int64_t) s.kind << 32) | (uint32_t) s.num; }

	~MapX() {
		for (int i = 0; i < MAXH; i++)
			APP->engine->removeParamHandle(&handles[i]);
	}

	void onReset() override {
		users.clear();
		undo.clear();
		preset = P_BOTH;
		learnOn = false;
		dirty = true;
	}

	// ---------------------------------------------------------------- audio thread
	engine::ParamQuantity* pqOf(int slot) {
		if (slot < 0 || slot >= MAXH)
			return nullptr;
		ParamHandle& h = handles[slot];
		Module* m = h.module;
		if (!m || h.paramId < 0 || h.paramId >= (int) m->paramQuantities.size())
			return nullptr;
		return m->paramQuantities[h.paramId];
	}

	void apply(int i, const Active& a, bool isCC, float v01, bool press, bool release) {
		engine::ParamQuantity* pq = pqOf(a.slot);
		if (!pq)
			return;
		float lo = pq->getMinValue(), hi = pq->getMaxValue();
		int mode = a.mode;
		if (mode == M_DIRECT && !isCC)
			mode = M_HOLD; // a button set to direct: on while held
		if (mode == M_DIRECT) {
			float x = lo + v01 * (hi - lo);
			if (pq->snapEnabled)
				x = std::round(x);
			pq->setValue(clamp(x, lo, hi));
			return;
		}
		// button ways: a CC is a button pressed above the middle
		bool p = press, r = release;
		if (isCC) {
			bool now = v01 >= 0.5f;
			p = now && !down[i];
			r = !now && down[i];
			down[i] = now;
		}
		switch (mode) {
			case M_ONCE:
				if (p) {
					pq->setValue(hi);
					onceLeft[a.slot] = std::max(1, (int) (APP->engine->getSampleRate() * 0.012f));
				}
				break;
			case M_TOGGLE:
				if (p)
					pq->setValue(pq->getValue() > (lo + hi) / 2 ? lo : hi);
				break;
			case M_HOLD:
				if (p)
					pq->setValue(hi);
				if (r)
					pq->setValue(lo);
				break;
			case M_STEP:
				if (p) {
					if (pq->snapEnabled) {
						int n = (int) std::round(hi - lo) + 1;
						int v = (int) std::round(pq->getValue() - lo);
						v = ((v + a.dir) % n + n) % n;
						pq->setValue(lo + v);
					} else {
						float x = pq->getValue() + a.dir * (hi - lo) * 0.1f;
						if (x > hi + 1e-4f) x = lo;
						if (x < lo - 1e-4f) x = hi;
						pq->setValue(clamp(x, lo, hi));
					}
				}
				break;
			default: break;
		}
	}

	void onMessage(int d, const midi::Message& msg, const Active* A, int n) {
		if (msg.bytes.empty())
			return;
		uint8_t st = msg.bytes[0];
		Src s;
		s.dev = d;
		float v01 = 0.f;
		bool press = false, release = false;
		int ch = st & 0x0f;
		switch (st >> 4) {
			case 0x9:
			case 0x8: {
				if (msg.bytes.size() < 3)
					return;
				s.kind = K_NOTE;
				s.num = msg.bytes[1];
				bool on = (st >> 4) == 0x9 && msg.bytes[2] > 0;
				press = on;
				release = !on;
				v01 = on ? 1.f : 0.f;
			} break;
			case 0xb:
				if (msg.bytes.size() < 3)
					return;
				s.kind = K_CC;
				s.num = msg.bytes[1];
				v01 = msg.bytes[2] / 127.f;
				break;
			case 0xc:
				if (msg.bytes.size() < 2)
					return;
				s.kind = K_PC;
				s.num = msg.bytes[1];
				press = true;
				v01 = 1.f;
				break;
			case 0xf:
				if (st != 0xf0)
					return; // clock, start, stop …: not a control
				{
					uint32_t h = 2166136261u; // FNV-1a over the bytes
					for (uint8_t b : msg.bytes)
						h = (h ^ b) * 16777619u;
					s.kind = K_SYSEX;
					s.num = (int32_t) (h & 0x7fffffff);
					press = true;
					v01 = 1.f;
					ch = -1;
				}
				break;
			default: return;
		}
		// note-offs of an unmapped note are not news for the display (a pad sends both)
		if (!(s.kind == K_NOTE && release)) {
			lastDev.store(d);
			lastKind.store(s.kind);
			lastNum.store(s.num);
			lastCh.store(ch);
			lastVal.store((int) std::round(v01 * 127.f));
			lastSeq.fetch_add(1);
		}
		for (int i = 0; i < n; i++)
			if (A[i].src.same(s))
				apply(i, A[i], s.kind == K_CC, v01, press, release);
		if (s.kind == K_CC) {
			int64_t key = packSrc(s);
			for (int k = 0; k < 5; k++)
				if (presetKey[k].load(std::memory_order_relaxed) == key) {
					presetVal[k].store(v01);
					presetSeq[k].fetch_add(1);
				}
		}
	}

	void process(const ProcessArgs& args) override {
		int c = cur.load();
		if (c != seenCur) {
			seenCur = c;
			std::fill(std::begin(down), std::end(down), false);
		}
		const Active* A = act[c];
		int n = actN[c];
		for (int d = 0; d < NDEV; d++) {
			midi::Message msg;
			while (midiIn[d].tryPop(&msg, args.frame))
				onMessage(d, msg, A, n);
		}
		for (int i = 0; i < MAXH; i++)
			if (onceLeft[i] > 0 && --onceLeft[i] == 0)
				if (engine::ParamQuantity* pq = pqOf(i))
					pq->setValue(pq->getMinValue());
		lights[LEARN_LIGHT].setBrightness(learnOn ? 1.f : 0.f);
	}

	// ---------------------------------------------------------------- UI thread
	struct Row {
		Src src;
		std::string from, to, mode;
		bool user = false, off = false, free = false, taken = false;
		int userIdx = -1;     // the own mapping (users[]), -1: the ready-made map
		int presetIdx = -1;   // the entry of the ready-made map, -1: none
	};
	std::vector<Row> rows;
	int devType[NDEV] = {D_OTHER, D_OTHER};
	int64_t genraId = -1;
	bool genraFound = false;
	int64_t loopId = -1; // the looper SCHLEIFE (MPD218 bank B)
	bool techno = false; // genraId is a GENRA TECHNO: its map (TECHNO()), else GENRA's (PRESET())
	/** The module an entry of the ready-made map goes to (-1: not in the rack; T_FOLLOW: no parameter handle). */
	int64_t targetId(int target) const { return target == T_LOOP ? loopId : target == T_FOLLOW ? -1 : genraId; }
	const std::vector<PEntry>& table() const { return techno ? TECHNO() : PRESET(); }

	bool presetHas(int type) const {
		return (type == D_MIX && (preset == P_BOTH || preset == P_MIX)) || (type == D_MPD && (preset == P_BOTH || preset == P_MPD));
	}
	/** The input the ready-made map of this controller listens to (-1: none). */
	int devOfType(int type) {
		for (int d = 0; d < NDEV; d++)
			if (devType[d] == type)
				return d;
		// unknown names: IN 1 is the MIDImix, IN 2 the MPD218
		int d = type == D_MIX ? 0 : 1;
		return (devType[d] == D_OTHER && midiIn[d].getDeviceId() >= 0) ? d : -1;
	}

	std::string deviceName(int d) {
		int id = midiIn[d].getDeviceId();
		if (id < 0)
			return "";
		return midiIn[d].getDeviceName(id);
	}

	int findUser(const Src& s) const {
		for (int i = 0; i < (int) users.size(); i++)
			if (users[i].src.same(s))
				return i;
		return -1;
	}

	int64_t findGenra() {
		if (genraId >= 0) {
			Module* m = APP->engine->getModule(genraId);
			if (m && m->model && m->model->plugin && m->model->plugin->slug == "Genra" && m->model->slug == "Genra")
				return genraId;
		}
		for (int64_t id : APP->engine->getModuleIds()) {
			Module* m = APP->engine->getModule(id);
			if (m && m->model && m->model->plugin && m->model->plugin->slug == "Genra" && m->model->slug == "Genra")
				return id;
		}
		return -1;
	}

	static bool isTechno(Module* m) {
		return m && m->model && m->model->plugin && m->model->plugin->slug == "GenraTechno" && m->model->slug == "GenraTechno";
	}
	int64_t findTechno() {
		if (genraId >= 0 && isTechno(APP->engine->getModule(genraId)))
			return genraId;
		for (int64_t id : APP->engine->getModuleIds())
			if (isTechno(APP->engine->getModule(id)))
				return id;
		return -1;
	}

	/** The instrument that follows GENRA TECHNO's track k (Follow GENRA, without a cable), nullptr: none. */
	follow::Host* follower(int k) {
		Module* g = genraId >= 0 ? APP->engine->getModule(genraId) : nullptr;
		if (!g || k < 0 || k >= 5)
			return nullptr;
		for (int64_t id : APP->engine->getModuleIds()) {
			Module* m = APP->engine->getModule(id);
			follow::Host* h = m ? follow::host(m) : nullptr;
			if (h && h->follows && h->fol.genra.to.load() == g && h->fol.track.load() == X_TO_FOLLOW[k])
				return h;
		}
		return nullptr;
	}

	/** UI thread: a PRESET knob moved: its share of the follower's preset list. */
	void presetStep() {
		for (int k = 0; k < 5; k++) {
			uint32_t q = presetSeq[k].load();
			if (q == presetSeen[k])
				continue;
			presetSeen[k] = q;
			follow::Host* h = follower(k);
			int n = h ? h->hostPresetCount() : 0;
			if (n <= 0)
				continue;
			int i = clamp((int) std::round(presetVal[k].load() * (n - 1)), 0, n - 1);
			if (i != h->hostPresetCurrent())
				h->hostPresetLoad(i);
		}
	}

	int64_t findLoop() {
		if (loopId >= 0) {
			Module* m = APP->engine->getModule(loopId);
			if (m && m->model && m->model->slug == "GenraSchleife")
				return loopId;
		}
		for (int64_t id : APP->engine->getModuleIds()) {
			Module* m = APP->engine->getModule(id);
			if (m && m->model && m->model->slug == "GenraSchleife") // (GENRA Test now, maybe another GENRA plugin later)
				return id;
		}
		return -1;
	}

	/** Binds a handle to (module, param): one already on it, else a free one. -1: none free / taken by another. */
	int bind(int64_t moduleId, int paramId, bool overwrite, std::vector<bool>& used) {
		for (int i = 0; i < MAXH; i++)
			if (handles[i].moduleId == moduleId && handles[i].paramId == paramId) {
				used[i] = true;
				return i;
			}
		for (int i = 0; i < MAXH; i++)
			if (!used[i] && handles[i].moduleId < 0) {
				APP->engine->updateParamHandle(&handles[i], moduleId, paramId, overwrite);
				if (handles[i].moduleId < 0)
					return -1; // another mapper holds it
				used[i] = true;
				return i;
			}
		return -1;
	}

	/** Builds the rows (display) and the table of the audio thread, binds the handles. */
	void rebuild() {
		dirty = false;
		for (int d = 0; d < NDEV; d++)
			devType[d] = devTypeOfName(deviceName(d));
		// the main module: GENRA or GENRA TECHNO (automatic: GENRA if there is one, else GENRA TECHNO)
		int64_t gid = engineSel == E_TECHNO ? -1 : findGenra();
		techno = false;
		if (gid < 0 && engineSel != E_GENRA) {
			gid = findTechno();
			techno = gid >= 0 || engineSel == E_TECHNO;
		}
		genraId = gid;
		genraFound = genraId >= 0;
		loopId = findLoop();
		int64_t pk[5] = {-1, -1, -1, -1, -1};
		int mixDev = devOfType(D_MIX), mpdDev = devOfType(D_MPD);

		struct Want {
			Src src;
			int64_t moduleId;
			int paramId;
			int8_t mode, dir;
			bool user;
			int row;
		};
		std::vector<Want> want;
		std::vector<Row> nr;
		std::vector<bool> userShown(users.size(), false);

		const std::vector<PEntry>& T = table();
		for (int pi = 0; pi < (int) T.size(); pi++) {
			const PEntry& e = T[pi];
			if (!presetHas(e.dev))
				continue;
			int d = e.dev == D_MIX ? mixDev : mpdDev;
			if (d < 0)
				continue;
			Row r;
			r.src.dev = d;
			r.src.kind = e.kind;
			r.src.num = e.num;
			r.presetIdx = pi;
			r.from = srcName(r.src, e.dev);
			int u = findUser(r.src);
			if (u >= 0) {
				userShown[u] = true;
				r.userIdx = u;
				r.user = true;
			} else if (e.param == G_FREE) {
				r.free = true;
				r.to = "FREE";
			} else {
				r.to = e.name ? e.name : "?";
				r.mode = MODE_NAME[e.mode];
				int64_t tid = targetId(e.target);
				if (e.target == T_FOLLOW) { // no handle: the knob loads the follower's presets (presetStep)
					pk[clamp((int) e.param, 0, 4)] = packSrc(r.src);
					if (!follower(e.param))
						r.to += " ·";
				}
				else if (tid >= 0)
					want.push_back({r.src, tid, (int) e.param, e.mode, e.dir, false, (int) nr.size()});
				else
					r.to += " ·";
			}
			nr.push_back(r);
		}
		for (int u = 0; u < (int) users.size(); u++)
			if (!userShown[u]) {
				Row r;
				r.src = users[u].src;
				int dt = r.src.dev >= 0 && r.src.dev < NDEV ? devType[r.src.dev] : D_OTHER;
				r.from = srcName(r.src, dt);
				r.userIdx = u;
				r.user = true;
				nr.push_back(r);
			}
		for (int i = 0; i < (int) nr.size(); i++) {
			Row& r = nr[i];
			if (r.userIdx < 0)
				continue;
			const UserMap& um = users[r.userIdx];
			if (um.off) {
				r.off = true;
				r.to = "OFF";
				r.mode = "";
				continue;
			}
			Module* m = APP->engine->getModule(um.moduleId);
			engine::ParamQuantity* pq = (m && um.paramId >= 0 && um.paramId < (int) m->paramQuantities.size())
			                                ? m->paramQuantities[um.paramId] : nullptr;
			r.to = m ? shortParamName(pq) : "(MODULE GONE)";
			r.mode = MODE_NAME[um.mode];
			if (m)
				want.push_back({r.src, um.moduleId, um.paramId, um.mode, um.dir, true, i});
		}

		// handles: keep what is wanted, free the rest, then bind (own mappings first: they may take a parameter over)
		std::vector<bool> used(MAXH, false);
		for (int i = 0; i < MAXH; i++) {
			bool keep = false;
			for (const Want& w : want)
				if (handles[i].moduleId == w.moduleId && handles[i].paramId == w.paramId)
					keep = true;
			if (!keep && handles[i].moduleId >= 0)
				APP->engine->updateParamHandle(&handles[i], -1, 0, true);
		}
		int nxt = 1 - cur.load();
		int n = 0;
		for (int pass = 0; pass < 2; pass++)
			for (const Want& w : want) {
				if (w.user != (pass == 0) || n >= MAXA)
					continue;
				int slot = bind(w.moduleId, w.paramId, w.user, used);
				if (slot < 0) {
					nr[w.row].taken = true;
					continue;
				}
				Active& a = act[nxt][n++];
				a.src = w.src;
				a.slot = slot;
				a.mode = w.mode;
				a.dir = w.dir;
			}
		actN[nxt] = n;
		cur.store(nxt);
		for (int k = 0; k < 5; k++)
			presetKey[k].store(pk[k]);
		rows = nr;
	}

	void pushUndo() {
		undo.push_back(users);
		if (undo.size() > 32)
			undo.erase(undo.begin());
	}

	/** A guess how a button should act on this parameter. */
	static int8_t modeFor(engine::ParamQuantity* pq, int kind) {
		if (kind == K_CC)
			return M_DIRECT;
		if (!pq)
			return M_ONCE;
		if (auto* sq = dynamic_cast<engine::SwitchQuantity*>(pq))
			return sq->labels.size() > 2 ? M_STEP : M_TOGGLE;
		if (pq->snapEnabled && pq->getMaxValue() - pq->getMinValue() <= 1.01f)
			return M_ONCE;
		if (pq->snapEnabled)
			return M_STEP;
		return M_HOLD;
	}

	void learn(const Src& s, int64_t moduleId, int paramId) {
		pushUndo();
		int u = findUser(s);
		if (u >= 0)
			users.erase(users.begin() + u);
		UserMap um;
		um.src = s;
		um.moduleId = moduleId;
		um.paramId = paramId;
		Module* m = APP->engine->getModule(moduleId);
		engine::ParamQuantity* pq = (m && paramId < (int) m->paramQuantities.size()) ? m->paramQuantities[paramId] : nullptr;
		um.mode = modeFor(pq, s.kind);
		users.push_back(um);
		dirty = true;
	}

	/** CLEAR on a row: an own mapping goes (the ready-made one comes back); a ready-made one is switched off. */
	void clearRow(const Row& r) {
		pushUndo();
		if (r.userIdx >= 0)
			users.erase(users.begin() + r.userIdx);
		else if (r.presetIdx >= 0 && !r.free) {
			UserMap um;
			um.src = r.src;
			um.off = true;
			users.push_back(um);
		}
		dirty = true;
	}

	/** A lamp on a row: the way it acts (an own mapping; a ready-made one becomes an own one). The lit one: direct. */
	void setMode(const Row& r, int mode) {
		if (r.free || r.off)
			return;
		pushUndo();
		int8_t was = M_DIRECT;
		if (r.userIdx >= 0)
			was = users[r.userIdx].mode;
		else if (r.presetIdx >= 0)
			was = table()[r.presetIdx].mode;
		int8_t nm = (was == mode) ? M_DIRECT : (int8_t) mode;
		if (r.userIdx >= 0)
			users[r.userIdx].mode = nm;
		else if (r.presetIdx >= 0 && targetId(table()[r.presetIdx].target) >= 0) {
			const PEntry& e = table()[r.presetIdx];
			UserMap um;
			um.src = r.src;
			um.moduleId = targetId(e.target);
			um.paramId = e.param;
			um.mode = nm;
			um.dir = e.dir;
			users.push_back(um);
		}
		dirty = true;
	}

	int modeOfRow(const Row& r) const {
		if (r.userIdx >= 0 && !users[r.userIdx].off)
			return users[r.userIdx].mode;
		if (r.userIdx < 0 && r.presetIdx >= 0 && !r.free)
			return table()[r.presetIdx].mode;
		return -1;
	}

	/** On a new module: IN 1 the MIDImix, IN 2 the MPD218, if they are connected. */
	void autoPick() {
		autoPicked = true;
		for (int driverId : midi::getDriverIds()) {
			midi::Driver* drv = midi::getDriver(driverId);
			if (!drv)
				continue;
			for (int id : drv->getInputDeviceIds()) {
				int t = devTypeOfName(drv->getInputDeviceName(id));
				int d = t == D_MIX ? 0 : t == D_MPD ? 1 : -1;
				if (d >= 0 && midiIn[d].getDeviceId() < 0) {
					midiIn[d].setDriverId(driverId);
					midiIn[d].setDeviceId(id);
					midiIn[d].setChannel(-1);
				}
			}
		}
		dirty = true;
	}

	// ---------------------------------------------------------------- saving
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "preset", json_integer(preset));
		json_object_set_new(root, "engine", json_integer(engineSel));
		json_object_set_new(root, "autoPicked", json_boolean(true));
		for (int d = 0; d < NDEV; d++)
			json_object_set_new(root, string::f("midi%d", d).c_str(), midiIn[d].toJson());
		json_t* arr = json_array();
		for (const UserMap& u : users) {
			json_t* o = json_object();
			json_object_set_new(o, "dev", json_integer(u.src.dev));
			json_object_set_new(o, "kind", json_integer(u.src.kind));
			json_object_set_new(o, "num", json_integer(u.src.num));
			json_object_set_new(o, "moduleId", json_integer(u.moduleId));
			json_object_set_new(o, "paramId", json_integer(u.paramId));
			json_object_set_new(o, "mode", json_integer(u.mode));
			json_object_set_new(o, "dir", json_integer(u.dir));
			json_object_set_new(o, "off", json_boolean(u.off));
			json_array_append_new(arr, o);
		}
		json_object_set_new(root, "maps", arr);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "preset"))
			preset = clamp((int) json_integer_value(j), 0, NUM_PRESETS - 1);
		if (json_t* j = json_object_get(root, "engine"))
			engineSel = clamp((int) json_integer_value(j), 0, NUM_ENGINES - 1);
		autoPicked = true;
		for (int d = 0; d < NDEV; d++)
			if (json_t* j = json_object_get(root, string::f("midi%d", d).c_str()))
				midiIn[d].fromJson(j);
		users.clear();
		if (json_t* arr = json_object_get(root, "maps")) {
			size_t i;
			json_t* o;
			json_array_foreach(arr, i, o) {
				UserMap u;
				u.src.dev = (int8_t) json_integer_value(json_object_get(o, "dev"));
				u.src.kind = (int8_t) json_integer_value(json_object_get(o, "kind"));
				u.src.num = (int32_t) json_integer_value(json_object_get(o, "num"));
				u.moduleId = json_integer_value(json_object_get(o, "moduleId"));
				u.paramId = (int) json_integer_value(json_object_get(o, "paramId"));
				u.mode = (int8_t) json_integer_value(json_object_get(o, "mode"));
				json_t* dj = json_object_get(o, "dir");
				u.dir = dj ? (int8_t) json_integer_value(dj) : 1;
				u.off = json_is_true(json_object_get(o, "off"));
				if (u.src.valid())
					users.push_back(u);
			}
		}
		undo.clear();
		dirty = true;
	}
};

// ---------------------------------------------------------------- widgets
namespace mapx {

/** A field with a menu (and the mouse wheel): the name left, a short label right. */
struct Field : OpaqueWidget {
	std::string label;
	std::function<std::vector<std::string>()> names;
	std::function<int()> current;
	std::function<void(int)> choose;
	std::function<std::string()> text; // what it shows (default: the current name)
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			Menu* menu = createMenu();
			menu->addChild(createMenuLabel(label));
			std::vector<std::string> ns = names();
			auto cur = current;
			auto ch = choose;
			for (int k = 0; k < (int) ns.size(); k++)
				menu->addChild(createCheckMenuItem(ns[k], "", [=]() { return cur() == k; }, [=]() { ch(k); }));
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (e.scrollDelta.y != 0.f) {
			int n = std::max(1, (int) names().size());
			choose(((current() + (e.scrollDelta.y < 0.f ? 1 : -1)) % n + n) % n);
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverScroll(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			auto f = genra_ui::boldFont();
			float y = box.size.y / 2 + 2.7f;
			std::string s = text ? text() : "";
			if (!text) {
				std::vector<std::string> ns = names();
				int i = current();
				s = (i >= 0 && i < (int) ns.size()) ? ns[i] : "";
			}
			genra_ui::text(args.vg, f, box.size.x - mm2px(6.f), y - 0.3f, label, 5.6f, genra_ui::DIM, NVG_ALIGN_RIGHT, 0.7f);
			float lw = 0.f;
			if (f && f->handle >= 0) {
				float b[4];
				nvgFontFaceId(args.vg, f->handle);
				nvgFontSize(args.vg, 5.6f);
				nvgTextLetterSpacing(args.vg, 0.7f);
				lw = nvgTextBounds(args.vg, 0, 0, label.c_str(), nullptr, b);
			}
			float room = box.size.x - mm2px(2.f) - mm2px(6.f) - lw - 6.f;
			if (f && f->handle >= 0)
				while (s.size() > 1) {
					float bounds[4];
					nvgFontFaceId(args.vg, f->handle);
					nvgFontSize(args.vg, 7.6f);
					nvgTextLetterSpacing(args.vg, 0.9f);
					if (nvgTextBounds(args.vg, 0, 0, s.c_str(), nullptr, bounds) <= room)
						break;
					s.pop_back();
				}
			genra_ui::text(args.vg, f, mm2px(2.f), y, s, 7.6f, genra_ui::TXT, NVG_ALIGN_LEFT, 0.9f);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

/** The four lamps: how the selected mapping acts. */
struct ModeLamps : OpaqueWidget {
	MapX* m = nullptr;
	std::function<int()> selMode;
	std::function<void(int)> pick;
	float dx = 31.f;
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			pick(clamp((int) (e.pos.x / dx), 0, 3));
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {
		for (int i = 0; i < 4; i++) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, dx * (i + 0.5f), box.size.y / 2, 3.6f);
			nvgFillColor(args.vg, genra_ui::LAMP);
			nvgFill(args.vg);
		}
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && selMode) {
			int v = selMode();
			if (v >= 0 && v < 4) {
				float cx = dx * (v + 0.5f), cy = box.size.y / 2;
				NVGcolor glow = ACC, none = ACC;
				glow.a = 0.35f;
				none.a = 0.f;
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, cx, cy, 8.f);
				nvgFillPaint(args.vg, nvgRadialGradient(args.vg, cx, cy, 2.f, 8.f, glow, none));
				nvgFill(args.vg);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, cx, cy, 3.6f);
				nvgFillColor(args.vg, ACC);
				nvgFill(args.vg);
			}
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

} // namespace mapx

struct MapXWidget;

/** The display: what comes in, the list of mappings (wheel: scroll, click: select). */
struct MapXDisplay : OpaqueWidget {
	MapX* m = nullptr;
	int* sel = nullptr;
	int scroll = 0;
	static constexpr float HEAD = 17.f, ROW = 11.f;
	int visible() const { return std::max(1, (int) ((box.size.y - HEAD - 4.f) / ROW)); }
	void ensureVisible(int i) {
		int v = visible();
		if (i < scroll)
			scroll = i;
		if (i >= scroll + v)
			scroll = i - v + 1;
		scroll = std::max(0, scroll);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (m && e.scrollDelta.y != 0.f) {
			int maxS = std::max(0, (int) m->rows.size() - visible());
			scroll = clamp(scroll + (e.scrollDelta.y < 0.f ? 2 : -2), 0, maxS);
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverScroll(e);
	}
	void onButton(const ButtonEvent& e) override {
		if (m && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			int i = scroll + (int) ((e.pos.y - HEAD) / ROW);
			if (e.pos.y >= HEAD && i >= 0 && i < (int) m->rows.size())
				*sel = i;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	std::string headLine();
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			OpaqueWidget::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float W = box.size.x;
		auto f = genra_ui::boldFont();
		nvgSave(vg);
		nvgScissor(vg, 0, 0, W, box.size.y);
		if (!m) {
			genra_ui::text(vg, f, 6, 12, "IN  MPD · NOTE 36 · CH10", 7.f, ACC, NVG_ALIGN_LEFT, 0.6f);
			genra_ui::text(vg, f, 6, HEAD + 9, "MIDI MAPPER FOR GENRA", 6.4f, genra_ui::DIM, NVG_ALIGN_LEFT, 0.5f);
		} else {
			genra_ui::text(vg, f, 6, 12, headLine(), 7.f, ACC, NVG_ALIGN_LEFT, 0.6f);
			int n = (int) m->rows.size(), v = visible();
			scroll = clamp(scroll, 0, std::max(0, n - v));
			if (n == 0)
				genra_ui::text(vg, f, 6, HEAD + 9, m->preset == P_OFF ? "NO MAPPINGS · LEARN" : "NO CONTROLLER · PICK IN 1 / IN 2",
				               6.2f, genra_ui::DIM, NVG_ALIGN_LEFT, 0.4f);
			for (int k = 0; k < v && scroll + k < n; k++) {
				const MapX::Row& r = m->rows[scroll + k];
				float y = HEAD + 8.f + k * ROW;
				bool s = (scroll + k) == *sel;
				if (s) {
					nvgBeginPath(vg);
					nvgRoundedRect(vg, 2, y - 8.f, W - 4, ROW - 0.5f, 2);
					NVGcolor bg = ACC;
					bg.a = 0.16f;
					nvgFillColor(vg, bg);
					nvgFill(vg);
				}
				NVGcolor toC = r.free || r.off ? genra_ui::DIM : (r.taken ? nvgRGB(0xff, 0x6a, 0x6a) : (s ? ACC : genra_ui::TXT));
				genra_ui::text(vg, f, 6, y, r.from, 6.2f, genra_ui::DIM, NVG_ALIGN_LEFT, 0.4f);
				std::string to = r.to;
				if (to.size() > 11)
					to = to.substr(0, 11);
				genra_ui::text(vg, f, W * 0.40f, y, to + (r.user && !r.off ? "*" : ""), 6.2f, toC, NVG_ALIGN_LEFT, 0.4f);
				genra_ui::text(vg, f, W - 5, y, r.taken ? "TAKEN" : r.mode, 5.6f, genra_ui::DIM, NVG_ALIGN_RIGHT, 0.3f);
			}
			// a scroll bar when the list is longer
			if (n > v) {
				float h = box.size.y - HEAD - 4.f;
				float bh = std::max(8.f, h * v / n), by = HEAD + (h - bh) * scroll / std::max(1, n - v);
				nvgBeginPath(vg);
				nvgRoundedRect(vg, W - 2.5f, by, 1.5f, bh, 0.75f);
				nvgFillColor(vg, genra_ui::EDGE);
				nvgFill(vg);
			}
		}
		nvgResetScissor(vg);
		nvgRestore(vg);
		OpaqueWidget::drawLayer(args, layer);
	}
};

std::string MapXDisplay::headLine() {
	if (m->learnOn)
		return "LEARN · CONTROL + PARAMETER";
	int d = m->lastDev.load();
	if (d < 0)
		return m->genraFound || m->preset == P_OFF ? "WAITING FOR MIDI" : "NO GENRA / TECHNO IN THE RACK";
	static const char* DN[3] = {"MIX", "MPD", ""};
	std::string dn = d < NDEV && m->devType[d] != D_OTHER ? DN[m->devType[d]] : string::f("IN%d", d + 1);
	int k = m->lastKind.load(), num = m->lastNum.load(), ch = m->lastCh.load();
	switch (k) {
		case K_NOTE: return string::f("IN  %s · NOTE %d · CH%d", dn.c_str(), num, ch + 1);
		case K_CC: return string::f("IN  %s · CC %d = %d · CH%d", dn.c_str(), num, m->lastVal.load(), ch + 1);
		case K_PC: return string::f("IN  %s · PC %d · CH%d", dn.c_str(), num, ch + 1);
		case K_SYSEX: return string::f("IN  %s · SYSEX", dn.c_str());
		default: return "";
	}
}

struct MapXWidget : ModuleWidget {
	MapXDisplay* disp = nullptr;
	int sel = -1;
	uint32_t seenSeq = 0;
	Src learnSrc;
	int64_t learnModule = -1;
	int learnParam = -1;
	bool prevBtn[3] = {};
	int frames = 0;

	MapXWidget(MapX* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MapX.svg")));
		jiant::addScrews(this);

		disp = new MapXDisplay;
		disp->m = module;
		disp->sel = &sel;
		disp->box = jiant::area(L::DISPLAY).grow(Vec(-1, -1));
		addChild(disp);

		// MAP: the ready-made map
		Field* pf = createWidget<Field>(Vec(L::PRESETF[0], L::PRESETF[1]));
		pf->box.size = Vec(L::PRESETF[2], L::PRESETF[3]);
		pf->label = "MAP";
		pf->names = []() { return std::vector<std::string>(PRESET_NAME, PRESET_NAME + NUM_PRESETS); };
		pf->current = [=]() { return module ? module->preset : (int) P_BOTH; };
		pf->choose = [=](int i) {
			if (module) {
				module->preset = clamp(i, 0, NUM_PRESETS - 1);
				module->dirty = true;
			}
		};
		addChild(pf);

		// IN 1, IN 2: the controllers (all MIDI inputs of all drivers)
		const float* df[NDEV] = {L::DEV0F, L::DEV1F};
		for (int d = 0; d < NDEV; d++) {
			Field* f = createWidget<Field>(Vec(df[d][0], df[d][1]));
			f->box.size = Vec(df[d][2], df[d][3]);
			f->label = string::f("IN %d", d + 1);
			struct Dev {
				int driver, id;
			};
			auto list = []() {
				std::vector<std::pair<Dev, std::string>> l;
				l.push_back({{-1, -1}, "NONE"});
				for (int driverId : midi::getDriverIds()) {
					midi::Driver* drv = midi::getDriver(driverId);
					if (!drv)
						continue;
					for (int id : drv->getInputDeviceIds())
						l.push_back({{driverId, id}, drv->getInputDeviceName(id)});
				}
				return l;
			};
			f->names = [=]() {
				std::vector<std::string> ns;
				for (auto& e : list())
					ns.push_back(e.second);
				return ns;
			};
			f->current = [=]() {
				if (!module)
					return 0;
				auto l = list();
				for (int k = 1; k < (int) l.size(); k++)
					if (l[k].first.driver == module->midiIn[d].getDriverId() && l[k].first.id == module->midiIn[d].getDeviceId())
						return k;
				return 0;
			};
			f->choose = [=](int k) {
				if (!module)
					return;
				auto l = list();
				if (k <= 0 || k >= (int) l.size())
					module->midiIn[d].setDeviceId(-1);
				else {
					module->midiIn[d].setDriverId(l[k].first.driver);
					module->midiIn[d].setDeviceId(l[k].first.id);
					module->midiIn[d].setChannel(-1);
				}
				module->dirty = true;
			};
			f->text = [=]() -> std::string {
				if (!module)
					return d == 0 ? "MIDI MIX" : "MPD 218";
				std::string n = module->deviceName(d);
				return n.empty() ? "NONE" : string::uppercase(n);
			};
			addChild(f);
		}

		addParam(createLightParamCentered<VCVLightBezel<jiant::AccentLight<0x7b, 0x6c, 0xff>>>(jiant::at(L::LEARN), module,
		                                                                                        MapX::LEARN_PARAM, MapX::LEARN_LIGHT));
		addParam(createParamCentered<VCVBezel>(jiant::at(L::CLEAR), module, MapX::CLEAR_PARAM));
		addParam(createParamCentered<VCVBezel>(jiant::at(L::UNDO), module, MapX::UNDO_PARAM));

		ModeLamps* ml = new ModeLamps;
		ml->m = module;
		ml->dx = L::MODE[2];
		ml->box.pos = Vec(L::MODE[0] - ml->dx / 2, L::MODE[1] - 8);
		ml->box.size = Vec(ml->dx * 4, 16);
		ml->selMode = [=]() -> int {
			if (!module)
				return 0;
			if (sel < 0 || sel >= (int) module->rows.size())
				return -1;
			return module->modeOfRow(module->rows[sel]);
		};
		ml->pick = [=](int i) {
			if (module && sel >= 0 && sel < (int) module->rows.size())
				module->setMode(module->rows[sel], i);
		};
		addChild(ml);
	}

	void selectSrc(const Src& s) {
		MapX* m = dynamic_cast<MapX*>(module);
		for (int i = 0; i < (int) m->rows.size(); i++)
			if (m->rows[i].src.same(s)) {
				sel = i;
				disp->ensureVisible(i);
				return;
			}
	}

	void step() override {
		ModuleWidget::step();
		MapX* m = dynamic_cast<MapX*>(module);
		if (!m)
			return;
		if (!m->autoPicked)
			m->autoPick();
		m->presetStep();

		// the buttons (here, on the UI thread: they change the own mappings)
		bool btn[3];
		for (int i = 0; i < 3; i++)
			btn[i] = m->params[MapX::LEARN_PARAM + i].getValue() > 0.5f;
		if (btn[0] && !prevBtn[0]) {
			m->learnOn = !m->learnOn;
			learnSrc = Src();
			learnModule = -1;
			learnParam = -1;
			if (APP->scene->rack)
				APP->scene->rack->touchedParam = nullptr;
		}
		if (btn[1] && !prevBtn[1] && sel >= 0 && sel < (int) m->rows.size()) {
			Src s = m->rows[sel].src;
			m->clearRow(m->rows[sel]);
			m->rebuild();
			selectSrc(s);
		}
		if (btn[2] && !prevBtn[2] && !m->undo.empty()) {
			m->users = m->undo.back();
			m->undo.pop_back();
			m->dirty = true;
		}
		for (int i = 0; i < 3; i++)
			prevBtn[i] = btn[i];

		// what came in: select its row; in LEARN it is the control
		uint32_t seq = m->lastSeq.load();
		if (seq != seenSeq) {
			seenSeq = seq;
			Src s;
			s.dev = (int8_t) m->lastDev.load();
			s.kind = (int8_t) m->lastKind.load();
			s.num = m->lastNum.load();
			if (s.valid()) {
				if (m->learnOn)
					learnSrc = s;
				selectSrc(s);
			}
		}
		// in LEARN: the parameter last touched (not one of MAP-X's own)
		if (m->learnOn && APP->scene->rack) {
			ParamWidget* tp = APP->scene->rack->touchedParam;
			if (tp && tp->module && tp->module != m) {
				learnModule = tp->module->id;
				learnParam = tp->paramId;
				APP->scene->rack->touchedParam = nullptr;
			}
			if (learnSrc.valid() && learnModule >= 0) {
				m->learn(learnSrc, learnModule, learnParam);
				m->learnOn = false;
				Src s = learnSrc;
				m->rebuild();
				selectSrc(s);
				learnSrc = Src();
				learnModule = -1;
			}
		}

		// now and then: GENRA added or removed, a device changed name, a module of an own mapping gone
		if (++frames >= 30) {
			frames = 0;
			m->dirty = true;
		}
		if (m->dirty) {
			Src s;
			if (sel >= 0 && sel < (int) m->rows.size())
				s = m->rows[sel].src;
			m->rebuild();
			sel = -1;
			if (s.valid())
				selectSrc(s);
		}
	}

	void appendContextMenu(Menu* menu) override {
		MapX* m = dynamic_cast<MapX*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Remove all own mappings", "", [=]() {
			m->pushUndo();
			m->users.clear();
			m->dirty = true;
		}, m->users.empty()));
		menu->addChild(createIndexSubmenuItem(
		    "Ready-made map for", std::vector<std::string>(ENGINE_NAME, ENGINE_NAME + NUM_ENGINES),
		    [=]() -> size_t { return (size_t) m->engineSel; }, [=](size_t i) {
			    m->engineSel = (int) i;
			    m->dirty = true;
		    }));
		menu->addChild(createMenuLabel(m->genraFound ? (m->techno ? "Now: GENRA TECHNO" : "Now: GENRA") : "Now: none in the rack"));
		menu->addChild(createMenuLabel("The ready-made map expects the factory settings"));
		menu->addChild(createMenuLabel("(MIDImix: as delivered, MPD218: preset 1)"));
	}
};

Model* modelMapX = createModel<MapX, MapXWidget>("GenraMapX");
