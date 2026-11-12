// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP (was GENRA HIP-HOP): the module. The engine (hh/Core) makes the patterns; this plays them (clock, swing, the drunk
// timing, rolls), plays the sample's chops and the break's hits, takes the pads from the mouse and MIDI, records them
// with REC, keeps the undo steps, loads and analyses samples in a worker thread, exports MIDI and the chops.
#include "GenraChopModule.hpp"
#include "ChopUI.hpp"
#include "GenraChopLayout.hpp"
#include "MidiLearn.hpp"
#include "hh/Song.hpp"

struct HHPad : widget::OpaqueWidget {
	GenraChop* module = nullptr;
	int slot = 0; // 0..15 on the panel; the chop: bank * 16 + slot
	int idx = 0;
	float flash = 0.f;
	uint32_t seenTick = 0;
	void step() override {
		idx = (module ? module->bank.load() * 16 : 0) + slot;
		if (module && module->lastPad.load() == idx && module->lastPadTick.load() != seenTick) {
			seenTick = module->lastPadTick.load();
			flash = 1.f;
		}
		flash = std::max(0.f, flash - 0.06f);
		OpaqueWidget::step();
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const ChopSet* c = module ? module->uiChops.load() : nullptr;
		bool has = c && idx < c->info.count;
		NVGcolor fill = nvgLerpRGBA(C_PAD, nvgRGB(0xf2, 0xc9, 0x6a), flash * 0.85f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.6f, 0.6f, box.size.x - 1.2f, box.size.y - 1.2f, 3.5f);
		nvgFillColor(vg, fill);
		nvgFill(vg);
		nvgStrokeColor(vg, C_PADEDGE);
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);
		label(vg, uiBold(), box.size.x / 2, 8.f, std::to_string(idx + 1), 7.5f, C_INKDIM);
		if (!module || !has)
			return;
		const PadSet& ps = module->pads[idx];
		Chord ch = padChord(c->info, module->pads, idx);
		char name[16];
		if (ch.valid()) {
			ch.root = (ch.root + ps.pitch + 120) % 12;
			chordName(ch, name, sizeof(name));
		}
		else
			snprintf(name, sizeof(name), "%s", c->info.chop[idx].kind == 2 ? "HIT" : "-");
		labelFit(vg, uiBold(), box.size.x / 2, box.size.y / 2 + 2.f, name, 10.5f, box.size.x - 5.f, ps.chordQ >= 0 ? C_RED : C_INK);
		std::string tags;
		if (ps.lock)
			tags += "L ";
		if (ps.rev)
			tags += "R ";
		if (ps.pitch)
			tags += string::f("%+d", ps.pitch);
		if (!tags.empty())
			labelFit(vg, uiBold(), box.size.x / 2, box.size.y - 6.f, tags, 7.f, box.size.x - 5.f, C_RED);
	}
	void onButton(const ButtonEvent& e) override {
		if (!module)
			return;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			module->queuePad(idx, 0.9f);
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
			menu();
			e.consume(this);
		}
	}
	void menu() {
		GenraChop* m = module;
		int i = idx;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Pad %d", i + 1)));
		menu->addChild(createBoolPtrMenuItem("Lock (stays through NEW and MUTATE)", "", &m->pads[i].lock));
		menu->addChild(createBoolPtrMenuItem("Reverse", "", &m->pads[i].rev));
		menu->addChild(createSubmenuItem("Pitch", string::f("%+d", m->pads[i].pitch), [=](ui::Menu* sub) {
			for (int p = -12; p <= 12; p++)
				sub->addChild(createCheckMenuItem(string::f("%+d", p), "", [=]() { return m->pads[i].pitch == p; },
				                                  [=]() { m->pads[i].pitch = (int8_t) p; }));
		}));
		const ChopSet* c = m->uiChops.load();
		std::string det = "-";
		if (c && i < c->info.count) {
			char nm[16];
			chordName(c->info.chop[i].chord, nm, sizeof(nm));
			det = nm;
		}
		menu->addChild(createSubmenuItem("Chord", m->pads[i].chordQ >= 0 ? "set by hand" : "detected", [=](ui::Menu* sub) {
			sub->addChild(createCheckMenuItem("As detected (" + det + ")", "", [=]() { return m->pads[i].chordQ < 0; }, [=]() {
				m->pads[i].chordQ = -1;
				m->reqRender = true;
			}));
			sub->addChild(createCheckMenuItem("No chord (a hit)", "", [=]() { return m->pads[i].chordQ == 99; }, [=]() {
				m->pads[i].chordQ = 99;
			}));
			for (int r = 0; r < 12; r++)
				sub->addChild(createSubmenuItem(NOTE_NAMES[r], "", [=](ui::Menu* s2) {
					for (int q = 0; q < NUM_QUALITIES; q++) {
						std::string nm = std::string(NOTE_NAMES[r]) + QUALITY_NAMES[q];
						s2->addChild(createCheckMenuItem(nm, "", [=]() { return m->pads[i].chordRoot == r && m->pads[i].chordQ == q; },
						                                 [=]() {
							                                 m->pads[i].chordRoot = (int8_t) r;
							                                 m->pads[i].chordQ = (int8_t) q;
						                                 }));
					}
				}));
		}));
	}
};

