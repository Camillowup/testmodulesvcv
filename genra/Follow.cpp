// SPDX-License-Identifier: GPL-3.0-only
// FOLLOW GENRA (see Follow.hpp)
#include "Follow.hpp"
#include "link/GenraPorts.hpp"
#include <mutex>
#include <algorithm>

namespace follow {

const char* const TRACK_NAMES[NUM_TRACKS] = {"Bass", "Melody", "Pad", "Drums"};

NVGcolor trackColor(int track) {
	switch (track) {
		case T_BASS: return nvgRGB(0xb8, 0xf2, 0x3a);
		case T_MEL: return nvgRGB(0x3d, 0xc9, 0xff);
		case T_PAD: return nvgRGB(0xe0, 0x77, 0x9e);
		case T_DRUMS: return nvgRGB(0x7b, 0x6c, 0xff);
		default: return nvgRGB(0x8a, 0x8d, 0x99);
	}
}

// ---------------------------------------------------------------- references (kept in the link hub: link/Hub.hpp)
Ref::~Ref() { genralink::hub().unref(this); }

void point(Ref& r, Module* owner, Module* to) {
	genralink::Hub& h = genralink::hub();
	h.point(&r, owner, nullptr); // (readers see nothing while the GENRA info changes)
	genralink::GenraInfo gi;
	if (!to || !h.genraInfo(to, &gi))
		gi = genralink::GenraInfo();
	r.genra = gi;
	if (to)
		h.point(&r, owner, to);
}

void gone(Module* m) { genralink::hub().gone(m); }

bool isGenra(Module* m) {
	genralink::GenraInfo gi;
	return m && m->model && m->model->slug == "Genra" && m->model->plugin && m->model->plugin->slug == "Genra" &&
	       genralink::hub().genraInfo(m, &gi);
}

void addGenra(Module* g, const float* followPub, int outputs, const float* stepPeriod) {
	genralink::GenraInfo gi;
	gi.followPub = followPub;
	gi.outputs = outputs;
	gi.stepPeriod = stepPeriod;
	genralink::hub().addGenra(g, gi);
}

float genraBpm(const Ref& r, float sr) {
	const float* p = r.genra.stepPeriod;
	float sp = p ? *p : 0.f; // samples per 16th, set every sample (a benign racy read)
	return sp > 1.f ? clamp(60.f * sr / (sp * 4.f), 20.f, 400.f) : 120.f;
}

const float* genraFrame(const Ref& r, int outputId, int64_t frame) {
	const genralink::GenraInfo& gi = r.genra;
	if (!gi.followPub || outputId < 0 || outputId >= gi.outputs)
		return nullptr;
	return gi.followPub + ((size_t) ((frame + 1) & 1) * gi.outputs + outputId) * genraports::PUB_CHANNELS;
}

Src genraOut(int track, int sig) {
	Src s;
	if (sig == CLOCK) {
		s.id = genraports::CLOCK_OUTPUT;
		return s;
	}
	if (sig == RESET) {
		s.id = genraports::RESET_OUTPUT;
		return s;
	}
	switch (track) {
		case T_BASS:
		case T_MEL: {
			bool b = track == T_BASS;
			switch (sig) {
				case VOCT: s.id = b ? genraports::BASS_VOCT_OUTPUT : genraports::MEL_VOCT_OUTPUT; break;
				case GATE: s.id = b ? genraports::BASS_GATE_OUTPUT : genraports::MEL_GATE_OUTPUT; break;
				case VEL: s.id = b ? genraports::BASS_VEL_OUTPUT : genraports::MEL_VEL_OUTPUT; break;
				case ACC: s.id = b ? genraports::BASS_ACC_OUTPUT : genraports::MEL_ACC_OUTPUT; break;
				case SLIDE: s.id = b ? genraports::BASS_SLIDE_OUTPUT : genraports::MEL_SLIDE_OUTPUT; break;
				default: break;
			}
			break;
		}
		case T_PAD:
			switch (sig) {
				case VOCT: s.id = genraports::PAD_VOCT_OUTPUT, s.channels = 4; break;
				case GATE: s.id = genraports::PAD_GATE_OUTPUT, s.channels = 4; break;
				case VEL: s.id = genraports::PAD_VEL_OUTPUT; break;
				case MOD: s.id = genraports::PAD_MOD_OUTPUT; break;
				default: break;
			}
			break;
		case T_DRUMS:
			if (sig == DRUMS)
				s.id = genraports::DRUM_OUTPUT, s.channels = genraports::NUM_DRUM_OUTPUTS; // the 8 trigger outputs, one channel each
			break;
		default: break;
	}
	return s;
}

// ---------------------------------------------------------------- the virtual cable
bool Drive::cableIn(const Input& in) const { return cable; }

bool Drive::feed(Input& in, const float* src, int n) {
	if (cableIn(in)) {
		ch = -1;
		owned = false;
		return false;
	}
	n = clamp(n, 1, PORT_MAX_CHANNELS);
	for (int c = 0; c < n; c++)
		in.voltages[c] = v[c] = src[c];
	for (int c = n; c < PORT_MAX_CHANNELS; c++)
		in.voltages[c] = 0.f;
	in.channels = (uint8_t) n; // (setChannels() would not connect a port, so directly)
	ch = n;
	owned = true;
	return true;
}

void Drive::release(Input& in) {
	if (ch >= 0 && !cableIn(in)) {
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			in.voltages[c] = 0.f;
		in.channels = 0;
	}
	ch = -1;
	owned = false;
}

// ---------------------------------------------------------------- the follower
void Follower::process(Module* self, int64_t frame) {
	Module* g = genra.to.load();
	int t = track.load();
	if (!g && t < 0 && !anyDriven)
		return; // nothing to do (the usual case)
	anyDriven = false;
	for (size_t i = 0; i < binds.size(); i++) {
		Input& in = self->inputs[binds[i].input];
		Src s = (g && t >= 0) ? genraOut(t, binds[i].sig) : Src();
		if (s.id < 0) {
			if (drv[i].ch >= 0)
				drv[i].release(in);
			continue;
		}
		anyDriven = true;
		float v[PORT_MAX_CHANNELS];
		bool ok = true;
		if (binds[i].sig == DRUMS) {
			for (int c = 0; c < s.channels && ok; c++) {
				const float* f = genraFrame(genra, s.id + c, frame);
				ok = f != nullptr;
				v[c] = ok ? f[0] : 0.f;
			}
		}
		else {
			const float* f = genraFrame(genra, s.id, frame);
			ok = f != nullptr;
			for (int c = 0; c < s.channels && ok; c++)
				v[c] = f[c];
		}
		if (!ok) {
			if (drv[i].ch >= 0)
				drv[i].release(in);
			continue;
		}
		drv[i].feed(in, v, s.channels);
	}
}

void Follower::portChange(const Module::PortChangeEvent& e) {
	if (e.type != Port::INPUT)
		return;
	for (size_t i = 0; i < binds.size(); i++)
		if (binds[i].input == e.portId)
			drv[i].cable = e.connecting;
}

void Follower::uiStep(Module* self, float dt) {
	uiTimer -= dt;
	Module* now = genra.to.load();
	if (track.load() < 0) {
		if (now)
			point(genra, self, nullptr);
		return;
	}
	if (now && uiTimer > 0.f)
		return;
	uiTimer = 0.5f;
	Module* want = nullptr;
	int64_t id = genraId.load();
	if (id >= 0) {
		Module* m = APP->engine->getModule(id);
		if (isGenra(m))
			want = m;
	}
	if (!want) { // automatic, or the chosen one is not there (e.g. a pasted copy): the only / first GENRA
		if (now && id < 0)
			want = now; // still there
		else
		for (int64_t mid : APP->engine->getModuleIds()) {
			Module* m = APP->engine->getModule(mid);
			if (isGenra(m)) {
				want = m;
				break;
			}
		}
	}
	if (want != now)
		point(genra, self, want);
}

void Follower::toJson(json_t* root) {
	json_t* o = json_object();
	json_object_set_new(o, "track", json_integer(track.load()));
	json_object_set_new(o, "genra", json_integer(genraId.load()));
	json_object_set_new(root, "followGenra", o);
}

void Follower::fromJson(json_t* root) {
	json_t* o = json_object_get(root, "followGenra");
	if (!o)
		return;
	if (json_t* j = json_object_get(o, "track"))
		track = clamp((int) json_integer_value(j), -1, NUM_TRACKS - 1);
	if (json_t* j = json_object_get(o, "genra"))
		genraId = (int64_t) json_integer_value(j);
	uiTimer = 0.f;
}

bool Follower::gateHigh(Module* self) {
	for (size_t i = 0; i < binds.size(); i++) {
		if (!drv[i].owned)
			continue;
		int sig = binds[i].sig;
		if (sig != GATE && sig != DRUMS)
			continue;
		const Input& in = self->inputs[binds[i].input];
		for (int c = 0; c < in.channels; c++)
			if (in.voltages[c] >= 1.f)
				return true;
	}
	return false;
}

void Follower::appendMenu(Menu* menu, Module* self) {
	Follower* f = this;
	int t = track.load();
	menu->addChild(new MenuSeparator);
	menu->addChild(createSubmenuItem("Follow GENRA", t < 0 ? "Off" : TRACK_NAMES[t], [=](Menu* sub) {
		sub->addChild(createCheckMenuItem("Off (cables only)", "", [=]() { return f->track.load() < 0; },
		                                  [=]() { f->track = -1; }));
		for (int k = 0; k < NUM_TRACKS; k++) {
			if (f->drumsOnly != (k == T_DRUMS))
				continue;
			sub->addChild(createCheckMenuItem(TRACK_NAMES[k], "", [=]() { return f->track.load() == k; }, [=]() {
				f->track = k;
				f->uiTimer = 0.f;
			}));
		}
		sub->addChild(new MenuSeparator);
		sub->addChild(createMenuLabel("Which GENRA"));
		sub->addChild(createCheckMenuItem("Automatic (the only / first one)", "", [=]() { return f->genraId.load() < 0; },
		                                  [=]() {
			                                  f->genraId = -1;
			                                  point(f->genra, self, nullptr);
			                                  f->uiTimer = 0.f;
		                                  }));
		int n = 0;
		for (int64_t id : APP->engine->getModuleIds()) {
			if (!isGenra(APP->engine->getModule(id)))
				continue;
			n++;
			sub->addChild(createCheckMenuItem(string::f("GENRA #%d", n), "", [=]() { return f->genraId.load() == id; },
			                                  [=]() {
				                                  f->genraId = id;
				                                  f->uiTimer = 0.f;
				                                  point(f->genra, self, nullptr);
			                                  }));
		}
		sub->addChild(new MenuSeparator);
		sub->addChild(createMenuLabel("A cable in a jack always wins"));
	}));
}

Host* host(Module* m) {
	if (!m)
		return nullptr;
	if (m->model && m->model->plugin == pluginInstance)
		return dynamic_cast<Host*>(m); // a module of this plugin
	return genralink::hub().findHost(m); // of the other plugin: only as its panel made it known
}

void knownHost(Module* m) {
	if (!m || !m->model || m->model->plugin != pluginInstance)
		return;
	if (Host* h = dynamic_cast<Host*>(m))
		genralink::hub().addHost(m, h);
}

// ---------------------------------------------------------------- the lit strip
static const float SEG_W = 7.6f, SEG_H = 0.55f, SEG_DY = -4.6f + 0.275f; // mm (tools/panellib.py: SLOT 9.4, tray top -4.6)

void StripLight::step() {
	Widget::step();
	Host* h = host(module);
	if (!h)
		return;
	float dt = APP->window->getLastFrameDuration();
	h->fol.uiStep(module, dt);
	bool g = h->fol.active() && h->fol.gateHigh(module);
	if (g && !lastGate)
		flash = 1.f;
	lastGate = g;
	flash = std::max(0.f, flash - dt * 8.f);
}

void StripLight::drawLayer(const DrawArgs& args, int layer) {
	Widget::drawLayer(args, layer);
	if (layer != 1)
		return;
	Host* h = host(module);
	if (!h || !h->fol.active())
		return;
	int t = h->fol.track.load();
	float w = mm2px(SEG_W), hh = std::max(1.5f, mm2px(SEG_H)), dy = mm2px(SEG_DY);
	float a = 0.75f + 0.25f * flash;
	for (auto& jp : jacks) {
		size_t i = (size_t) jp.second;
		if (i >= h->fol.drv.size() || !h->fol.drv[i].owned)
			continue;
		int sig = h->fol.binds[i].sig;
		NVGcolor c = (sig == CLOCK || sig == RESET) ? nvgRGB(0xe6, 0xe7, 0xec) : trackColor(t);
		float x = jp.first.x - box.pos.x - w / 2, y = jp.first.y - box.pos.y + dy - hh / 2;
		// glow
		NVGpaint glow = nvgBoxGradient(args.vg, x, y, w, hh, hh / 2, mm2px(1.6f), nvgTransRGBAf(c, 0.45f * a),
		                               nvgTransRGBAf(c, 0.f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x - 10, y - 10, w + 20, hh + 20);
		nvgFillPaint(args.vg, glow);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, x, y, w, hh, hh / 2);
		nvgFillColor(args.vg, nvgTransRGBAf(c, a));
		nvgFill(args.vg);
	}
}

NVGcolor mixColor(int ch) { return trackColor(ch == 0 ? T_DRUMS : ch == 1 ? T_BASS : ch == 2 ? T_MEL : T_PAD); }

void ring(NVGcontext* vg, Vec c, NVGcolor col, float a) {
	float r = mm2px(4.3f);
	NVGpaint glow = nvgRadialGradient(vg, c.x, c.y, r * 0.75f, r * 1.45f, nvgTransRGBAf(col, 0.55f * a), nvgTransRGBAf(col, 0.f));
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r * 1.5f);
	nvgFillPaint(vg, glow);
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r);
	nvgStrokeColor(vg, nvgTransRGBAf(col, a));
	nvgStrokeWidth(vg, 1.4f);
	nvgStroke(vg);
}

