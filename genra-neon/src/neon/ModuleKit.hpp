// SPDX-License-Identifier: GPL-3.0-only
// GENRA NEON – what the newer neon modules (WOGE, PULS, GLAS, FLUT) share on the Rack side: the UNDO history of
// whole-module snapshots, the knob + CV helper, the CLOCK input menu, and helpers that put the panel's widgets where
// tools/neon_panel.py drew their frames.
#pragma once
#include "NeonUI.hpp"
#include "NeonDsp.hpp"
#include "../../genra/Follow.hpp"
#include "../../genra/ui/PresetBank.hpp"
#include <vector>

namespace neonkit {

using namespace neonui;
static const int PPQN_CHOICES[4] = {1, 2, 4, 24};

/** UNDO for MUTATE / NEW / INIT and the like: snapshots of the whole module (UI thread). */
struct Undo {
	std::vector<json_t*> stack;
	~Undo() { clear(); }
	void clear() {
		for (json_t* j : stack)
			json_decref(j);
		stack.clear();
	}
	void push(Module* m) {
		stack.push_back(m->toJson());
		if (stack.size() > 16) {
			json_decref(stack.front());
			stack.erase(stack.begin());
		}
	}
	bool pop(Module* m) {
		if (stack.empty())
			return false;
		json_t* j = stack.back();
		stack.pop_back();
		m->fromJson(j);
		json_decref(j);
		return true;
	}
};

/** A knob plus its CV (0..10 V over the knob's range, added). */
inline float kv(Module* m, int param, int input, float lo = 0.f, float hi = 1.f) {
	return clamp(m->params[param].getValue() + m->inputs[input].getVoltage() * 0.1f * (hi - lo), lo, hi);
}

/** The time a knob shows (the neon envelope times). */
struct TimeQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float t = neon::envTime(getValue());
		return t < 1.f ? string::f("%.0f", t * 1000.f) : string::f("%.2f", t);
	}
	std::string getUnit() override { return neon::envTime(getValue()) < 1.f ? " ms" : " s"; }
};
struct CutoffQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float f = 20.f * std::pow(2.f, 10.f * getValue());
		return f < 1000.f ? string::f("%.0f", f) : string::f("%.2f k", f / 1000.f);
	}
	std::string getUnit() override { return "Hz"; }
};

/** The CLOCK input's pulses per quarter, as a context menu item. */
inline void clockMenu(Menu* menu, int* ppqn) {
	menu->addChild(createIndexSubmenuItem(
	    "CLOCK input", {"Quarters (1 PPQN)", "8ths (2 PPQN)", "16ths (4 PPQN, GENRA)", "24 PPQN"},
	    [=]() {
		    for (int i = 0; i < 4; i++)
			    if (PPQN_CHOICES[i] == *ppqn)
				    return (size_t) i;
		    return (size_t) 2;
	    },
	    [=](size_t i) { *ppqn = PPQN_CHOICES[i]; }));
}

// ---------------------------------------------------------------- the panel's widgets
inline NKnob* knob(ModuleWidget* mw, Module* m, const float* p, int id) {
	NKnob* k = createParam<NKnob>(Vec(0, 0), m, id);
	k->setup(p);
	mw->addParam(k);
	return k;
}
inline Field* field(ModuleWidget* mw, Module* m, const float* r, int id, int n, const char* const* names,
                    const char* const* info, const char* title) {
	Field* f = createParam<Field>(Vec(0, 0), m, id);
	f->setup(r);
	f->count = [=]() { return n; };
	f->name = [=](int i) { return std::string(names[i]); };
	f->info = [=](int i) { return info ? std::string(info[i]) : std::string(); };
	f->title = title;
	f->undo = std::string("change ") + string::lowercase(title);
	mw->addParam(f);
	return f;
}
inline NButton* button(ModuleWidget* mw, Module* m, const float* r, int id, bool latch, NVGcolor col) {
	NButton* b = createParam<NButton>(Vec(0, 0), m, id);
	b->setup(r, latch);
	b->col = col;
	mw->addParam(b);
	return b;
}
inline NLamps* lamps(ModuleWidget* mw, Module* m, const float* r, int id, int n, NVGcolor col) {
	NLamps* l = createParam<NLamps>(Vec(0, 0), m, id);
	l->place(r, n);
	l->accent = col;
	mw->addParam(l);
	return l;
}
inline genra_presets::PresetField* presetField(ModuleWidget* mw, Module* m, const float* r, genra_presets::Bank* bank,
                                               int selParam, const char* sample) {
	genra_presets::PresetField* pf = createWidget<genra_presets::PresetField>(mm2px(Vec(r[0], r[1])));
	pf->box.size = mm2px(Vec(r[2], r[3]));
	pf->bank = bank;
	pf->module = m;
	pf->paramId = selParam;
	pf->accent = PINK;
	pf->sample = sample;
	mw->addChild(pf);
	return pf;
}
/** The jacks (layout arrays from the panel tool) and the lit strip over the GENRA jacks (the first seven inputs). */
template <size_t NI, size_t NO>
inline void jacks(ModuleWidget* mw, Module* m, const float (&in)[NI][2], const unsigned* inCol, const float (&out)[NO][2]) {
	for (size_t i = 0; i < NI; i++)
		addPort<NPort>(mw, in[i], m, (int) i, false, hex(inCol[i]));
	for (size_t i = 0; i < NO; i++)
		addPort<NPort>(mw, out[i], m, (int) i, true, PINK);
	std::vector<Vec> strip;
	for (int i = 0; i < 7; i++)
		strip.push_back(mm(in[i]));
	follow::addStrip(mw, m, strip);
}

/** The text in the display's corners: two lines left (main in cyan, info dim), a big note and a small line right. */
inline void corners(NVGcontext* vg, float W, const std::string& l1, const std::string& l2, const std::string& big,
                    const std::string& small) {
	float pad = mm2px(1.5f);
	plaque(vg, pad, pad, mm2px(48.f), mm2px(9.4f), false);
	plaque(vg, W - pad - mm2px(46.f), pad, mm2px(46.f), mm2px(10.6f), true);
	text(vg, pad + mm2px(2.f), pad + mm2px(4.f), string::uppercase(l1), 6.6f, CYAN, NVG_ALIGN_LEFT, 1.2f);
	text(vg, pad + mm2px(2.f), pad + mm2px(7.4f), l2, 5.8f, DIM, NVG_ALIGN_LEFT, 1.0f);
	text(vg, W - pad - mm2px(2.f), pad + mm2px(5.8f), big, 16.f, WHITE, NVG_ALIGN_RIGHT, 1.2f);
	text(vg, W - pad - mm2px(2.f), pad + mm2px(9.2f), string::uppercase(small), 5.4f, DIM, NVG_ALIGN_RIGHT, 1.0f);
}
/** "FOLLOW · MELODY", the tempo, or FREE. */
inline std::string followLine(follow::Host* h, bool clock, float bpm) {
	if (h && h->fol.active()) {
		int tr = h->fol.track.load();
		return std::string("FOLLOW · ") + (tr >= 0 && tr < follow::NUM_TRACKS ? follow::TRACK_NAMES[tr] : "");
	}
	return clock ? string::f("%.0f BPM", bpm) : "FREE · 120 BPM";
}
inline std::string noteName(float v) {
	char nn[8];
	neon::noteName(v, nn);
	return nn;
}

}  // namespace neonkit