struct HHLcd : widget::Widget {
	GenraChop* module = nullptr;
	std::string learnText; // MIDI learn waiting (set by the panel)
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 1.5f);
		nvgFillColor(vg, C_LCD);
		nvgFill(vg);
		auto f = lcdFont();
		std::string l1 = "90s > EAST COAST BOOM BAP", l2 = "92 BPM  F#m  SWING 58%", l3 = "NO SAMPLE - PRESS LOAD";
		if (module) {
			const Style& st = module->style();
			l1 = string::uppercase(string::f("%s > %s", ERA_NAMES[st.era], st.name));
			int sp = module->songPart.load();
			if (sp >= 0 && sp < NUM_PARTS)
				l1 = std::string(PART_NAMES[sp]) + "  " + l1; // the part GENRA CHOP ABLAUF plays
			char kn[16];
			keyName(module->set.key, kn, sizeof(kn));
			int s = module->step.load();
			l2 = string::f("%3.0f BPM  %s%s  SWING %2.0f%%  %s", module->bpmNow(), kn, module->set.keyAuto ? "" : "*",
			               module->params[GenraChop::SWING_PARAM].getValue(),
			               s >= 0 ? string::f("BAR %d.%d", s / STEPS + 1, (s % STEPS) / 4 + 1).c_str() : "STOP");
			const ChopSet* c = module->uiChops.load();
			if (module->loader->busy)
				l3 = "ANALYSING SAMPLE...";
			else if (c)
				l3 = string::f("SMPL %s  %d CHOPS  KEY:%s", string::uppercase(c->data.name).substr(0, 18).c_str(),
				               c->info.count, module->set.keyAuto ? "AUTO" : "MAN");
			else {
				std::lock_guard<std::mutex> lk(module->loader->m);
				if (!module->loader->err.empty())
					l3 = string::uppercase("ERR: " + module->loader->err);
				else if (!module->loader->status.empty())
					l3 = string::uppercase(module->loader->status);
				else
					l3 = "NO SAMPLE - PRESS LOAD";
			}
			if (module->rec)
				l3 = "REC  " + l3;
			if (!learnText.empty())
				l3 = learnText;
		}
		label(vg, f, 6.f, 9.5f, l1, 13.f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 6.f, 22.f, l2, 11.5f, C_LCDINK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		label(vg, f, 6.f, 33.5f, l3, 10.f, nvgRGB(0x3a, 0x4a, 0x2a), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	}
};

