// SPDX-License-Identifier: GPL-3.0-only
// THE LINK HUB (see Hub.hpp)
#include "Hub.hpp"
#include "../Follow.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

namespace genralink {

// what both plugins must agree on (a change of LINK_VERSION or of these layouts: no shared hub)
static const uint64_t LINK_VERSION = 1;
static uint64_t versionNow() {
	return LINK_VERSION | (uint64_t) sizeof(follow::Ref) << 8 | (uint64_t) sizeof(follow::Host) << 20 |
	       (uint64_t) sizeof(follow::Follower) << 36 | (uint64_t) sizeof(follow::Drive) << 52 |
	       (uint64_t) sizeof(GenraInfo) << 60;
}
static const char* ENV = "GENRA_LINK_HUB";

// ---- the registry of the plugin that made the hub
static std::mutex mx;
static std::vector<follow::Ref*> refs;
static std::map<Module*, follow::Host*> hosts;
static std::map<Module*, GenraInfo> genras;

static void hPoint(follow::Ref* r, Module* owner, Module* to) {
	std::lock_guard<std::mutex> lk(mx);
	r->owner = owner;
	if (std::find(refs.begin(), refs.end(), r) == refs.end())
		refs.push_back(r);
	r->to.store(to);
}
static void hUnref(follow::Ref* r) {
	std::lock_guard<std::mutex> lk(mx);
	refs.erase(std::remove(refs.begin(), refs.end(), r), refs.end());
}
static void hGone(Module* m) {
	std::lock_guard<std::mutex> lk(mx);
	for (follow::Ref* r : refs)
		if (r->to.load() == m)
			r->to.store(nullptr);
	// its own references point nowhere from now on (the memory goes with the module)
	refs.erase(std::remove_if(refs.begin(), refs.end(),
	                          [m](follow::Ref* r) {
		                          if (r->owner != m)
			                          return false;
		                          r->to.store(nullptr);
		                          return true;
	                          }),
	           refs.end());
	hosts.erase(m);
	genras.erase(m);
}
static void hAddHost(Module* m, follow::Host* h) {
	std::lock_guard<std::mutex> lk(mx);
	hosts[m] = h;
}
static follow::Host* hFindHost(Module* m) {
	std::lock_guard<std::mutex> lk(mx);
	auto it = hosts.find(m);
	return it == hosts.end() ? nullptr : it->second;
}
static void hAddGenra(Module* m, const GenraInfo& info) {
	std::lock_guard<std::mutex> lk(mx);
	genras[m] = info;
}
static bool hGenraInfo(Module* m, GenraInfo* out) {
	std::lock_guard<std::mutex> lk(mx);
	auto it = genras.find(m);
	if (it == genras.end())
		return false;
	*out = it->second;
	return true;
}

static Hub own = {0, hPoint, hUnref, hGone, hAddHost, hFindHost, hAddGenra, hGenraInfo};

Hub& hub() {
	static Hub* h = nullptr;
	if (h)
		return *h;
	own.version = versionNow();
	// the other plugin of the family may have made one already
	if (const char* e = std::getenv(ENV)) {
		void* p = nullptr;
		if (std::sscanf(e, "%p", &p) == 1 && p && static_cast<Hub*>(p)->version == own.version) {
			h = static_cast<Hub*>(p);
			return *h;
		}
	}
	h = &own;
	if (!std::getenv(ENV)) { // the first plugin: leave the address for the other one
		char buf[64];
		std::snprintf(buf, sizeof buf, "%p", (void*) h);
#ifdef _WIN32
		_putenv_s(ENV, buf);
#else
		setenv(ENV, buf, 0);
#endif
	}
	return *h;
}

} // namespace genralink
