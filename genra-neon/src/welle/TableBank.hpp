// SPDX-License-Identifier: GPL-3.0-only
// WELLE – where the wavetables come from: the folders (five built-in ones, then the folders below the user's root
// folder with WAV files, sub folders included) and, per oscillator, the tables of its folder, loaded in a worker
// thread so the audio thread never waits and never frees: it takes a table when it is there (an atomic pointer) and
// says which one it uses; the worker frees an old table only when no oscillator uses it any more.
#pragma once
#include "../plugin.hpp"
#include "../neon/Wavetable.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace welle {

static const int MAXT = 128;  // tables per folder (more WAV files: the first 128)

struct FolderInfo {
	std::string name;   // "TECHNO", "MY PACK/LEADS"
	int builtin = -1;   // a built-in folder (neon::BuiltinFolder), or -1
	std::string path;   // a user folder
	std::vector<int> ids;               // built-in: the tables
	std::vector<std::string> files;     // user: the WAV files (sorted)
	int count() const { return builtin >= 0 ? (int) ids.size() : (int) files.size(); }
	std::string tableName(int i) const {
		if (i < 0 || i >= count())
			return "";
		if (builtin >= 0)
			return neon::BUILTINS[ids[i]].name;
		std::string f = system::getStem(files[i]);
		return string::uppercase(f);
	}
};

/** The root folder of the user's wavetables (one for all WELLE modules; saved in the user folder). */
inline std::string& rootDir() {
	static std::string r;
	return r;
}
inline std::string settingsFile() { return asset::user("GenraNeon.json"); }
inline void loadSettings() {
	static bool done = false;
	if (done)
		return;
	done = true;
	json_t* j = json_load_file(settingsFile().c_str(), 0, nullptr);
	if (!j)
		return;
	if (json_t* r = json_object_get(j, "wavetableRoot"))
		if (json_is_string(r))
			rootDir() = json_string_value(r);
	json_decref(j);
}
inline void saveSettings() {
	json_t* j = json_object();
	json_object_set_new(j, "wavetableRoot", json_string(rootDir().c_str()));
	json_dump_file(j, settingsFile().c_str(), JSON_INDENT(2));
	json_decref(j);
}

inline bool isDirSafe(const std::string& p) {
	try {
		return !p.empty() && system::isDirectory(p);
	} catch (std::exception& e) {
		return false;
	}
}
inline std::vector<std::string> entriesSafe(const std::string& d) {
	try {
		return system::getEntries(d);
	} catch (std::exception& e) {
		return {};
	}
}
inline bool isWavFile(const std::string& p) { return string::lowercase(system::getExtension(p)) == ".wav"; }

/** The folder list: the built-in ones, then every folder below the root that holds WAV files (3 levels deep). */
inline std::vector<FolderInfo> scanFolders(const std::string& root) {
	std::vector<FolderInfo> v;
	for (int f = 0; f < neon::NUM_BUILTIN_FOLDERS; f++) {
		FolderInfo fi;
		fi.name = neon::FOLDER_NAMES[f];
		fi.builtin = f;
		fi.ids = neon::builtinsOf(f);
		v.push_back(fi);
	}
	if (!isDirSafe(root))
		return v;
	std::function<void(const std::string&, const std::string&, int)> walk = [&](const std::string& dir, const std::string& name,
	                                                                            int depth) {
		std::vector<std::string> es = entriesSafe(dir);
		std::sort(es.begin(), es.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(system::getFilename(a)) < string::lowercase(system::getFilename(b));
		});
		FolderInfo fi;
		fi.name = name.empty() ? "USER" : name;
		fi.path = dir;
		for (const std::string& e : es)
			if (isWavFile(e) && (int) fi.files.size() < MAXT)
				fi.files.push_back(e);
		if (!fi.files.empty())
			v.push_back(fi);
		if (depth >= 3)
			return;
		for (const std::string& e : es)
			if (isDirSafe(e)) {
				std::string n = string::uppercase(system::getFilename(e));
				walk(e, name.empty() ? n : name + "/" + n, depth + 1);
			}
	};
	walk(root, "", 0);
	return v;
}

