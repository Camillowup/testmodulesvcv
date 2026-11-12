// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: MIDI learn for every control (knobs, buttons, fields) of GENRA CHOP and GENRA CHOP ABLAUF.
//
// Up to three controllers per module (each its own MIDI input). A mapping belongs to a controller (by its device name)
// and is kept per computer, not per patch: <Rack user folder>/Genra/chop-midi-map.json, for every patch. The chosen
// controllers are kept there too. One target is driven by one control only (learning it again moves it).
// Knobs: CC absolute (0..127 over the range) or relative (endless encoders: 1 / 127 or 63 / 65), found out on its own
// (a value that comes twice in a row can only be an encoder) or set by hand. Buttons: note on / off or CC >= 64 = press.
// Fields: CC over the list (absolute) or one step per tick (relative), a note steps on.
// Everything runs in the UI thread (the widget's step): buttons and fields act like a mouse click.
#pragma once
#include "ChopUI.hpp"
#include "Export.hpp"
#include <osdialog.h>
#include <algorithm>
#include <functional>
#include <mutex>
#include <map>
#include <vector>

namespace ml {

/** A MIDI input that keeps what comes in for the UI thread. */
struct CtrlIn : midi::Input {
	std::mutex m;
	std::vector<midi::Message> q;
	void onMessage(const midi::Message& msg) override {
		std::lock_guard<std::mutex> lk(m);
		if (q.size() < 1024)
			q.push_back(msg);
	}
	void take(std::vector<midi::Message>& out) {
		out.clear();
		std::lock_guard<std::mutex> lk(m);
		out.swap(q);
	}
	std::string name() {
		int id = getDeviceId();
		return id >= 0 ? getDeviceName(id) : std::string();
	}
};
static const int CTRLS = 3;

enum Kind { K_CC, K_NOTE };
enum Mode { M_AUTO, M_ABS, M_REL };
struct Binding {
	std::string device, target;
	int kind = K_CC, ch = 0, num = 0;
	int mode = M_AUTO;
	int relType = 0; // 0: 1 / 127 (two's complement), 1: 63 / 65 (around 64)
	int last = -1;   // the last value (finding encoders), not saved
};

// ---------------------------------------------------------------- the file (shared by every module of the kind)
struct Store {
	std::map<std::string, std::vector<Binding>> bindings; // per module kind ("chop", "ablauf")
	std::map<std::string, std::vector<std::string>> inputs; // per kind: the controllers' port JSON (dumped)
	bool loaded = false;
	static std::string path() { return asset::user("Genra/chop-midi-map.json"); }
	static Store& get() {
		static Store s;
		if (!s.loaded)
			s.load();
		return s;
	}
	static json_t* bindingToJson(const Binding& b) {
		json_t* o = json_object();
		json_object_set_new(o, "device", json_string(b.device.c_str()));
		json_object_set_new(o, "target", json_string(b.target.c_str()));
		json_object_set_new(o, "kind", json_integer(b.kind));
		json_object_set_new(o, "ch", json_integer(b.ch));
		json_object_set_new(o, "num", json_integer(b.num));
		json_object_set_new(o, "mode", json_integer(b.mode));
		json_object_set_new(o, "relType", json_integer(b.relType));
		return o;
	}
	static bool bindingFromJson(json_t* o, Binding& b) {
		json_t* d = json_object_get(o, "device");
		json_t* t = json_object_get(o, "target");
		if (!d || !t || !json_is_string(d) || !json_is_string(t))
			return false;
		b.device = json_string_value(d);
		b.target = json_string_value(t);
		auto gi = [&](const char* k, int def) {
			json_t* j = json_object_get(o, k);
			return j ? (int) json_integer_value(j) : def;
		};
		b.kind = clamp(gi("kind", K_CC), 0, 1);
		b.ch = clamp(gi("ch", 0), 0, 15);
		b.num = clamp(gi("num", 0), 0, 127);
		b.mode = clamp(gi("mode", M_AUTO), 0, 2);
		b.relType = clamp(gi("relType", 0), 0, 1);
		return true;
	}
	static json_t* listToJson(const std::vector<Binding>& v) {
		json_t* a = json_array();
		for (const Binding& b : v)
			json_array_append_new(a, bindingToJson(b));
		return a;
	}
	static void listFromJson(json_t* a, std::vector<Binding>& v) {
		v.clear();
		for (size_t i = 0; i < json_array_size(a); i++) {
			Binding b;
			if (bindingFromJson(json_array_get(a, i), b))
				v.push_back(b);
		}
	}
	void load() {
		loaded = true;
		FILE* f = std::fopen(path().c_str(), "r");
		if (!f)
			return;
		json_error_t err;
		json_t* r = json_loadf(f, 0, &err);
		std::fclose(f);
		if (!r)
			return;
		json_t* mods = json_object_get(r, "modules");
		const char* key;
		json_t* val;
		json_object_foreach(mods, key, val) {
			listFromJson(json_object_get(val, "bindings"), bindings[key]);
			std::vector<std::string>& in = inputs[key];
			in.clear();
			json_t* ji = json_object_get(val, "inputs");
			for (size_t i = 0; i < json_array_size(ji); i++) {
				char* s = json_dumps(json_array_get(ji, i), JSON_COMPACT);
				in.push_back(s ? s : "");
				std::free(s);
			}
		}
		json_decref(r);
	}
	void save() {
		json_t* r = json_object();
		json_object_set_new(r, "version", json_integer(1));
		json_t* mods = json_object();
		std::vector<std::string> kinds;
		for (auto& kv : bindings)
			kinds.push_back(kv.first);
		for (auto& kv : inputs)
			if (!bindings.count(kv.first))
				kinds.push_back(kv.first);
		for (const std::string& k : kinds) {
			json_t* o = json_object();
			json_object_set_new(o, "bindings", listToJson(bindings[k]));
			json_t* ji = json_array();
			for (const std::string& s : inputs[k]) {
				json_error_t e;
				json_t* p = json_loads(s.c_str(), 0, &e);
				json_array_append_new(ji, p ? p : json_null());
			}
			json_object_set_new(o, "inputs", ji);
			json_object_set_new(mods, k.c_str(), o);
		}
		json_object_set_new(r, "modules", mods);
		system::createDirectories(system::getDirectory(path()));
		FILE* f = std::fopen(path().c_str(), "w");
		if (f) {
			json_dumpf(r, f, JSON_INDENT(2));
			std::fclose(f);
		}
		json_decref(r);
	}
};

// ---------------------------------------------------------------- what can be driven
struct Target {
	std::string id, label;
	int param = -1;           // a knob
	HHButton* btn = nullptr;  // a button
	HHField* field = nullptr; // a list field
	float fine = 1.f;         // relative steps: 1 = 1/200 of the range per tick (MOVE: finer)
};

/** One per module widget: knows the module's targets, learns, and plays what the controllers send. */
struct Learner {
	std::string kind; // "chop", "ablauf"
	Module* module = nullptr;
	CtrlIn* ins = nullptr; // CTRLS of them, owned by the module
	std::vector<Target> targets;
	std::string learning; // the target waiting for a control ("": none)
	std::string lastPorts;
	float portCheck = 0.f;
	std::vector<midi::Message> buf;

