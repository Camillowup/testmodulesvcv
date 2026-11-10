#pragma once
#include <rack.hpp>

using namespace rack;

// the plugin's own knobs: dark, the same in every module (res/knob/*.svg; sizes as Rack's round knobs)
struct GenraKnob : RoundKnob {
	void load(const char* n);
};
struct GLargeKnob : GenraKnob {
	GLargeKnob() { load("large"); }
};
struct GKnob : GenraKnob {
	GKnob() { load("medium"); }
};
struct GSmallKnob : GenraKnob {
	GSmallKnob() { load("small"); }
};
struct GTrimpot : GenraKnob {
	GTrimpot() { load("trim"); }
};

extern Plugin* pluginInstance;
extern Model* modelGenra;
extern Model* modelGenraGrid;
extern Model* modelDrumX;
extern Model* modelGenraMono;
extern Model* modelSchmelz;
extern Model* modelFunke;
extern Model* modelMix;
extern Model* modelStotter;
extern Model* modelAblauf;
extern Model* modelGrund;
extern Model* modelGrundFx;
extern Model* modelGenraFx;
extern Model* modelSchlag;