/** Everything about the tables of one WELLE: the folder list, the two oscillators' tables, mutants, the worker. */
struct TableBank {
	struct Slot {
		int folder = -1;   // the folder loaded (worker)
		int gen = 0;
		FolderInfo info;
		std::vector<std::shared_ptr<neon::Table>> owned;
		std::atomic<neon::Table*> live[MAXT];
		std::atomic<int> count{0};
		std::atomic<int> want{0};          // the audio thread's table (index into the folder)
		std::atomic<neon::Table*> using_{nullptr};  // what the audio thread plays now
		std::vector<bool> failed;
		// mutant
		std::atomic<uint32_t> mutantReq{0};  // a seed to build (0: none)
		std::shared_ptr<neon::Table> mutant;
		std::atomic<neon::Table*> mutantLive{nullptr};
		uint32_t mutantSeed = 0;
		Slot() {
			for (auto& p : live)
				p.store(nullptr);
		}
	};
	Slot slot[2];
	std::shared_ptr<neon::Table> fallback;  // BASIC: there before anything else, never freed
	std::mutex m;                            // folders, owned tables, the graveyard
	std::vector<FolderInfo> folders;
	std::vector<std::pair<std::shared_ptr<neon::Table>, int>> grave;  // old tables and how many rounds they wait
	std::atomic<int> wantFolder[2];
	std::atomic<bool> rescan{false};
	std::atomic<int> folderCount{neon::NUM_BUILTIN_FOLDERS};
	std::thread worker;
	std::atomic<bool> quit{false};
	std::condition_variable cv;
	std::mutex cvm;

	TableBank() {
		wantFolder[0].store(0);
		wantFolder[1].store(0);
		fallback = neon::makeBuiltin(neon::B_BASIC);
		loadSettings();
		folders = scanFolders(rootDir());
		folderCount.store((int) folders.size());
		worker = std::thread([this]() { run(); });
	}
	~TableBank() {
		quit.store(true);
		cv.notify_all();
		if (worker.joinable())
			worker.join();
	}
	void wake() { cv.notify_all(); }

	/** Audio thread: the table oscillator o plays now (index idx of its folder; mutant when set). */
	neon::Table* pick(int o, int idx, bool useMutant) {
		Slot& s = slot[o];
		neon::Table* t = nullptr;
		if (useMutant)
			t = s.mutantLive.load();
		if (!t) {
			int n = s.count.load();
			if (n > 0) {
				idx = std::max(0, std::min(idx, n - 1));
				s.want.store(idx);
				t = s.live[idx].load();
			}
		}
		if (!t)
			t = s.using_.load();  // keep the last one until the new one is there
		if (!t)
			t = fallback.get();
		s.using_.store(t);
		return t;
	}

	FolderInfo folderInfo(int f) {
		std::lock_guard<std::mutex> lk(m);
		return f >= 0 && f < (int) folders.size() ? folders[f] : FolderInfo();
	}
	std::vector<FolderInfo> folderList() {
		std::lock_guard<std::mutex> lk(m);
		return folders;
	}
	int findFolder(const std::string& name) {
		std::lock_guard<std::mutex> lk(m);
		for (size_t i = 0; i < folders.size(); i++)
			if (folders[i].name == name)
				return (int) i;
		return -1;
	}