	std::vector<Binding>& list() { return Store::get().bindings[kind]; }

	/** Called once the module exists: the computer's controllers for this kind of module. */
	void restorePorts() {
		if (!ins)
			return;
		std::vector<std::string>& in = Store::get().inputs[kind];
		for (int i = 0; i < CTRLS && i < (int) in.size(); i++) {
			json_error_t e;
			json_t* p = json_loads(in[i].c_str(), 0, &e);
			if (p && json_is_object(p))
				ins[i].fromJson(p);
			if (p)
				json_decref(p);
		}
		lastPorts = dumpPorts();
	}
	std::string dumpPorts() {
		std::string all;
		for (int i = 0; i < CTRLS; i++) {
			json_t* p = ins[i].toJson();
			char* s = json_dumps(p, JSON_COMPACT | JSON_SORT_KEYS);
			all += s ? s : "";
			all += "\n";
			std::free(s);
			json_decref(p);
		}
		return all;
	}
	void savePorts() {
		std::vector<std::string>& in = Store::get().inputs[kind];
		in.clear();
		for (int i = 0; i < CTRLS; i++) {
			json_t* p = ins[i].toJson();
			char* s = json_dumps(p, JSON_COMPACT);
			in.push_back(s ? s : "");
			std::free(s);
			json_decref(p);
		}
		Store::get().save();
	}

