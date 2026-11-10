// SPDX-License-Identifier: GPL-3.0-only
// The GENRA look for the JIANT voices (VOICE, DRUM-X, ANIMAL): the colours, the fonts, the display's helpers and the
// few widgets the three share. The panels themselves come from tools/jiant_panel.py (res/Jiant*.svg + JiantLayout.hpp).
#pragma once
#include "../plugin.hpp"
#include "JiantLayout.hpp"
#include <blendish.h>
#include <atomic>
#include <cstring>
#include <functional>

namespace jiant {

/** The cores' 44.1 kHz blocks into Rack's rate (quality 6). The firmware hushes itself when idle: once only exact
 * silence has gone through the converter for longer than its filter (its history all zeros, its output exactly 0),
 * the converter rests and zeros go straight into the buffer at the same ratio; the first sound wakes it with its
 * state fresh (all zeros, as it was), so the sound and the buffer's fill (the latency) stay as they were, to within
 * the converter's sub-sample phase. At 44.1 kHz nothing is converted, as before. */
struct Resampler {
	dsp::SampleRateConverter<2> src;
	uint32_t zeros = 0;      // frames of exact silence in a row through the converter
	bool resting = false;
	uint64_t acc = 0;        // resting: the output frames owed, in units of 1 / devRate
	int lastRate = 0;

	Resampler() { src.setQuality(6); }
	void reset() {
		src.refreshState();
		zeros = 0;
		resting = false;
	}

	static bool silent(const dsp::Frame<2>* in, int n) {
		for (int i = 0; i < n; i++)
			if (in[i].samples[0] != 0.f || in[i].samples[1] != 0.f)
				return false;
		return true;
	}

	template <class Buf>
	void process(dsp::Frame<2>* in, int n, int devRate, int rate, Buf& outBuf) {
		if (rate == devRate) {
			for (int i = 0; i < n && !outBuf.full(); i++)
				outBuf.push(in[i]);
			zeros = 0;
			resting = false;
			return;
		}
		bool quiet = silent(in, n);
		if (resting && (!quiet || rate != lastRate)) {
			src.setRates(devRate, rate);
			src.refreshState();               // its history: the zeros it would hold
			resting = false;
			zeros = 0;
		}
		if (resting) {
			acc += (uint64_t) n * (uint64_t) rate;
			uint64_t k = acc / (uint64_t) devRate;
			acc -= k * (uint64_t) devRate;
			size_t m = std::min((size_t) k, outBuf.capacity());
			if (m) {
				std::memset((void*) outBuf.endData(), 0, m * sizeof(dsp::Frame<2>));
				outBuf.endIncr(m);
			}
			return;
		}
		src.setRates(devRate, rate);
		int inLen = n, outLen = (int) outBuf.capacity();
		src.process(in, &inLen, outBuf.endData(), &outLen);
		outBuf.endIncr(outLen);
		if (!quiet) {
			zeros = 0;
			return;
		}
		zeros += (uint32_t) n;
		// the filter's length: at most 256 taps (quality 10; 6 has 96), times the ratio when it lowers the rate
		uint32_t need = 512u * (uint32_t) (1 + devRate / std::max(1, rate)) + 256u;
		if (zeros >= need) {
			resting = true;
			lastRate = rate;
			acc = (uint64_t) devRate / 2;
		}
	}
};

/** When a module's screen was last drawn: Rack draws no widget off the screen, so the audio thread renders the
 * firmware's screen only while it is seen (and at once when it is seen again). */
struct ScreenClock {
	std::atomic<double> drawn{-1e9};
	void mark() { drawn.store(system::getTime(), std::memory_order_relaxed); }
	bool seen() const { return system::getTime() - drawn.load(std::memory_order_relaxed) < 0.2; }
};

// one colour per module (keep in step with tools/jiant_panel.py)
#define JIANT_VOICE_RGB 0xff, 0x4f, 0x5a
#define JIANT_DRUMX_RGB 0x7b, 0x6c, 0xff
#define JIANT_ANIMAL_RGB 0x4f, 0xe0, 0x8a

static const NVGcolor WHITE = nvgRGB(0xf2, 0xf3, 0xf7), TXT = nvgRGB(0xd9, 0xdb, 0xe2), DIM = nvgRGB(0x8a, 0x8d, 0x99),
                      CHIP = nvgRGB(0x1a, 0x1b, 0x21), OFF = nvgRGB(0x22, 0x23, 0x2a), INK = nvgRGB(0x0a, 0x0a, 0x0d),
                      FIELD = nvgRGB(0x0d, 0x0e, 0x12), EDGE = nvgRGB(0x34, 0x36, 0x3f), ERR = nvgRGB(0xff, 0x45, 0x3a);

inline Vec at(const float* p) { return Vec(p[0], p[1]); }
inline Rect area(const float* p) { return Rect(p[0], p[1], p[2], p[3]); }

inline std::shared_ptr<window::Font> boldFont() {
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
}
inline std::shared_ptr<window::Font> monoFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

/** Text at a baseline; align NVG_ALIGN_LEFT / CENTER / RIGHT. */
inline void text(NVGcontext* vg, const std::shared_ptr<window::Font>& f, float x, float y, const std::string& s, float size,
                 NVGcolor c, int align = NVG_ALIGN_LEFT, float spacing = 0.4f) {
	if (!f || f->handle < 0 || s.empty())
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, spacing);
	nvgTextAlign(vg, align | NVG_ALIGN_BASELINE);
	nvgFillColor(vg, c);
	nvgText(vg, x, y, s.c_str(), nullptr);
}

