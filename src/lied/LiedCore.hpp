// SPDX-License-Identifier: GPL-3.0-only
// LIED: the eSpeak core (res/liedcore.*) and the worker that speaks the lyrics.
//
// eSpeak keeps its state in static variables, so there is one core for the whole process, loaded once and used under
// one lock. Each LIED has a worker thread of its own: it speaks a line when its text or voice changed and hands the
// result (a Line) to the audio thread through an atomic pointer. A replaced Line is kept a few seconds and freed
// only when no voice reads it any more.
#pragma once
#include "../plugin.hpp"
#include "../../liedcore/liedcore.h"
#include "LiedText.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lied {

/** The core: loaded on first use. All calls under lock(). */
struct Core {
	const lic_api* api = nullptr;
	std::atomic<int> rate{0};
	std::string error;
	std::mutex m;
	static Core& get();
	/** Loads the library and starts eSpeak on res/espeak-ng-data (once). */
	bool ready();
};

struct Request {
	std::string text, voice;
	int f0 = 130;
	std::string key;
};

struct Worker {
	static const int LINES = 4;
	std::atomic<Line*> line[LINES];
	std::atomic<const Line*> inUse[2];   // what the singer's two voices read (the audio thread writes)
	std::atomic<int> busy{0};            // lines being spoken right now (the display)
	std::atomic<bool> failed{false};
	std::string error;                   // UI thread reads after failed

	Worker();
	~Worker();
	/** UI thread: speak line i (its text with this voice) if that is not what it holds or is being made. */
	void want(int i, const std::string& text, const std::string& voice, int f0);

private:
	std::mutex m;
	std::condition_variable cv;
	Request pending[LINES];
	bool has[LINES] = {};
	std::string asked[LINES];            // the key last asked for
	struct Old {
		Line* l;
		double t;
	};
	std::vector<Old> retired;
	bool stop = false;
	std::thread th;
	void run();
	void sweep(bool all);
};

}  // namespace lied