	const Target* find(const std::string& id) const {
		for (const Target& t : targets)
			if (t.id == id)
				return &t;
		return nullptr;
	}
	std::string labelOf(const std::string& id) const {
		const Target* t = find(id);
		return t ? t->label : id;
	}
	const Binding* bindingOf(const std::string& id) {
		for (const Binding& b : list())
			if (b.target == id)
				return &b;
		return nullptr;
	}
	static std::string describe(const Binding& b) {
		std::string s = b.kind == K_CC ? string::f("CC %d", b.num) : string::f("Note %d", b.num);
		s += string::f(" ch %d", b.ch + 1);
		if (b.kind == K_CC)
			s += b.mode == M_REL ? " (encoder)" : b.mode == M_ABS ? " (absolute)" : " (auto)";
		return s;
	}
	void forget(const std::string& id) {
		std::vector<Binding>& v = list();
		v.erase(std::remove_if(v.begin(), v.end(), [&](const Binding& b) { return b.target == id; }), v.end());
		Store::get().save();
	}
	/** Is this note learned as a control (then the pad input leaves it)? */
	bool noteTaken(int ch, int note) {
		for (const Binding& b : list())
			if (b.kind == K_NOTE && b.num == note && b.ch == ch)
				return true;
		return false;
	}

	void step() {
		if (!ins || !module)
			return;
		portCheck += APP->window->getLastFrameDuration();
		if (portCheck > 0.5f) {
			portCheck = 0.f;
			std::string p = dumpPorts();
			if (p != lastPorts) {
				lastPorts = p;
				savePorts();
			}
		}
		for (int i = 0; i < CTRLS; i++) {
			ins[i].take(buf);
			if (buf.empty())
				continue;
			std::string dev = ins[i].name();
			for (const midi::Message& msg : buf)
				handle(dev, msg);
		}
	}

	void handle(const std::string& dev, const midi::Message& msg) {
		int st = msg.getStatus(), ch = msg.getChannel(), num = msg.getNote() & 0x7f, val = msg.getValue() & 0x7f;
		int kind;
		bool on = true;
		if (st == 0xb)
			kind = K_CC;
		else if (st == 0x9 || st == 0x8) {
			kind = K_NOTE;
			on = st == 0x9 && val > 0;
		}
		else
			return;
		if (!learning.empty()) {
			if (kind == K_NOTE && !on)
				return;
			std::vector<Binding>& v = list();
			// one target, one control - and one control drives one target
			v.erase(std::remove_if(v.begin(), v.end(),
			                       [&](const Binding& b) {
				                       return b.target == learning ||
				                              (b.device == dev && b.kind == kind && b.ch == ch && b.num == num);
			                       }),
			        v.end());
			Binding b;
			b.device = dev;
			b.target = learning;
			b.kind = kind;
			b.ch = ch;
			b.num = num;
			b.last = val;
			v.push_back(b);
			Store::get().save();
			learning.clear();
			return;
		}
		for (Binding& b : list()) {
			if (b.device != dev || b.kind != kind || b.ch != ch || b.num != num)
				continue;
			const Target* t = find(b.target);
			if (!t)
				continue;
			int delta = 0;
			bool rel = false;
			if (kind == K_CC) {
				if (b.mode == M_AUTO && b.last >= 0 && val == b.last && val != 0 && val != 127 && val != 64) {
					// the same value twice: an endless encoder
					b.mode = M_REL;
					b.relType = (val >= 56 && val <= 72) ? 1 : 0;
					Store::get().save();
				}
				else if (b.mode == M_AUTO && b.last >= 0 && val == b.last && (val == 1 || val == 127)) {
					b.mode = M_REL;
					b.relType = 0;
					Store::get().save();
				}
				b.last = val;
				rel = b.mode == M_REL;
				if (rel)
					delta = b.relType == 1 ? val - 64 : (val < 64 ? val : val - 128);
			}
			apply(*t, kind, on, val, rel, delta);
		}
	}