	// ---------------------------------------------------------------- the worker
	void run() {
		while (!quit.load()) {
			{
				std::unique_lock<std::mutex> lk(cvm);
				cv.wait_for(lk, std::chrono::milliseconds(15));
			}
			if (quit.load())
				break;
			if (rescan.exchange(false)) {
				std::vector<FolderInfo> nf = scanFolders(rootDir());
				std::lock_guard<std::mutex> lk(m);
				folders = nf;
				folderCount.store((int) folders.size());
				for (auto& s : slot)
					s.folder = -1;  // reload (the folder may have moved in the list)
			}
			for (int o = 0; o < 2; o++)
				step(o);
			collect();
		}
	}
	void step(int o) {
		Slot& s = slot[o];
		int f = wantFolder[o].load();
		if (f != s.folder) {
			FolderInfo fi = folderInfo(f);
			if (fi.count() == 0)
				fi = folderInfo(0), f = 0;
			std::lock_guard<std::mutex> lk(m);
			for (auto& t : s.owned)
				if (t)
					grave.push_back({t, 0});
			s.owned.assign(MAXT, nullptr);
			for (auto& p : s.live)
				p.store(nullptr);
			s.failed.assign(MAXT, false);
			s.info = fi;
			s.folder = f;
			s.gen++;
			s.count.store(fi.count());
		}
		// the table wanted, then its neighbours (TABLE turns quickly through them)
		int n = s.count.load();
		int w = std::max(0, std::min(s.want.load(), n - 1));
		const int order[5] = {0, 1, -1, 2, -2};
		for (int k = 0; k < 5 && n > 0; k++) {
			int i = w + order[k];
			if (i < 0 || i >= n || s.live[i].load() || s.failed[i])
				continue;
			std::shared_ptr<neon::Table> t;
			if (s.info.builtin >= 0)
				t = neon::makeBuiltin(s.info.ids[i]);
			else {
				std::string err;
				t = neon::loadWav(s.info.files[i], &err);
				if (!t)
					WARN("WELLE: %s: %s", s.info.files[i].c_str(), err.c_str());
			}
			std::lock_guard<std::mutex> lk(m);
			if (!t) {
				s.failed[i] = true;
				s.live[i].store(fallback.get());
				continue;
			}
			s.owned[i] = t;
			s.live[i].store(t.get());
			break;  // one table per round: the wanted one is checked again first
		}
		// a mutant to build
		uint32_t seed = s.mutantReq.exchange(0);
		if (seed) {
			std::shared_ptr<neon::Table> t = neon::makeMutant(seed);
			std::lock_guard<std::mutex> lk(m);
			if (s.mutant)
				grave.push_back({s.mutant, 0});
			s.mutant = t;
			s.mutantSeed = seed;
			s.mutantLive.store(t.get());
		}
	}
	/** Frees the old tables no oscillator plays any more (after a few rounds: the audio thread may just be taking one). */
	void collect() {
		std::lock_guard<std::mutex> lk(m);
		neon::Table* a = slot[0].using_.load();
		neon::Table* b = slot[1].using_.load();
		for (auto& g : grave)
			g.second++;
		grave.erase(std::remove_if(grave.begin(), grave.end(),
		                           [&](const std::pair<std::shared_ptr<neon::Table>, int>& g) {
			                           return g.second > 6 && g.first.get() != a && g.first.get() != b;
		                           }),
		            grave.end());
	}
	void clearMutant(int o) {
		std::lock_guard<std::mutex> lk(m);
		Slot& s = slot[o];
		s.mutantLive.store(nullptr);
		if (s.mutant)
			grave.push_back({s.mutant, 0});
		s.mutant.reset();
		s.mutantSeed = 0;
	}

	/** UI thread: n points of the table oscillator o plays, at frame position f (for the display). False: none. */
	bool shape(int o, float f, int n, float* out, int* frames = nullptr, std::string* name = nullptr) {
		std::lock_guard<std::mutex> lk(m);
		neon::Table* t = slot[o].using_.load();
		if (!t)
			return false;
		t->shape(f * (t->frames - 1), n, out);
		if (frames)
			*frames = t->frames;
		if (name)
			*name = t->name;
		return true;
	}
};

}  // namespace welle