struct HHWave : widget::OpaqueWidget {
	GenraChop* module = nullptr;
	void onButton(const ButtonEvent& e) override {
		if (module && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && !module->uiChops.load() &&
		    !module->loader->busy) {
			module->loadSample();
			e.consume(this);
		}
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const ChopSet* c = module ? module->uiChops.load() : nullptr;
		float w = box.size.x, h = box.size.y;
		if (!c || c->peaks.empty()) {
			label(vg, uiBold(), w / 2, h / 2, module && module->loader->busy ? "ANALYSING..." : "CLICK HERE OR PRESS LOAD", 9.f,
			      nvgRGB(0x8f, 0x8a, 0x7c));
			return;
		}
		size_t total = c->data.frames();
		// the chop MOVE works on (last hit by hand): a frame
		int sp = module->selPad.load();
		if (sp >= 0 && sp < c->info.count && total) {
			float x0 = w * c->start[sp] / total, x1 = w * c->end[sp] / total;
			nvgBeginPath(vg);
			nvgRect(vg, x0, 0.5f, std::max(1.f, x1 - x0), h - 1.f);
			nvgStrokeColor(vg, C_YELLOW);
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}
		// the chop playing right now
		int lp = module->lastPad.load();
		if (lp >= 0 && lp < c->info.count && total) {
			float x0 = w * c->start[lp] / total, x1 = w * c->end[lp] / total;
			nvgBeginPath(vg);
			nvgRect(vg, x0, 0, x1 - x0, h);
			nvgFillColor(vg, nvgRGBA(0xa9, 0xb9, 0x8c, 0x30));
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		size_t n = c->peaks.size();
		for (size_t i = 0; i < n; i++) {
			float x = w * i / (n - 1), a = c->peaks[i] * h * 0.48f;
			nvgRect(vg, x, h / 2 - a, std::max(0.6f, w / n), 2 * a);
		}
		nvgFillColor(vg, C_LCD);
		nvgFill(vg);
		// SAMPLE: where the whole sample is playing
		float sp2 = module->samplePos.load();
		if (sp2 >= 0.f && sp2 <= 1.f) {
			nvgBeginPath(vg);
			nvgRect(vg, w * sp2 - 0.6f, 0, 1.2f, h);
			nvgFillColor(vg, C_YELLOW);
			nvgFill(vg);
		}
		for (int p = 0; p < c->info.count; p++) {
			float x = total ? w * c->start[p] / total : 0.f;
			nvgBeginPath(vg);
			nvgRect(vg, x, 0, 0.8f, h);
			nvgFillColor(vg, C_RED);
			nvgFill(vg);
			label(vg, uiBold(), x + 2.f, 5.f, std::to_string(p + 1), 6.5f, nvgRGB(0xd8, 0xd1, 0xbf), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}
};

struct HHBars : widget::Widget {
	GenraChop* module = nullptr;
	void draw(const DrawArgs& args) override {
		int s = module ? module->step.load() : 16;
		for (int i = 0; i < BARS; i++) {
			float w = box.size.x / BARS;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, i * w + 1.5f, 0, w - 3.f, box.size.y, 1.8f);
			nvgFillColor(args.vg, s >= 0 && s / STEPS == i ? C_RED : C_GRAPH);
			nvgFill(args.vg);
		}
	}
};

/** WARP's tempo: the sample's (found, x2 / /2) > the beat's; while working "WARPING". */
struct WarpBpm : widget::Widget {
	GenraChop* module = nullptr;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 1.5f);
		nvgFillColor(vg, C_LCD);
		nvgFill(vg);
		std::string t = "-- BPM";
		if (module) {
			const ChopSet* c = module->uiChops.load();
			float src = c ? (c->source ? c->source->bpm : c->bpm) * module->warpMul : 0.f;
			if (module->loader->busy && module->warpOn)
				t = "WARPING...";
			else if (src > 0.f && module->warpOn)
				t = string::f("%.0f > %.0f BPM", src, module->bpmNow());
			else if (src > 0.f)
				t = string::f("%.1f BPM", src);
		}
		labelFit(vg, lcdFont(), box.size.x / 2, box.size.y / 2 + 0.5f, t, 11.f, box.size.x - 6.f, C_LCDINK);
	}
};

struct GenraChopWidget : ModuleWidget, ml::LearnHost {
	ml::Learner learn;
	HHLcd* lcd = nullptr;
	float takenCheck = 0.f;
	ml::Learner* learner() override { return &learn; }
	float lastStop = -10.f;
	int chopsSeen = -1;
	float chopsRest = 0.f;
	float warpSeen = -1.f, warpRest = 0.f;
	std::string note; // last export message
	float noteTime = 0.f;

	template <typename T>
	T* at(const float* r) {
		T* w = new T();
		w->box = rectOf(r);
		addChild(w);
		return w;
	}
	HHButton* button(const float* r, const char* text, NVGcolor col, std::function<void()> act) {
		HHButton* b = at<HHButton>(r);
		b->text = text;
		b->col = col;
		b->act = act;
		return b;
	}
	/** Registers a control for MIDI learn: its right-click menu, its ring while learning. */
	void learnButton(HHButton* b, const std::string& id, const std::string& lab) {
		ml::Target t;
		t.id = id;
		t.label = lab;
		t.btn = b;
		learn.targets.push_back(t);
		b->rightMenu = [=](ui::Menu* m) {
			m->addChild(createMenuLabel(lab));
			learn.appendTargetMenu(m, id);
		};
		b->learning = [=]() { return learn.learning == id; };
	}
	void learnField(HHField* f, const std::string& id, const std::string& lab) {
		ml::Target t;
		t.id = id;
		t.label = lab;
		t.field = f;
		learn.targets.push_back(t);
		f->rightMenu = [=](ui::Menu* m) {
			m->addChild(createMenuLabel(lab));
			learn.appendTargetMenu(m, id);
		};
		f->learning = [=]() { return learn.learning == id; };
	}
	template <class K>
	void knob(const float* p, GenraChop* m, int id, const std::string& lab, float fine = 1.f) {
		addParam(createParamCentered<ml::LearnKnob<K>>(Vec(p[0], p[1]), m, id));
		ml::Target t;
		t.id = string::f("p%d", id);
		t.label = lab;
		t.param = id;
		t.fine = fine;
		learn.targets.push_back(t);
	}

