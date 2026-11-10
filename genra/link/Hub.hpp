// SPDX-License-Identifier: GPL-3.0-only
// THE LINK HUB: one registry for the cable-free links (Follow GENRA, MIX "CH n input from", GENRA FX, DUCK), shared by
// the two plugins of the family: GENRA (sequencer, MIX, effects, a few instruments) and GENRA Instruments.
//
// Each plugin is its own library, with its own copy of every static variable. The links need one registry for both:
// when a module is removed, every reference to it must go, whichever plugin holds the reference. The first plugin that
// loads creates the hub and leaves its address in an environment variable of the Rack process; the other one finds it
// there and uses the same hub (its functions run in the plugin that made it). Plugins are not unloaded while Rack runs.
//
// The two plugins must agree on the layout of what they share (follow::Ref, follow::Host …): the hub carries a
// version made of LINK_VERSION and those sizes. A plugin whose version differs (an old copy of the other plugin) does not
// join: it keeps a hub of its own, and links across the two plugins simply do not form (nothing points across).
#pragma once
#include <rack.hpp>

namespace follow {
struct Ref;
struct Host;
} // namespace follow

namespace genralink {
using rack::engine::Module;

// GENRA as a follower sees it (also from the other plugin): its outputs per frame and its tempo
struct GenraInfo {
	const float* followPub = nullptr; // [2][outputs][PUB_CHANNELS]: the outputs as of the last complete frame
	int outputs = 0;
	const float* stepPeriod = nullptr; // samples per 16th
};

struct Hub {
	uint64_t version;
	// references (follow::Ref): point / unregister; gone(m): every reference to m goes, and m's own
	void (*point)(follow::Ref* r, Module* owner, Module* to);
	void (*unref)(follow::Ref* r);
	void (*gone)(Module* m);
	// modules MIX can take audio from / that follow GENRA (follow::Host), seen from both plugins
	void (*addHost)(Module* m, follow::Host* h);
	follow::Host* (*findHost)(Module* m);
	// GENRAs
	void (*addGenra)(Module* m, const GenraInfo& info);
	bool (*genraInfo)(Module* m, GenraInfo* out);
};

/** The hub of this Rack (shared if the other plugin agrees on the version). */
Hub& hub();

} // namespace genralink