	void apply(const Target& t, int kind, bool on, int val, bool rel, int delta) {
		if (t.param >= 0) {
			ParamQuantity* pq = module->paramQuantities[t.param];
			if (!pq)
				return;
			if (kind == K_NOTE) {
				if (on)
					pq->reset(); // a pad on a knob: back to its default
				return;
			}
			if (!rel)
				pq->setScaledValue(val / 127.f);
			else if (pq->snapEnabled)
				pq->setValue(pq->getValue() + (float) delta);
			else
				pq->setScaledValue(clamp(pq->getScaledValue() + delta * t.fine / 200.f, 0.f, 1.f));
			return;
		}
		if (t.btn) {
			bool press = kind == K_NOTE ? on : val >= 64;
			if (kind == K_CC && rel)
				press = delta > 0;
			if (press && !t.btn->down) {
				t.btn->down = true;
				if (t.btn->act)
					t.btn->act();
			}
			else if (!press && t.btn->down) {
				t.btn->down = false;
				if (t.btn->release)
					t.btn->release();
			}
			if (kind == K_CC && rel && press) {
				// an encoder only ticks: let go at once
				t.btn->down = false;
				if (t.btn->release)
					t.btn->release();
			}
			return;
		}
		if (t.field && t.field->count && t.field->current && t.field->choose) {
			int n = t.field->count(), cur = t.field->current();
			if (n <= 0)
				return;
			int want = cur;
			if (kind == K_NOTE) {
				if (!on)
					return;
				want = (cur + 1) % n;
			}
			else if (rel)
				want = clamp(cur + (delta > 0 ? 1 : delta < 0 ? -1 : 0), 0, n - 1);
			else
				want = clamp(val * n / 128, 0, n - 1);
			if (want != cur)
				t.field->choose(want);
		}
	}

	// ---------------------------------------------------------------- export / import (no save dialog)
	std::string exportFile(const std::string& dir) {
		json_t* r = json_object();
		json_object_set_new(r, "genraMidiMap", json_string(kind.c_str()));
		json_object_set_new(r, "bindings", Store::listToJson(list()));
		std::string err;
		try {
			system::createDirectories(dir);
			std::string name = string::f("GENRA CHOP %s MIDI map", kind == "ablauf" ? "ABLAUF" : "");
			std::string file;
			for (int i = 1; i < 1000; i++) {
				file = system::join(dir, i == 1 ? name + ".json" : string::f("%s %d.json", name.c_str(), i));
				if (!system::exists(file))
					break;
			}
			FILE* f = std::fopen(file.c_str(), "w");
			if (!f)
				err = "Could not write " + file;
			else {
				json_dumpf(r, f, JSON_INDENT(2));
				std::fclose(f);
				err = "Saved " + system::getFilename(file);
			}
		}
		catch (Exception& e) {
			err = e.what();
		}
		json_decref(r);
		return err;
	}
	std::string importFile(const std::string& path) {
		FILE* f = std::fopen(path.c_str(), "r");
		if (!f)
			return "Could not open the file";
		json_error_t e;
		json_t* r = json_loadf(f, 0, &e);
		std::fclose(f);
		if (!r)
			return "Not a MIDI map";
		std::vector<Binding> v;
		Store::listFromJson(json_object_get(r, "bindings"), v);
		json_decref(r);
		if (v.empty())
			return "No mappings in the file";
		list() = v;
		Store::get().save();
		return string::f("%d mappings loaded", (int) v.size());
	}

