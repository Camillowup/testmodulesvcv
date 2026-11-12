// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;
extern Model* modelGenraHipHop;

// the knobs of the production-center look (res/HHKnob*.svg, from tools/hiphop_panel.py)
struct HHKnob : app::SvgKnob {
	HHKnob() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/HHKnob.svg")));
	}
};
struct HHKnobSmall : app::SvgKnob {
	HHKnobSmall() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/HHKnobSmall.svg")));
	}
};