	GenraChopWidget(GenraChop* m) {
		setModule(m);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/GenraChop.svg")));
		using M = GenraChop;
		learn.kind = "chop";
		learn.module = m;
		learn.ins = m ? m->ctrl : nullptr;
		if (m)
			learn.restorePorts();
		// header
		HHButton* stop = button(hhl::STOP, "STOP", C_GRAPH, [=]() {
			if (!m)
				return;
			float now = (float) system::getTime();
			if (!m->running && now - lastStop < 0.5f)
				m->reqKill = true; // twice: cut the chops and the break too
			m->running = false;
			lastStop = now;
		});
		stop->symbol = 2;
		learnButton(stop, "stop", "STOP");
		HHButton* play = button(hhl::PLAY, "PLAY", C_RED, [=]() {
			if (m)
				m->running = true;
		});
		play->symbol = 1;
		play->lit = [=]() { return m && m->running; };
		learnButton(play, "play", "PLAY");
		HHButton* smp = button(hhl::SPLAY, "SAMPLE", C_YELLOW, [=]() {
			if (m)
				m->reqSample = m->samplePlaying ? 2 : 1;
		});
		smp->textCol = C_YELLOWINK;
		smp->lit = [=]() { return m && m->samplePlaying; };
		learnButton(smp, "sample", "SAMPLE");
		knob<HHKnobSmall>(hhl::BPM, m, M::BPM_PARAM, "BPM");
		knob<HHKnobSmall>(hhl::SWING, m, M::SWING_PARAM, "SWING");
		knob<HHKnobSmall>(hhl::VOL, m, M::VOL_PARAM, "CHOP level");
		knob<HHKnobSmall>(hhl::BRK, m, M::BRK_PARAM, "BREAK level");
		// LCD, fields
		lcd = at<HHLcd>(hhl::LCD);
		lcd->module = m;
		HHField* era = at<HHField>(hhl::ERA);
		era->value = [=]() { return std::string("ERA  ") + ERA_NAMES[m ? m->style().era : 1]; };
		era->count = []() { return (int) NUM_ERAS; };
		era->current = [=]() { return m ? m->style().era : 1; };
		era->item = [](int i) { return std::string(ERA_NAMES[i]); };
		era->choose = [=](int i) {
			if (!m || i == m->style().era)
				return;
			int list[NUM_STYLES];
			stylesInEra(i, list, NUM_STYLES);
			chooseStyle(m, list[0]);
		};
		learnField(era, "era", "ERA");
		HHField* sty = at<HHField>(hhl::STYLE);
		sty->value = [=]() { return std::string("STYLE  ") + (m ? m->style().name : "East Coast Boom Bap"); };
		sty->count = [=]() { return m ? stylesInEra(m->style().era, nullptr, 0) : 0; };
		sty->current = [=]() { return m ? styleIndexInEra(m->set.style) : 0; };
		sty->item = [=](int i) {
			int list[NUM_STYLES];
			stylesInEra(m->style().era, list, NUM_STYLES);
			return std::string(STYLES[list[i]].name);
		};
		sty->choose = [=](int i) {
			int list[NUM_STYLES];
			stylesInEra(m->style().era, list, NUM_STYLES);
			chooseStyle(m, list[i]);
		};
		learnField(sty, "style", "STYLE");
		HHField* key = at<HHField>(hhl::KEY);
		key->value = [=]() {
			if (!m)
				return std::string("KEY  Auto");
			char kn[16];
			keyName(m->set.key, kn, sizeof(kn));
			return std::string("KEY  ") + (m->set.keyAuto ? "Auto " : "") + kn;
		};
		key->count = []() { return 25; };
		key->current = [=]() { return !m || m->set.keyAuto ? 0 : 1 + m->set.key.root * 2 + (m->set.key.minor ? 1 : 0); };
		key->item = [](int i) {
			if (i == 0)
				return std::string("Auto (from the sample)");
			Key k;
			k.root = (i - 1) / 2;
			k.minor = (i - 1) % 2;
			char kn[16];
			keyName(k, kn, sizeof(kn));
			return std::string(kn) + (k.minor ? " minor" : " major");
		};
		key->choose = [=](int i) {
			if (!m)
				return;
			if (i == 0) {
				m->set.keyAuto = true;
				if (const ChopSet* c = m->uiChops.load())
					m->set.key = c->info.key;
			}
			else {
				m->set.keyAuto = false;
				m->set.key.root = (i - 1) / 2;
				m->set.key.minor = (i - 1) % 2;
			}
			m->reqNew.fetch_or((1 << T_BASS) | (1 << T_MEL) | (1 << T_PAD));
		};
		learnField(key, "key", "KEY");
		// the pads
		for (int i = 0; i < 16; i++) {
			const float* r = i == 0 ? hhl::PAD0 : i == 1 ? hhl::PAD1 : i == 2 ? hhl::PAD2 : i == 3 ? hhl::PAD3 : i == 4 ? hhl::PAD4
			                 : i == 5 ? hhl::PAD5 : i == 6 ? hhl::PAD6 : i == 7 ? hhl::PAD7 : i == 8 ? hhl::PAD8 : i == 9 ? hhl::PAD9
			                 : i == 10 ? hhl::PAD10 : i == 11 ? hhl::PAD11 : i == 12 ? hhl::PAD12 : i == 13 ? hhl::PAD13
			                 : i == 14 ? hhl::PAD14 : hhl::PAD15;
			HHPad* p = at<HHPad>(r);
			p->module = m;
			p->slot = (3 - i / 4) * 4 + i % 4; // as on an MPC (and the MPD218): pad 1 bottom left
			p->idx = p->slot;
		}
		// the pad banks A-D: 64 chops at most
		const float* BK[4] = {hhl::BANK0, hhl::BANK1, hhl::BANK2, hhl::BANK3};
		for (int i = 0; i < 4; i++) {
			HHButton* b = button(BK[i], i == 0 ? "A" : i == 1 ? "B" : i == 2 ? "C" : "D", C_GRAPH, [=]() {
				if (m)
					m->bank = i;
			});
			b->fontSize = 8.f;
			b->colOf = [=]() { return m && m->bank.load() == i ? C_RED : C_GRAPH; };
			// a bank without chops (the sample has fewer) stays dim
			b->textColOf = [=]() {
				const ChopSet* c = m ? m->uiChops.load() : nullptr;
				return (c && c->info.count <= i * 16) ? nvgRGB(0x77, 0x73, 0x6a) : C_CREAM;
			};
			learnButton(b, string::f("bank%d", i), string::f("PAD BANK %c", 'A' + i));
		}
		// CHOP
		HHField* mode = at<HHField>(hhl::MODE);
		mode->value = [=]() { return std::string(CHOP_MODES[m ? m->chopMode : 0]); };
		mode->count = []() { return (int) NUM_CHOP_MODES; };
		mode->current = [=]() { return m ? m->chopMode : 0; };
		mode->item = [](int i) { return std::string(CHOP_MODES[i]); };
		mode->choose = [=](int i) {
			if (m)
				m->setChopMode(i);
		};
		learnField(mode, "chopmode", "CHOP mode");
		knob<HHKnob>(hhl::MUTATE, m, M::MUTATE_PARAM, "MUTATE");
		knob<HHKnob>(hhl::PITCH, m, M::PITCH_PARAM, "PITCH");
		knob<HHKnob>(hhl::CHOPS, m, M::CHOPS_PARAM, "CHOPS");
		knob<HHKnob>(hhl::MOVE, m, M::MOVE_PARAM, "MOVE", 0.25f);
		// LOAD SAMPLE: big and yellow, right under the waveform (used often)
		HHButton* load = button(hhl::LOAD, "LOAD SAMPLE", C_YELLOW, [=]() {
			if (m)
				m->loadSample();
		});
		load->textCol = C_YELLOWINK;
		learnButton(load, "load", "LOAD SAMPLE");
		HHButton* rec = button(hhl::REC, "REC", C_RED, [=]() {
			if (m)
				m->rec = !m->rec;
		});
		rec->lit = [=]() { return m && m->rec; };
		learnButton(rec, "rec", "REC");
		HHButton* lock = button(hhl::LOCK, "LOCK", C_GRAPH, [=]() {
			if (m)
				m->lockChords = !m->lockChords;
		});
		lock->lit = [=]() { return m && m->lockChords; };
		learnButton(lock, "lock", "LOCK");
		HHButton* root = button(hhl::ROOT, "ROOT", C_GRAPH, [=]() {
			if (m)
				m->rootHeld = true;
		});
		root->release = [=]() {
			if (m)
				m->rootHeld = false;
		};
		learnButton(root, "root", "ROOT");
		learnButton(button(hhl::UNDO, "UNDO", C_GRAPH, [=]() {
			if (m)
				m->reqUndo = true;
		}), "undo", "UNDO");
		learnButton(button(hhl::SAVE, "SAVE", C_GRAPH, [=]() {
			if (m)
				say(m->saveChops());
		}), "save", "SAVE");
		// CLEAR: alone the whole chop pattern; held while pads are hit, only those pads' hits
		HHButton* clr = button(hhl::CLEAR, "CLEAR", C_GRAPH, [=]() {
			if (!m)
				return;
			m->clearUsed = false;
			m->clearHeld = true;
		});
		clr->release = [=]() {
			if (!m)
				return;
			m->clearHeld = false;
			if (!m->clearUsed)
				m->reqClearAll = true;
		};
		clr->lit = [=]() { return m && m->clearHeld; };
		learnButton(clr, "clear", "CLEAR");
		learnButton(button(hhl::NEWALL, "NEW ALL", C_RED, [=]() {
			if (!m)
				return;
			m->freshCut(); // the sample cut fresh (CHOPS 16, MOVE 0), as if just loaded
			m->reqNew.fetch_or(1 << 5);
		}), "newall", "NEW ALL");
		// the waveform and the tracks
		HHWave* wave = at<HHWave>(hhl::WAVE);
		wave->module = m;
		// WARP: the strip under the waveform, the stretcher top right in it
		HHButton* warp = button(hhl::WARP, "WARP", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (!m)
				return;
			m->warpOn = !m->warpOn;
			m->rewarp();
		});
		warp->colOf = [=]() { return m && m->warpOn ? C_RED : nvgRGB(0x55, 0x53, 0x4f); };
		learnButton(warp, "warp", "WARP");
		HHField* wmode = at<HHField>(hhl::WMODE);
		wmode->count = []() { return (int) NUM_WARP_MODES; };
		wmode->current = [=]() { return m ? m->warpMode : 0; };
		wmode->item = [](int i) {
			const char* T[NUM_WARP_MODES] = {"AUTO (BEATS or TONE, from the sample)", "BEATS (the chops on the grid)",
			                                 "TONE (stretched, for loops)"};
			return std::string(T[i]);
		};
		wmode->value = [=]() {
			if (!m)
				return std::string("AUTO");
			std::string v = WARP_MODES[m->warpMode];
			const ChopSet* c = m->uiChops.load();
			if (m->warpMode == WARP_AUTO && c && c->warpHow >= 0)
				v += c->warpHow == WARP_BEATS ? " B" : " T";
			return v;
		};
		wmode->choose = [=](int i) {
			if (!m || i == m->warpMode)
				return;
			m->warpMode = i;
			if (m->warpOn)
				m->rewarp();
		};
		learnField(wmode, "warpmode", "WARP mode");
		WarpBpm* wb = at<WarpBpm>(hhl::WBPM);
		wb->module = m;
		HHButton* x2 = button(hhl::WX2, "x2", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (!m)
				return;
			m->warpMul = std::min(4.f, m->warpMul * 2.f);
			if (m->warpOn)
				m->rewarp();
		});
		learnButton(x2, "warpx2", "WARP x2");
		HHButton* d2 = button(hhl::WD2, "/2", nvgRGB(0x55, 0x53, 0x4f), [=]() {
			if (!m)
				return;
			m->warpMul = std::max(0.25f, m->warpMul * 0.5f);
			if (m->warpOn)
				m->rewarp();
		});
		learnButton(d2, "warpd2", "WARP /2");
		for (int e = 0; e < hhw::NUM_ENGINES; e++) {
			float r[4] = {hhl::WALGO[0] + e * hhl::WALGO[2] / 2.f + (e ? 0.6f : 0.f), hhl::WALGO[1], hhl::WALGO[2] / 2.f - 0.6f,
			              hhl::WALGO[3]};
			HHButton* ab = new HHButton();
			ab->box = rectOf(r);
			addChild(ab);
			ab->text = hhw::ENGINE_NAMES[e];
			ab->fontSize = 7.f;
			ab->act = [=]() {
				if (!m || m->warpEngine == e)
					return;
				m->warpEngine = e;
				if (m->warpOn)
					m->rewarp();
			};
			ab->colOf = [=]() { return m && m->warpEngine == e ? C_YELLOW : nvgRGB(0x55, 0x53, 0x4f); };
			ab->textColOf = [=]() { return m && m->warpEngine == e ? C_YELLOWINK : C_CREAM; };
			learnButton(ab, string::f("stretch%d", e), string::f("STRETCHER %s", hhw::ENGINE_NAMES[e]));
		}
		const float* TM[4] = {hhl::TMODE0, hhl::TMODE1, hhl::TMODE2, hhl::TMODE3};
		const float* DN[4] = {hhl::DENS0, hhl::DENS1, hhl::DENS2, hhl::DENS3};
		const float* K2[4] = {hhl::TK2_0, hhl::TK2_1, hhl::TK2_2, hhl::TK2_3};
		const float* NW[4] = {hhl::TNEW0, hhl::TNEW1, hhl::TNEW2, hhl::TNEW3};
		const float* CL[4] = {hhl::TCLR0, hhl::TCLR1, hhl::TCLR2, hhl::TCLR3};
		for (int t = 0; t < 4; t++) {
			HHField* f = at<HHField>(TM[t]);
			f->count = [t]() { return t == 0 ? 3 : t == 1 ? 4 : t == 2 ? 4 : 3; };
			f->item = [t](int i) {
				return std::string(t == 0 ? DRUM_MODES[i] : t == 1 ? BASS_MODES[i] : t == 2 ? MEL_MODES[i] : PAD_MODES[i]);
			};
			f->current = [=]() {
				if (!m)
					return 0;
				return t == 0 ? m->set.drumMode : t == 1 ? m->set.bassMode : t == 2 ? m->set.melMode : m->set.padMode;
			};
			f->value = [=]() { return f->item(f->current()); };
			f->choose = [=](int i) {
				if (!m)
					return;
				(t == 0 ? m->set.drumMode : t == 1 ? m->set.bassMode : t == 2 ? m->set.melMode : m->set.padMode) = i;
				if (t > 0)
					m->reqNew.fetch_or(1 << t);
			};
			const char* TN[4] = {"DRUMS", "BASS", "MELODY", "PADS"};
			learnField(f, string::f("mode%d", t), string::f("%s mode", TN[t]));
			knob<HHKnob>(DN[t], m, M::DENS_PARAM + t, string::f("%s DENS", TN[t]));
			knob<HHKnob>(K2[t], m, M::TK2_PARAM + t, string::f("%s %s", TN[t], t == 1 ? "OCT" : "CHOP"));
			learnButton(button(NW[t], "NEW", C_RED, [=]() {
				if (m)
					m->reqNew.fetch_or(1 << t);
			}), string::f("new%d", t), string::f("%s NEW", TN[t]));
			HHButton* clrT = button(CL[t], "CLEAR", C_GRAPH, [=]() {
				if (m)
					m->reqClearTrack.fetch_or(1 << t);
			});
			clrT->fontSize = 8.f;
			learnButton(clrT, string::f("clear%d", t), string::f("%s CLEAR", TN[t]));
		}
		HHBars* bars = at<HHBars>(hhl::BARS);
		bars->module = m;
		// jacks
		auto in = [&](const float* p, int id) { addInput(createInputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		auto out = [&](const float* p, int id) { addOutput(createOutputCentered<PJ301MPort>(Vec(p[0], p[1]), m, id)); };
		in(hhl::CLK_IN, M::CLK_INPUT);
		in(hhl::RST_IN, M::RST_INPUT);
		in(hhl::RUN_IN, M::RUN_INPUT);
		in(hhl::NEW_IN, M::NEW_INPUT);
		const float* DR[8] = {hhl::DRUM0, hhl::DRUM1, hhl::DRUM2, hhl::DRUM3, hhl::DRUM4, hhl::DRUM5, hhl::DRUM6, hhl::DRUM7};
		for (int i = 0; i < 8; i++)
			out(DR[i], M::DRUM_OUTPUT + i);
		const float* BA[5] = {hhl::BASS0, hhl::BASS1, hhl::BASS2, hhl::BASS3, hhl::BASS4};
		const float* ME[5] = {hhl::MEL0, hhl::MEL1, hhl::MEL2, hhl::MEL3, hhl::MEL4};
		for (int i = 0; i < 5; i++) {
			out(BA[i], M::BASS_OUTPUT + i);
			out(ME[i], M::MEL_OUTPUT + i);
		}
		const float* PO[4] = {hhl::PADO0, hhl::PADO1, hhl::PADO2, hhl::PADO3};
		for (int i = 0; i < 4; i++)
			out(PO[i], M::PAD_OUTPUT + i);
		out(hhl::CHOP_L, M::CHOP_L_OUTPUT);
		out(hhl::CHOP_R, M::CHOP_R_OUTPUT);
		out(hhl::BRK_L, M::BRK_L_OUTPUT);
		out(hhl::BRK_R, M::BRK_R_OUTPUT);
		out(hhl::CLK_OUT, M::CLK_OUTPUT);
		out(hhl::RST_OUT, M::RST_OUTPUT);
		out(hhl::BAR_OUT, M::BAR_OUTPUT);
	}

	static void chooseStyle(GenraChop* m, int s) {
		if (!m)
			return;
		m->set.style = s;
		m->applyStyleDefaults();
		m->params[GenraChop::BPM_PARAM].setValue(STYLES[s].bpm);
		m->params[GenraChop::SWING_PARAM].setValue(STYLES[s].swing * 100.f);
		m->reqStyle = true;
	}

	void say(const std::string& s) {
		note = s;
		noteTime = 3.f;
	}

	void onPathDrop(const PathDropEvent& e) override {
		GenraChop* m = dynamic_cast<GenraChop*>(module);
		if (m && !e.paths.empty()) {
			m->loadPath(e.paths[0]);
			e.consume(this);
		}
	}

	void step() override {
		if (GenraChop* m = dynamic_cast<GenraChop*>(module)) {
			m->collectTrash();
			learn.step();
			if (m->warpPending && !m->loader->busy)
				m->rewarp();
			// NEW at the jack: the sample cut fresh, as with the button
			if (m->freshReq.exchange(false))
				m->freshCut();
			// WARP follows the tempo: when it rests a moment on a new one, the sample is warped again
			if (m->warpOn && m->uiChops.load()) {
				float now = m->bpmNow();
				if (std::fabs(now - m->warpFor) > 0.05f) {
					if (std::fabs(now - warpSeen) > 0.01f) {
						warpSeen = now;
						warpRest = 0.4f;
					}
					else if (warpRest > 0.f) {
						warpRest -= APP->window->getLastFrameDuration();
						if (warpRest <= 0.f && !m->loader->busy)
							m->rewarp();
						else if (warpRest <= 0.f)
							warpRest = 0.1f;
					}
				}
			}
			// CHOPS: when it rests a moment on a new number, the sample is cut anew
			int want = m->chopsWanted();
			if (want != chopsSeen) {
				chopsSeen = want;
				chopsRest = 0.35f;
			}
			if (chopsRest > 0.f) {
				chopsRest -= APP->window->getLastFrameDuration();
				if (chopsRest <= 0.f && want != m->cutCount && m->loader->busy)
					chopsRest = 0.1f; // still cutting: try again a little later
				else if (chopsRest <= 0.f)
					m->setChopCount(want);
			}
			// notes learned as controls: the pad input leaves them
			takenCheck -= APP->window->getLastFrameDuration();
			if (takenCheck <= 0.f) {
				takenCheck = 0.5f;
				uint32_t t[4] = {0, 0, 0, 0};
				for (auto& kv : ml::Store::get().bindings) // also notes learned for GENRA CHOP ABLAUF
					for (const ml::Binding& b : kv.second)
						if (b.kind == ml::K_NOTE)
							t[b.num >> 5] |= 1u << (b.num & 31);
				for (int i = 0; i < 4; i++)
					m->takenNotes[i] = t[i];
			}
		}
		if (lcd)
			lcd->learnText = learn.learning.empty() ? "" : "MIDI LEARN " + string::uppercase(learn.labelOf(learn.learning)) + ": MOVE A CONTROL";
		if (noteTime > 0.f)
			noteTime -= APP->window->getLastFrameDuration();
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		ModuleWidget::draw(args);
		if (noteTime > 0.f && !note.empty()) {
			// a short message over the waveform (export done / failed)
			float x = hhl::WAVE[0], y = hhl::WAVE[1] + hhl::WAVE[3] - 12.f, w = hhl::WAVE[2];
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, y, w, 12.f);
			nvgFillColor(args.vg, nvgRGBA(0x2a, 0x29, 0x28, 0xe0));
			nvgFill(args.vg);
			label(args.vg, uiBold(), x + 4.f, y + 6.f, note, 8.f, C_CREAM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}

	void appendContextMenu(Menu* menu) override {
		GenraChop* m = dynamic_cast<GenraChop*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Sample"));
		menu->addChild(createMenuItem("Load sample…", "", [=]() { m->loadSample(); }));
		std::string root = hhx::sampleFolder();
		menu->addChild(createMenuItem("Sample root folder…", root.empty() ? "not set" : system::getFilename(root),
		                              []() { hhx::pickSampleFolder(); }));
		menu->addChild(createMenuItem("Load break (drums)…", "", [=]() { m->loadBreak(); }));
		menu->addChild(createMenuItem("Clear break", "", [=]() {
			m->breakPath.clear();
			m->set.drumMode = 0;
		}));
		menu->addChild(createBoolPtrMenuItem("SAVE writes the chops with their pitch and direction", "", &m->bakeChops));
		menu->addChild(createMenuItem("Chop folder…", "", []() { hhx::pickChopFolder(); }));
		menu->addChild(createMenuItem("Open chop folder", "", []() {
			system::createDirectories(hhx::chopFolder());
			system::openDirectory(hhx::chopFolder());
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI"));
		menu->addChild(createMenuItem("Export MIDI (4 bars, 5 tracks)", "", [=]() { say(m->exportMidi()); }));
		menu->addChild(createMenuItem("MIDI export folder…", "", []() { hhx::pickMidiFolder(); }));
		menu->addChild(createMenuItem("Open MIDI export folder", "", []() {
			system::createDirectories(hhx::midiFolder());
			system::openDirectory(hhx::midiFolder());
		}));
		menu->addChild(createSubmenuItem("MIDI input (plays the pads, C1 = pad 1)", "", [=](Menu* sub) {
			app::appendMidiMenu(sub, &m->midiIn);
		}));
		learn.appendModuleMenu(menu, [=](const std::string& s) { say(s); });
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input", {"16ths", "24 PPQN", "48 PPQN"},
		                                      [=]() { return m->clockDiv == 6 ? 1 : m->clockDiv == 12 ? 2 : 0; },
		                                      [=](int i) { m->clockDiv = i == 1 ? 6 : i == 2 ? 12 : 1; }));
		menu->addChild(createMenuItem("Undo steps", string::f("%d", m->undoCount.load()), []() {}));
	}
};

Model* modelGenraChop = createModel<GenraChop, GenraChopWidget>("GenraChop");
