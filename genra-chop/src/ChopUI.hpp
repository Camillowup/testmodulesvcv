// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: the look shared by its modules (colours, fonts, buttons, fields).
// Buttons and fields have a right-click menu (MIDI learn, set by the module widget) and blink while they learn.
#pragma once
#include "plugin.hpp"
// ================================================================ the panel
static const NVGcolor C_GRAPH = nvgRGB(0x3b, 0x3a, 0x38), C_CREAM = nvgRGB(0xec, 0xe7, 0xda), C_RED = nvgRGB(0xc8, 0x31, 0x2b),
                      C_PAD = nvgRGB(0xb9, 0xb4, 0xa6), C_PADEDGE = nvgRGB(0x8f, 0x8a, 0x7c), C_INK = nvgRGB(0x3b, 0x3a, 0x38),
                      C_INKDIM = nvgRGB(0x77, 0x73, 0x6a), C_LCD = nvgRGB(0xa9, 0xb9, 0x8c), C_LCDINK = nvgRGB(0x24, 0x30, 0x1a),
                      C_YELLOW = nvgRGB(0xd6, 0xa6, 0x40), C_YELLOWINK = nvgRGB(0x2a, 0x24, 0x16);

/** The ring of a control that waits for MIDI learn. */
inline void learnRing(NVGcontext* vg, float w, float h) {
	float a = 0.5f + 0.5f * std::sin(8.f * (float) system::getTime());
	nvgBeginPath(vg);
	nvgRoundedRect(vg, -1.5f, -1.5f, w + 3.f, h + 3.f, 3.f);
	nvgStrokeColor(vg, nvgRGBAf(0.84f, 0.65f, 0.25f, a));
	nvgStrokeWidth(vg, 2.f);
	nvgStroke(vg);
}

inline std::shared_ptr<window::Font> uiBold() { return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf")); }
inline std::shared_ptr<window::Font> lcdFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}
inline void label(NVGcontext* vg, std::shared_ptr<window::Font> f, float x, float y, const std::string& s, float size,
                  NVGcolor col, int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE) {
	if (!f)
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, 0.4f);
	nvgFillColor(vg, col);
	nvgTextAlign(vg, align);
	nvgText(vg, x, y, s.c_str(), NULL);
}
inline Rect rectOf(const float* v) { return Rect(Vec(v[0], v[1]), Vec(v[2], v[3])); }

/** A button in the look (label in the button): press calls `act`; `lit` shows a toggle's state. */
struct HHButton : widget::OpaqueWidget {
	std::string text;
	NVGcolor col = C_GRAPH, textCol = C_CREAM;
	std::function<void()> act, release;
	std::function<bool()> lit, learning;
	std::function<void(ui::Menu*)> rightMenu;
	float fontSize = 9.5f;
	int symbol = 0; // 1 play triangle, 2 stop square (before the text)
	bool down = false;
	void draw(const DrawArgs& args) override {
		bool on = lit && lit();
		NVGcolor c = col;
		if (down)
			c = nvgLerpRGBA(c, nvgRGB(0, 0, 0), 0.25f);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.4f);
		nvgFillColor(args.vg, c);
		nvgFill(args.vg);
		if (on) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 1.f, 1.f, box.size.x - 2.f, box.size.y - 2.f, 2.f);
			nvgStrokeColor(args.vg, nvgRGB(0xff, 0xe0, 0x8a));
			nvgStrokeWidth(args.vg, 1.6f);
			nvgStroke(args.vg);
		}
		float tx = box.size.x / 2;
		if (symbol) {
			nvgFontFaceId(args.vg, uiBold()->handle);
			nvgFontSize(args.vg, 9.5f);
			nvgTextLetterSpacing(args.vg, 0.4f);
			float tw = nvgTextBounds(args.vg, 0, 0, text.c_str(), NULL, NULL);
			float x0 = (box.size.x - tw - 9.f) / 2, y = box.size.y / 2;
			nvgBeginPath(args.vg);
			if (symbol == 1) {
				nvgMoveTo(args.vg, x0, y - 3.2f);
				nvgLineTo(args.vg, x0 + 5.5f, y);
				nvgLineTo(args.vg, x0, y + 3.2f);
				nvgClosePath(args.vg);
			}
			else
				nvgRect(args.vg, x0, y - 2.7f, 5.4f, 5.4f);
			nvgFillColor(args.vg, C_CREAM);
			nvgFill(args.vg);
			tx = x0 + 9.f + tw / 2;
		}
		label(args.vg, uiBold(), tx, box.size.y / 2 + 0.5f, text, fontSize, textCol);
		if (learning && learning())
			learnRing(args.vg, box.size.x, box.size.y);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			down = true;
			if (act)
				act();
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS && rightMenu) {
			rightMenu(createMenu());
			e.consume(this);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		down = false;
		if (release)
			release();
	}
};

/** A field with a value: click opens the list, the mouse wheel steps through it. */
struct HHField : widget::OpaqueWidget {
	std::function<std::string()> value;
	std::function<int()> count, current;
	std::function<std::string(int)> item;
	std::function<void(int)> choose;
	std::function<bool()> learning;
	std::function<void(ui::Menu*)> rightMenu;
	void draw(const DrawArgs& args) override {
		if (learning && learning())
			learnRing(args.vg, box.size.x, box.size.y);
		std::string v = value ? value() : "";
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, 0, 0, box.size.x - 9.f, box.size.y);
		label(args.vg, uiBold(), 5.f, box.size.y / 2 + 0.5f, v, 9.5f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgRestore(args.vg);
		// the arrow
		float x = box.size.x - 6.f, y = box.size.y / 2;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x - 2.6f, y - 1.3f);
		nvgLineTo(args.vg, x + 2.6f, y - 1.3f);
		nvgLineTo(args.vg, x, y + 1.7f);
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, C_RED);
		nvgFill(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && count) {
			ui::Menu* menu = createMenu();
			int n = count(), cur = current ? current() : -1;
			for (int i = 0; i < n; i++)
				menu->addChild(createCheckMenuItem(item(i), "", [=]() { return cur == i; }, [=]() { choose(i); }));
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS && rightMenu) {
			rightMenu(createMenu());
			e.consume(this);
		}
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!count || !current)
			return;
		int n = count();
		int d = e.scrollDelta.y > 0 ? -1 : (e.scrollDelta.y < 0 ? 1 : 0);
		if (d && n > 0)
			choose(clamp(current() + d, 0, n - 1));
		e.consume(this);
	}
};