inline void roundRect(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor fill) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, r);
	nvgFillColor(vg, fill);
	nvgFill(vg);
}

/** A chip as in GENRA's chord row: lit in the module's colour with dark text, else dark with dim text. */
inline void chip(NVGcontext* vg, float x, float y, float w, float h, const std::string& s, bool on, NVGcolor accent,
                 float size = 5.4f) {
	roundRect(vg, x, y, w, h, 2.2f, on ? accent : CHIP);
	text(vg, boldFont(), x + w / 2, y + h / 2 + size * 0.36f, s, size, on ? INK : DIM, NVG_ALIGN_CENTER, 0.3f);
}

/** The firmware's RGB565 canvas (byte-swapped, the FM-1 panel's order) as a NanoVG image. */
struct Canvas565 {
	int w, h;
	int image = -1;
	NVGcontext* owner = nullptr;
	uint32_t seq = ~0u;
	std::vector<uint8_t> rgba;
	Canvas565(int w, int h) : w(w), h(h), rgba(w * h * 4, 0) {}
	~Canvas565() {
		if (image >= 0 && owner)
			nvgDeleteImage(owner, image);
	}
	void draw(NVGcontext* vg, const uint16_t* px, uint32_t s, float bw, float bh) {
		if (image >= 0 && owner != vg)
			image = -1;
		if (s != seq || image < 0) {
			seq = s;
			for (int i = 0; i < w * h; i++) {
				uint16_t v = px[i];
				uint16_t p = (uint16_t) ((v >> 8) | (v << 8));
				rgba[i * 4 + 0] = (uint8_t) (((p >> 11) & 31) * 255 / 31);
				rgba[i * 4 + 1] = (uint8_t) (((p >> 5) & 63) * 255 / 63);
				rgba[i * 4 + 2] = (uint8_t) ((p & 31) * 255 / 31);
				rgba[i * 4 + 3] = 255;
			}
			if (image < 0) {
				image = nvgCreateImageRGBA(vg, w, h, 0, rgba.data());
				owner = vg;
			} else {
				nvgUpdateImage(vg, image, rgba.data());
			}
		}
		if (image < 0)
			return;
		NVGpaint paint = nvgImagePattern(vg, 0, 0, bw, bh, 0.f, image, 1.f);
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, bw, bh);
		nvgFillPaint(vg, paint);
		nvgFill(vg);
	}
};

/** An error over the screen (the core could not be loaded). */
inline void drawStatus(NVGcontext* vg, const std::string& s, float w) {
	auto f = monoFont();
	if (!f || f->handle < 0)
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, 8.f);
	nvgFillColor(vg, ERR);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgTextBox(vg, 6, 6, w - 12, s.c_str(), nullptr);
}

/** The light of a lit button, in the module's colour. */
template <int R, int G, int B>
struct AccentLight : GrayModuleLightWidget {
	AccentLight() { addBaseColor(nvgRGB(R, G, B)); }
};

/** A row of lamps for a switch parameter (POLY · MONO · LEGATO): the one in use lit; a click picks one. The names are
 * on the panel. Placed with its first lamp at (x0, y), the next ones dx apart. */
