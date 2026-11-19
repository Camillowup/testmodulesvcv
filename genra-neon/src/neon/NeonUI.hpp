// SPDX-License-Identifier: GPL-3.0-only
// GENRA NEON – the look of the neon modules (NEON-TERRAIN): knobs with a glowing value arc, hexagon jacks, chamfered
// neon buttons, selection fields (click: the list, wheel: the next one) and the helpers for the displays.
// Every control is a real parameter, so MIDI-MAP learns it with a click.
#pragma once
#include "../plugin.hpp"
#include "../../genra/ui/GenraUI.hpp"
#include <functional>

namespace neonui {

static const NVGcolor PINK = nvgRGB(0xff, 0x2b, 0xd6), CYAN = nvgRGB(0x22, 0xe6, 0xff), VIOLET = nvgRGB(0x8b, 0x5b, 0xff),
                      WHITE = nvgRGB(0xff, 0xff, 0xff), LBL = nvgRGB(0xd6, 0xcc, 0xff), LBL2 = nvgRGB(0xa9, 0x9b, 0xd6),
                      DIM = nvgRGB(0x9b, 0x8c, 0xd0), TRACK = nvgRGB(0x24, 0x18, 0x3d), DEEP = nvgRGB(0x05, 0x02, 0x0c);

inline Vec mm(const float* p) { return mm2px(Vec(p[0], p[1])); }
inline NVGcolor alpha(NVGcolor c, float a) {
	c.a = a;
	return c;
}
inline NVGcolor hex(unsigned v) { return nvgRGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }
inline std::shared_ptr<window::Font> font() { return genra_ui::boldFont(); }
inline float text(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor c, int align = NVG_ALIGN_LEFT,
                  float spacing = 0.6f) {
	return genra_ui::text(vg, font(), x, y, s, size, c, align, spacing);
}
/** A line with a soft glow around it (layer 1). */
inline void glowStroke(NVGcontext* vg, NVGcolor c, float w, float glow = 3.f) {
	nvgStrokeColor(vg, alpha(c, 0.18f));
	nvgStrokeWidth(vg, w * glow);
	nvgStroke(vg);
	nvgStrokeColor(vg, c);
	nvgStrokeWidth(vg, w);
	nvgStroke(vg);
}

/** The knob: a dark disc, a white pointer and a value arc in pink (main knobs) or cyan, glowing. */
struct NKnob : app::Knob {
	float r = 10.f;   // px
	int kind = 0;     // 0 cyan, 1 big pink, 2 small pink
	NKnob() {
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
	}
	void setup(const float* p) {
		r = mm2px(p[2]);
		kind = (int) p[3];
		box.size = Vec(2 * r + mm2px(2.6f), 2 * r + mm2px(2.6f));
		box.pos = mm(p).minus(box.size.div(2));
	}
	float frac() {
		engine::ParamQuantity* pq = getParamQuantity();
		return pq ? pq->getScaledValue() : 0.5f;
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		Vec c = box.size.div(2);
		float a0 = minAngle - M_PI / 2, a1 = maxAngle - M_PI / 2, rr = r + mm2px(1.2f);
		nvgBeginPath(vg);
		nvgArc(vg, c.x, c.y, rr, a0, a1, NVG_CW);
		nvgStrokeColor(vg, TRACK);
		nvgStrokeWidth(vg, kind == 1 ? mm2px(0.7f) : mm2px(0.45f));
		nvgStroke(vg);
		// body
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r);
		nvgFillPaint(vg, nvgRadialGradient(vg, c.x - r * 0.25f, c.y - r * 0.4f, r * 0.1f, r * 1.1f, nvgRGB(0x2c, 0x24, 0x40),
		                                   nvgRGB(0x05, 0x03, 0x09)));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(0x3a, 0x2d, 0x5c));
		nvgStrokeWidth(vg, mm2px(0.2f));
		nvgStroke(vg);
		float a = minAngle + (maxAngle - minAngle) * frac() - M_PI / 2;
		nvgBeginPath(vg);
		nvgMoveTo(vg, c.x + r * 0.3f * std::cos(a), c.y + r * 0.3f * std::sin(a));
		nvgLineTo(vg, c.x + r * 0.85f * std::cos(a), c.y + r * 0.85f * std::sin(a));
		nvgStrokeColor(vg, WHITE);
		nvgStrokeWidth(vg, r * 0.13f);
		nvgLineCap(vg, NVG_ROUND);
		nvgStroke(vg);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext* vg = args.vg;
			Vec c = box.size.div(2);
			float rr = r + mm2px(1.2f);
			engine::ParamQuantity* pq = getParamQuantity();
			float lo = minAngle - M_PI / 2;
			float from = lo, to = lo + (maxAngle - minAngle) * frac();
			if (pq && pq->getMinValue() < 0.f && pq->getMaxValue() > 0.f) {  // bipolar: from the middle
				from = lo + (maxAngle - minAngle) * 0.5f;
				if (to < from)
					std::swap(from, to);
			}
			if (to - from > 0.01f) {
				nvgBeginPath(vg);
				nvgArc(vg, c.x, c.y, rr, from, to, NVG_CW);
				nvgLineCap(vg, NVG_ROUND);
				glowStroke(vg, kind ? PINK : CYAN, kind == 1 ? mm2px(0.7f) : mm2px(0.45f));
			}
		}
		app::Knob::drawLayer(args, layer);
	}
};

