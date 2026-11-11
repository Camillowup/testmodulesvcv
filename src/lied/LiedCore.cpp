// SPDX-License-Identifier: GPL-3.0-only
#include "LiedCore.hpp"

#if defined(ARCH_WIN)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

#if defined(ARCH_WIN)
static const char* CORE_FILE = "liedcore.dll";
#elif defined(ARCH_MAC)
static const char* CORE_FILE = "liedcore.dylib";
#else
static const char* CORE_FILE = "liedcore.so";
#endif

namespace lied {

Core& Core::get() {
	static Core c;
	return c;
}

bool Core::ready() {
	std::lock_guard<std::mutex> lk(m);
	if (api)
		return rate > 0;
	if (!error.empty())
		return false;
	std::string path = asset::plugin(pluginInstance, std::string("res/") + CORE_FILE);
	void* sym = nullptr;
#if defined(ARCH_WIN)
	std::wstring w = string::UTF8toUTF16(path);
	HMODULE h = LoadLibraryW(w.c_str());
	if (!h) {
		error = string::f("LIED core: LoadLibrary failed (%lu)", (unsigned long) GetLastError());
		return false;
	}
	sym = (void*) GetProcAddress(h, "lic_get");
#else
	void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		const char* e = dlerror();
		error = std::string("LIED core: ") + (e ? e : "dlopen failed");
		return false;
	}
	sym = dlsym(h, "lic_get");
#endif
	const lic_api* a = sym ? ((lic_get_fn) sym)() : nullptr;
	if (!a || a->api != LIC_API_VERSION) {
		error = "LIED core has the wrong interface";
		return false;
	}
	std::string data = asset::plugin(pluginInstance, "res/espeak-ng-data");
	int r = a->init(data.c_str());
	if (r <= 0) {
		error = a->error();
		return false;
	}
	api = a;
	rate = r;
	return true;
}

// ------------------------------------------------------------------------------------------------ the worker
Worker::Worker() {
	for (int i = 0; i < LINES; i++)
		line[i] = nullptr;
	inUse[0] = inUse[1] = nullptr;
	th = std::thread([this]() { run(); });
}

Worker::~Worker() {
	{
		std::lock_guard<std::mutex> lk(m);
		stop = true;
	}
	cv.notify_all();
	if (th.joinable())
		th.join();
	sweep(true);
	for (int i = 0; i < LINES; i++)
		delete line[i].exchange(nullptr);
}

void Worker::want(int i, const std::string& text, const std::string& voice, int f0) {
	std::string key = voice + "|" + std::to_string(f0) + "|" + text;
	{
		std::lock_guard<std::mutex> lk(m);
		if (asked[i] == key)
			return;
		asked[i] = key;
		pending[i].text = text;
		pending[i].voice = voice;
		pending[i].f0 = f0;
		pending[i].key = key;
		has[i] = true;
	}
	cv.notify_all();
}

void Worker::sweep(bool all) {
	double now = system::getTime();
	for (size_t k = 0; k < retired.size();) {
		Line* l = retired[k].l;
		bool used = inUse[0].load() == l || inUse[1].load() == l;
		if (all || (!used && now - retired[k].t > 5.0)) {
			delete l;
			retired.erase(retired.begin() + k);
		} else
			k++;
	}
}

void Worker::run() {
	std::vector<float> buf(22050 * 40);
	std::vector<lic_ph> ph(2048);
	while (true) {
		Request rq;
		int which = -1;
		{
			std::unique_lock<std::mutex> lk(m);
			cv.wait_for(lk, std::chrono::seconds(1), [&]() {
				if (stop)
					return true;
				for (int i = 0; i < LINES; i++)
					if (has[i])
						return true;
				return false;
			});
			if (stop)
				return;
			for (int i = 0; i < LINES; i++)
				if (has[i]) {
					which = i;
					rq = pending[i];
					has[i] = false;
					break;
				}
		}
		sweep(false);
		if (which < 0)
			continue;
		Core& core = Core::get();
		if (!core.ready()) {
			error = core.error;
			failed = true;
			continue;
		}
		busy++;
		Line* L = new Line;
		auto speak = [&](const std::string& t, std::vector<float>& out, std::vector<Ph>& phs) {
			std::lock_guard<std::mutex> lk(core.m);
			int np = 0;
			int n = core.api->render(rq.voice.c_str(), t.c_str(), 110, rq.f0, buf.data(), (int) buf.size(), ph.data(),
			                         (int) ph.size(), &np);
			if (n <= 0)
				return false;
			out.assign(buf.begin(), buf.begin() + n);
			phs.clear();
			for (int i = 0; i < np; i++)
				phs.push_back({ph[i].pos, ph[i].vowel != 0, ph[i].name});
			return true;
		};
		makeLine(*L, rq.text, speak, core.rate, (float) core.rate / (float) rq.f0);
		L->key = rq.key;
		busy--;
		{
			std::lock_guard<std::mutex> lk(m);
			if (stop) {
				delete L;
				return;
			}
		}
		Line* old = line[which].exchange(L);
		if (old)
			retired.push_back({old, system::getTime()});
	}
}

}  // namespace lied
