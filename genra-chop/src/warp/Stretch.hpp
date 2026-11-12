// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP: time-stretching for WARP (TONE), with two libraries to compare (no Rack in here):
//   Rubber Band Library (R3 "finer" engine), GPL-2.0-or-later, dep/rubberband (Particular Programs Ltd.)
//   Signalsmith Stretch, MIT, dep/signalsmith-stretch + dep/signalsmith-linear (Signalsmith Audio)
// Runs offline, in the worker thread: a stretched copy of the whole sample, the pitch stays.
#pragma once
#include "../hh/Chop.hpp"
#include <string>

namespace hhw {

enum Engine { ENGINE_RUBBER, ENGINE_SIGNAL, NUM_ENGINES };
extern const char* const ENGINE_NAMES[NUM_ENGINES];

/** `ratio`: the new length / the old length (0.25..4). False with err set if it failed. */
bool stretch(const hh::SampleData& in, double ratio, int engine, hh::SampleData& out, std::string& err);

} // namespace hhw
