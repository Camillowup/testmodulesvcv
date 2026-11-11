// SPDX-License-Identifier: GPL-3.0-only
// A private copy of the SAURUS core library (res/sauruscore.*), loaded for one module instance. The firmware keeps its
// state in static variables, so a copy of the library is an engine of its own.
#pragma once
#include <string>
#include "../../sauruscore/sauruscore.h"

struct SaurusCore {
	const jsa_api* api = nullptr;
	std::string error;

	SaurusCore() {}
	~SaurusCore() { unload(); }
	SaurusCore(const SaurusCore&) = delete;
	SaurusCore& operator=(const SaurusCore&) = delete;

	/** Copies the library to a file of its own and loads it. False (and `error`) when it cannot. */
	bool load();
	void unload();
	bool loaded() const { return api != nullptr; }

private:
	void* handle = nullptr;
	std::string copyPath;
};