void segment(NVGcontext* vg, Vec jack, NVGcolor c, float a) {
	float w = mm2px(7.6f), hh = std::max(1.5f, mm2px(0.55f)), dy = mm2px(-4.6f + 0.275f);
	float x = jack.x - w / 2, y = jack.y + dy - hh / 2;
	NVGpaint glow = nvgBoxGradient(vg, x, y, w, hh, hh / 2, mm2px(1.6f), nvgTransRGBAf(c, 0.45f * a), nvgTransRGBAf(c, 0.f));
	nvgBeginPath(vg);
	nvgRect(vg, x - 10, y - 10, w + 20, hh + 20);
	nvgFillPaint(vg, glow);
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, hh, hh / 2);
	nvgFillColor(vg, nvgTransRGBAf(c, a));
	nvgFill(vg);
}

void OutLight::step() {
	Widget::step();
	if (!known && module) { // (once: MIX of the other plugin can take this module's audio from now on)
		knownHost(module);
		known = true;
	}
	Host* h = host(module);
	if (found || !h)
		return;
	ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
	if (!mw)
		return;
	PortWidget* pl = h->audioL >= 0 ? mw->getOutput(h->audioL) : nullptr;
	PortWidget* pr = h->audioR >= 0 ? mw->getOutput(h->audioR) : nullptr;
	if (!pl)
		return;
	l = pl->box.getCenter();
	r = pr ? pr->box.getCenter() : l;
	found = true;
}