struct LampRow : app::ParamWidget {
	NVGcolor accent = WHITE;
	int n = 3;
	float dx = 40.f;
	void place(const float* p, int count) {
		n = count;
		dx = p[2];
		box.pos = Vec(p[0] - dx / 2, p[1] - 8);
		box.size = Vec(dx * n, 16);
	}
	int value() {
		engine::ParamQuantity* pq = getParamQuantity();
		return pq ? (int) std::round(pq->getValue()) : 0;
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (engine::ParamQuantity* pq = getParamQuantity())
				pq->setValue((float) clamp((int) (e.pos.x / dx), 0, n - 1));
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {
		int v = value();
		for (int i = 0; i < n; i++) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, dx * (i + 0.5f), box.size.y / 2, 3.6f);
			nvgFillColor(args.vg, i == v ? accent : nvgRGB(0x3d, 0x3f, 0x48));
			nvgFill(args.vg);
		}
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			float cx = dx * (value() + 0.5f), cy = box.size.y / 2;
			NVGcolor glow = accent;
			glow.a = 0.35f;
			NVGcolor none = accent;
			none.a = 0.f;
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cx, cy, 8.f);
			nvgFillPaint(args.vg, nvgRadialGradient(args.vg, cx, cy, 2.f, 8.f, glow, none));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cx, cy, 3.6f);
			nvgFillColor(args.vg, accent);
			nvgFill(args.vg);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** Chips for a switch parameter, in the display (ANIMAL's MODE, VOICE's LANG): the one in use lit; a click picks one. */
struct ChipRow : app::ParamWidget {
	NVGcolor accent = WHITE;
	std::vector<std::string> names;
	float gap = 3.f;
	float chipW() const { return (box.size.x - gap * (names.size() - 1)) / names.size(); }
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (engine::ParamQuantity* pq = getParamQuantity())
				pq->setValue((float) clamp((int) (e.pos.x / (chipW() + gap)), 0, (int) names.size() - 1));
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			engine::ParamQuantity* pq = getParamQuantity();
			int v = pq ? (int) std::round(pq->getValue()) : 0;
			float w = chipW();
			for (size_t i = 0; i < names.size(); i++)
				chip(args.vg, i * (w + gap), 0, w, box.size.y, names[i], (int) i == v, accent);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

/** A knob's name and value under it (the name follows MODE / LANG). */
struct KnobText : TransparentWidget {
	NVGcolor accent = WHITE;
	std::function<std::string()> label, value;
	void draw(const DrawArgs& args) override {
		float cx = box.size.x / 2;
		if (label)
			text(args.vg, boldFont(), cx, 6.5f, label(), 5.6f, TXT, NVG_ALIGN_CENTER, 0.4f);
		if (value)
			text(args.vg, monoFont(), cx, 13.5f, value(), 6.f, accent, NVG_ALIGN_CENTER, 0.f);
	}
};

/** A text field in the display, in the display's mono font and the module's colour. */
struct DisplayTextField : app::LedDisplayTextField {
	float fontSize = 8.f;
	DisplayTextField() {
		bgColor = nvgRGBA(0, 0, 0, 0);
		textOffset = Vec(0, -3.5f);  // blendish puts the baseline 14 px below y and 4 px in
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		if (layer == 1) {
			std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
			if (font && font->handle >= 0) {
				bndSetFont(font->handle);
				NVGcolor hl = color;
				hl.a = 0.5f;
				int begin = std::min(cursor, selection);
				int end = (this == APP->event->selectedWidget) ? std::max(cursor, selection) : -1;
				if (text.empty() && this != APP->event->selectedWidget)
					bndIconLabelCaret(args.vg, textOffset.x, textOffset.y, box.size.x - 2 * textOffset.x, box.size.y,
					                  -1, DIM, fontSize, placeholder.c_str(), hl, -1, -1);
				else
					bndIconLabelCaret(args.vg, textOffset.x, textOffset.y, box.size.x - 2 * textOffset.x, box.size.y,
					                  -1, color, fontSize, text.c_str(), hl, begin, end);
				bndSetFont(APP->window->uiFont->handle);
			}
		}
		Widget::drawLayer(args, layer);
		nvgResetScissor(args.vg);
	}
	int getTextPosition(math::Vec mousePos) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(fontPath);
		if (!font || font->handle < 0)
			return 0;
		bndSetFont(font->handle);
		int pos = bndIconLabelTextPosition(APP->window->vg, textOffset.x, textOffset.y, box.size.x - 2 * textOffset.x,
		                                   box.size.y, -1, fontSize, text.c_str(), mousePos.x, mousePos.y);
		bndSetFont(APP->window->uiFont->handle);
		return pos;
	}
};

/** The four screws, as on GENRA. */
inline void addScrews(ModuleWidget* w) {
	w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewBlack>(Vec(w->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	w->addChild(createWidget<ScrewBlack>(Vec(w->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
}

}  // namespace jiant
