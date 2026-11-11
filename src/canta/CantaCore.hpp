// SPDX-License-Identifier: GPL-3.0-only
// A private copy of the CANTA core library (res/cantacore.*), loaded for one module instance. The voice's effects keep
// their buffers in static variables, so a copy of the library is an engine of its own.
#pragma once
#include <string>
#include "../../cantacore/cantacore.h"

struct CantaCore {
	const jca_api* api = nullptr;
	std::string error;

	CantaCore() {}
	~CantaCore() { unload(); }
	CantaCore(const CantaCore&) = delete;
	CantaCore& operator=(const CantaCore&) = delete;

	/** Copies the library to a file of its own and loads it. False (and `error`) when it cannot. */
	bool load();
	void unload();
	bool loaded() const { return api != nullptr; }

private:
	void* handle = nullptr;
	std::string copyPath;
};