void OutLight::drawLayer(const DrawArgs& args, int layer) {
	Widget::drawLayer(args, layer);
	Host* h = host(module);
	if (layer != 1 || !h || !found || !h->pulled(APP->engine->getFrame()))
		return;
	int ch = clamp(h->pulledBy.load(), 0, 3);
	NVGcolor c = mixColor(ch);
	segment(args.vg, l, c); // (a small lit bar over each, as over the inputs)
	if (r.x != l.x || r.y != l.y)
		segment(args.vg, r, c);
	auto font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
	if (font && font->handle >= 0) {
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 8.f);
		nvgTextLetterSpacing(args.vg, 0.4f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, c);
		std::string t = string::f("\xe2\x80\xba MIX CH %d", ch + 1);
		nvgText(args.vg, (l.x + r.x) / 2, std::min(l.y, r.y) - mm2px(6.2f), t.c_str(), nullptr);
	}
}

OutLight* addOutLight(ModuleWidget* mw, Module* module) {
	OutLight* o = new OutLight;
	o->module = module;
	o->box.size = mw->box.size;
	mw->addChild(o);
	return o;
}

StripLight* addStrip(ModuleWidget* mw, Module* module, const std::vector<Vec>& pos) {
	addOutLight(mw, module);
	StripLight* s = new StripLight; // (transparent: clicks go to the jacks below)
	s->module = module;
	s->box.pos = Vec(0, 0);
	s->box.size = mw->box.size;
	for (size_t i = 0; i < pos.size(); i++)
		s->jacks.push_back({pos[i], (int) i});
	mw->addChild(s);
	return s;
}

} // namespace follow
