// SPDX-License-Identifier: GPL-3.0-only
// A private copy of the KESSEL core library (res/kesselcore.*), loaded for one module instance. The LXR firmware keeps its
// state in static variables, so a copy of the library is an engine of its own.
#pragma once
#include <string>
#include "../../kesselcore/kesselcore.h"

struct KesselCore {
	const jks_api* api = nullptr;
	std::string error;

	KesselCore() {}
	~KesselCore() { unload(); }
	KesselCore(const KesselCore&) = delete;
	KesselCore& operator=(const KesselCore&) = delete;

	/** Copies the library to a file of its own and loads it. False (and `error`) when it cannot. */
	bool load();
	void unload();
	bool loaded() const { return api != nullptr; }

private:
	void* handle = nullptr;
	std::string copyPath;
};