/** A jack: the hexagon is on the panel; this is the hole. */
struct NPort : app::PortWidget {
	NVGcolor col = CYAN;
	NPort() { box.size = mm2px(Vec(7.f, 7.f)); }
	void draw(const DrawArgs& args) override {
		Vec c = box.size.div(2);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, mm2px(2.3f));
		nvgFillColor(args.vg, nvgRGB(0x14, 0x10, 0x24));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, mm2px(1.6f));
		nvgFillColor(args.vg, nvgRGB(0, 0, 0));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, alpha(col, 0.55f));
		nvgStrokeWidth(args.vg, mm2px(0.25f));
		nvgStroke(args.vg);
	}
};
template <class P>
P* addPort(ModuleWidget* mw, const float* p, Module* m, int id, bool out, NVGcolor col) {
	P* w = out ? createOutputCentered<P>(mm(p), m, id) : createInputCentered<P>(mm(p), m, id);
	w->col = col;
	if (out)
		mw->addOutput(w);
	else
		mw->addInput(w);
	return w;
}

/** A neon button over its frame on the panel: momentary (MUTATE, UNDO …) or latching (FREEZE). Lights while held / on. */
struct NButton : app::Switch {
	NVGcolor col = PINK;
	std::function<bool()> lit;  // extra: lit for a moment (the module flashes it)
	void setup(const float* r, bool latch) {
		box.pos = mm2px(Vec(r[0], r[1]));
		box.size = mm2px(Vec(r[2], r[3]));
		momentary = !latch;
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			engine::ParamQuantity* pq = getParamQuantity();
			bool on = (pq && pq->getValue() > 0.5f) || (lit && lit());
			if (on) {
				NVGcontext* vg = args.vg;
				float w = box.size.x, h = box.size.y, c = mm2px(1.4f);
				nvgBeginPath(vg);
				nvgMoveTo(vg, 0, 0);
				nvgLineTo(vg, w - c, 0);
				nvgLineTo(vg, w, c);
				nvgLineTo(vg, w, h);
				nvgLineTo(vg, 0, h);
				nvgClosePath(vg);
				nvgFillColor(vg, alpha(col, 0.35f));
				nvgFill(vg);
				glowStroke(vg, col, mm2px(0.4f));
			}
		}
		app::Switch::drawLayer(args, layer);
	}
};

/** A selection field over its frame: the name (and a short info), a click opens the list, the wheel steps. */
struct Field : app::ParamWidget {
	std::function<int()> count;
	std::function<std::string(int)> name, info;
	std::string title, undo;
	void setup(const float* r) {
		box.pos = mm2px(Vec(r[0], r[1]));
		box.size = mm2px(Vec(r[2], r[3]));
	}
	int value() {
		engine::ParamQuantity* pq = getParamQuantity();
		int n = count ? count() : 1;
		return pq ? clamp((int) std::round(pq->getValue()), 0, std::max(0, n - 1)) : 0;
	}
	void set(int t) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		float old = pq->getValue();
		if ((int) std::round(old) == t)
			return;
		pq->setValue((float) t);
		history::ParamChange* h = new history::ParamChange;
		h->name = undo;
		h->moduleId = module->id;
		h->paramId = paramId;
		h->oldValue = old;
		h->newValue = (float) t;
		APP->history->push(h);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			ParamWidget::onButton(e);  // touch the param: MIDI-MAP learns it
			Menu* menu = createMenu();
			menu->addChild(createMenuLabel(title));
			int n = count ? count() : 0;
			for (int i = 0; i < n; i++)
				menu->addChild(createCheckMenuItem(name(i), info ? info(i) : "", [=]() { return value() == i; }, [=]() { set(i); }));
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (module && e.scrollDelta.y != 0.f) {
			int n = count ? count() : 1;
			set(clamp(value() + (e.scrollDelta.y < 0.f ? 1 : -1), 0, n - 1));
			e.consume(this);
			return;
		}
		ParamWidget::onHoverScroll(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int t = module ? value() : 0;
			std::string s = name ? name(t) : "";
			float y = box.size.y / 2 + 2.4f;
			text(args.vg, mm2px(1.6f), y, s, 7.0f, WHITE, NVG_ALIGN_LEFT, 0.8f);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** The lamp row (voice mode, chord source) that MIDI-MAP can learn with a click. */
struct NLamps : genra_ui::LampRow {
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module)
			APP->scene->rack->setTouchedParam(this);
		genra_ui::LampRow::onButton(e);
	}
};

/** The panel's small info plaque in a display corner. */
inline void plaque(NVGcontext* vg, float x, float y, float w, float h, bool right) {
	nvgBeginPath(vg);
	if (!right) {
		nvgMoveTo(vg, x, y);
		nvgLineTo(vg, x + w - 6, y);
		nvgLineTo(vg, x + w, y + 6);
		nvgLineTo(vg, x + w, y + h);
		nvgLineTo(vg, x, y + h);
	} else {
		nvgMoveTo(vg, x + w, y);
		nvgLineTo(vg, x + 6, y);
		nvgLineTo(vg, x, y + 6);
		nvgLineTo(vg, x, y + h);
		nvgLineTo(vg, x + w, y + h);
	}
	nvgClosePath(vg);
	nvgFillColor(vg, alpha(DEEP, 0.72f));
	nvgFill(vg);
}

/** The glitch title flicker etc. are not needed: the panel has them. Scanlines over a display (layer 1). */
inline void scanlines(NVGcontext* vg, float w, float h) {
	nvgBeginPath(vg);
	for (float y = 0.f; y < h; y += 2.4f)
		nvgRect(vg, 0, y, w, 0.9f);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 70));
	nvgFill(vg);
}

}  // namespace neonui