	// ---------------------------------------------------------------- menus
	/** The right-click items of one control. */
	void appendTargetMenu(ui::Menu* menu, const std::string& id) {
		menu->addChild(new ui::MenuSeparator);
		const Binding* b = bindingOf(id);
		menu->addChild(createMenuItem(learning == id ? "MIDI learn… (move a control now)" : "MIDI learn",
		                              b ? b->device + " " + describe(*b) : "", [=]() { learning = learning == id ? "" : id; }));
		if (b) {
			menu->addChild(createSubmenuItem("MIDI mode", "", [=](ui::Menu* sub) {
				const char* NM[3] = {"Auto (finds encoders)", "Absolute knob / fader", "Endless encoder"};
				for (int m = 0; m < 3; m++)
					sub->addChild(createCheckMenuItem(NM[m], "", [=]() {
						const Binding* x = bindingOf(id);
						return x && x->mode == m;
					},
					                                  [=]() { setMode(id, m, -1); }));
				sub->addChild(new ui::MenuSeparator);
				sub->addChild(createCheckMenuItem("Encoder sends 1 / 127", "", [=]() {
					const Binding* x = bindingOf(id);
					return x && x->relType == 0;
				},
				                                  [=]() { setMode(id, M_REL, 0); }));
				sub->addChild(createCheckMenuItem("Encoder sends 63 / 65", "", [=]() {
					const Binding* x = bindingOf(id);
					return x && x->relType == 1;
				},
				                                  [=]() { setMode(id, M_REL, 1); }));
			}));
			menu->addChild(createMenuItem("Clear MIDI mapping", "", [=]() { forget(id); }));
		}
	}
	void setMode(const std::string& id, int mode, int relType) {
		for (Binding& b : list())
			if (b.target == id) {
				b.mode = mode;
				if (relType >= 0)
					b.relType = relType;
			}
		Store::get().save();
	}
	/** The module's menu: controllers, the mappings, export / import. */
	void appendModuleMenu(ui::Menu* menu, std::function<void(const std::string&)> say) {
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("MIDI control (kept on this computer, for every patch)"));
		for (int i = 0; i < CTRLS; i++) {
			CtrlIn* in = &ins[i];
			std::string nm = in->name();
			menu->addChild(createSubmenuItem(string::f("Controller %d", i + 1), nm.empty() ? "none" : nm,
			                                 [=](ui::Menu* sub) { app::appendMidiMenu(sub, in); }));
		}
		menu->addChild(createSubmenuItem("Mappings", string::f("%d", (int) list().size()), [=](ui::Menu* sub) {
			if (list().empty())
				sub->addChild(createMenuLabel("None yet: right-click a control › MIDI learn"));
			std::vector<Binding> v = list();
			for (const Binding& b : v) {
				std::string id = b.target;
				sub->addChild(createSubmenuItem(labelOf(id), b.device + "  " + describe(b), [=](ui::Menu* s2) {
					appendTargetMenu(s2, id);
				}));
			}
			if (!v.empty()) {
				sub->addChild(new ui::MenuSeparator);
				sub->addChild(createMenuItem("Clear all mappings", "", [=]() {
					list().clear();
					Store::get().save();
				}));
			}
		}));
		if (!learning.empty())
			menu->addChild(createMenuItem("Cancel MIDI learn", labelOf(learning), [=]() { learning.clear(); }));
		menu->addChild(createMenuItem("Export mappings (to the MIDI export folder)", "", [=]() {
			say(exportFile(system::join(hhx::midiFolder(), "MIDI maps")));
		}));
		menu->addChild(createMenuItem("Import mappings…", "", [=]() {
			std::string dir = system::join(hhx::midiFolder(), "MIDI maps");
			osdialog_filters* f = osdialog_filters_parse("MIDI map:json");
			char* p = osdialog_file(OSDIALOG_OPEN, system::isDirectory(dir) ? dir.c_str() : nullptr, nullptr, f);
			osdialog_filters_free(f);
			if (!p)
				return;
			std::string path = p;
			std::free(p);
			say(importFile(path));
		}));
	}
};

/** A module widget that can learn (its knobs, buttons and fields find it through this). */
struct LearnHost {
	virtual Learner* learner() = 0;
	virtual ~LearnHost() {}
};
inline Learner* learnerOf(widget::Widget* w) {
	app::ModuleWidget* mw = w->getAncestorOfType<app::ModuleWidget>();
	LearnHost* h = dynamic_cast<LearnHost*>(mw);
	return h ? h->learner() : nullptr;
}

/** A knob with "MIDI learn" in its right-click menu. */
template <class K>
struct LearnKnob : K {
	void appendContextMenu(ui::Menu* menu) override {
		Learner* l = learnerOf(this);
		if (l && this->module)
			l->appendTargetMenu(menu, string::f("p%d", this->paramId));
	}
	void draw(const widget::Widget::DrawArgs& args) override {
		K::draw(args);
		Learner* l = learnerOf(this);
		if (l && l->learning == string::f("p%d", this->paramId)) {
			// learning: a ring that blinks
			float a = 0.5f + 0.5f * std::sin(8.f * (float) system::getTime());
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, this->box.size.x / 2, this->box.size.y / 2, this->box.size.x / 2 + 1.5f);
			nvgStrokeColor(args.vg, nvgRGBAf(0.84f, 0.65f, 0.25f, a));
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
		}
	}
};

} // namespace ml
