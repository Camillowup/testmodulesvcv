// SPDX-License-Identifier: GPL-3.0-only
#include "Stretch.hpp"
#include <rubberband/RubberBandStretcher.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace hhw {

const char* const ENGINE_NAMES[NUM_ENGINES] = {"RUBBER", "SIGNAL"};

static bool rubber(const std::vector<const float*>& in, size_t n, int ch, float sr, double ratio,
                   std::vector<std::vector<float>>& out) {
	using RB = RubberBand::RubberBandStretcher;
	RB rb((size_t) sr, (size_t) ch, RB::OptionProcessOffline | RB::OptionEngineFiner, ratio, 1.0);
	const size_t BLOCK = 4096;
	rb.setDebugLevel(0);
	rb.setExpectedInputDuration(n);
	rb.setMaxProcessSize(BLOCK);
	std::vector<const float*> p(ch);
	// first the study pass (offline mode), then the processing; whatever is ready is taken as it comes
	for (size_t at = 0; at < n; at += BLOCK) {
		size_t k = std::min(BLOCK, n - at);
		for (int c = 0; c < ch; c++)
			p[c] = in[c] + at;
		rb.study(p.data(), k, at + k >= n);
	}
	std::vector<std::vector<float>> tmp(ch, std::vector<float>(BLOCK));
	std::vector<float*> q(ch);
	auto take = [&]() {
		int av;
		while ((av = rb.available()) > 0) {
			size_t k = std::min((size_t) av, BLOCK);
			for (int c = 0; c < ch; c++)
				q[c] = tmp[c].data();
			size_t got = rb.retrieve(q.data(), k);
			for (int c = 0; c < ch; c++)
				out[c].insert(out[c].end(), tmp[c].begin(), tmp[c].begin() + got);
			if (got == 0)
				break;
		}
	};
	for (size_t at = 0; at < n; at += BLOCK) {
		size_t k = std::min(BLOCK, n - at);
		for (int c = 0; c < ch; c++)
			p[c] = in[c] + at;
		rb.process(p.data(), k, at + k >= n);
		take();
	}
	take();
	return !out[0].empty();
}

static bool signal(const std::vector<const float*>& in, size_t n, int ch, float sr, double ratio,
                   std::vector<std::vector<float>>& out) {
	signalsmith::stretch::SignalsmithStretch<float> st;
	st.presetDefault(ch, sr);
	size_t outN = (size_t) std::llround(n * ratio);
	std::vector<std::vector<float>> o(ch, std::vector<float>(outN));
	std::vector<float*> ib(ch), ob(ch);
	std::vector<std::vector<float>> copy(ch);
	for (int c = 0; c < ch; c++) {
		copy[c].assign(in[c], in[c] + n);
		ib[c] = copy[c].data();
		ob[c] = o[c].data();
	}
	if (!st.exact(ib, (int) n, ob, (int) outN))
		return false;
	out = std::move(o);
	return true;
}

bool stretch(const hh::SampleData& in, double ratio, int engine, hh::SampleData& out, std::string& err) {
	size_t n = in.frames();
	if (n < 64) {
		err = "sample too short";
		return false;
	}
	ratio = std::max(0.25, std::min(4.0, ratio));
	int ch = in.R.empty() ? 1 : 2;
	std::vector<const float*> ip = {in.L.data()};
	if (ch == 2)
		ip.push_back(in.R.data());
	std::vector<std::vector<float>> o(ch);
	bool ok = false;
	try {
		ok = engine == ENGINE_SIGNAL ? signal(ip, n, ch, in.sampleRate, ratio, o) : rubber(ip, n, ch, in.sampleRate, ratio, o);
	}
	catch (...) {
		ok = false;
	}
	if (!ok) {
		err = "stretching failed";
		return false;
	}
	// exactly the new length (the chops are placed by it)
	size_t want = (size_t) std::llround(n * ratio);
	for (auto& v : o)
		v.resize(want, 0.f);
	out.sampleRate = in.sampleRate;
	out.path = in.path;
	out.name = in.name;
	out.L = std::move(o[0]);
	if (ch == 2)
		out.R = std::move(o[1]);
	else
		out.R.clear();
	return true;
}

} // namespace hhw
