// SPDX-License-Identifier: Apache-2.0

#include "btop_monitor.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <sqlite3/sqlite3.h>

#include <fmt/core.h>

#include "btop_config.hpp"
#include "btop_log.hpp"
#include "btop_shared.hpp"

namespace fs = std::filesystem;

using std::deque;
using std::string;
using std::unordered_map;

namespace {

	constexpr auto SCHEMA = R"SQL(
CREATE TABLE IF NOT EXISTS event (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	ts INTEGER NOT NULL,
	metric TEXT NOT NULL,
	resource TEXT NOT NULL,
	value REAL NOT NULL,
	threshold REAL NOT NULL,
	state TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_event_ts ON event(ts);
)SQL";

	//? Tracked state of a single metric/resource pair
	struct MetricState {
		bool triggered = false;
		int samples = 0;
	};

	sqlite3* g_db = nullptr;
	unordered_map<string, MetricState> g_states;
	int g_debounce = 3;

	//? Log a sqlite error and return false
	bool log_sqlite_error(const char* op) {
		if (g_db == nullptr) return false;
		Logger::error("Monitor: {} failed: {}", op, sqlite3_errmsg(g_db));
		return false;
	}

	//? Run a single SQL statement, ignoring errors only for creation statements
	bool exec(const char* sql, const char* op) {
		char* err = nullptr;
		if (sqlite3_exec(g_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
			Logger::error("Monitor: {} failed: {}", op, err ? err : sqlite3_errmsg(g_db));
			sqlite3_free(err);
			return false;
		}
		return true;
	}

	//? Insert one event row using bound parameters (safe for arbitrary resource names)
	void record_event(const string& metric, const string& resource, const double value, const double threshold, const string& state) {
		if (g_db == nullptr) return;
		const auto ts = static_cast<long long>(time(nullptr));
		sqlite3_stmt* stmt = nullptr;
		constexpr const char* SQL = "INSERT INTO event (ts, metric, resource, value, threshold, state) VALUES (?, ?, ?, ?, ?, ?)";
		if (sqlite3_prepare_v2(g_db, SQL, -1, &stmt, nullptr) != SQLITE_OK) {
			log_sqlite_error("prepare");
			return;
		}
		sqlite3_bind_int64(stmt, 1, ts);
		sqlite3_bind_text(stmt, 2, metric.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, resource.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_double(stmt, 4, value);
		sqlite3_bind_double(stmt, 5, threshold);
		sqlite3_bind_text(stmt, 6, state.c_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) != SQLITE_DONE) {
			log_sqlite_error("insert");
			sqlite3_finalize(stmt);
			return;
		}
		sqlite3_finalize(stmt);
		Logger::info("Monitor: {} {} {} = {:.2f} (threshold {:.2f})", state, metric, resource, value, threshold);
	}

	//? Update the debounce state for one metric/resource and record a transition when due
	void check(const string& metric, const string& resource, const double value, const double threshold) {
		if (g_db == nullptr or threshold <= 0) return;

		auto& state = g_states[metric + "/" + resource];
		const bool exceeded = value >= threshold;

		if (state.triggered) {
			if (not exceeded) {
				if (++state.samples >= g_debounce) {
					record_event(metric, resource, value, threshold, "resolved");
					state.triggered = false;
					state.samples = 0;
				}
			}
			else {
				state.samples = 0;
			}
		}
		else {
			if (exceeded) {
				if (++state.samples >= g_debounce) {
					record_event(metric, resource, value, threshold, "triggered");
					state.triggered = true;
					state.samples = 0;
				}
			}
			else {
				state.samples = 0;
			}
		}
	}

	//? Latest value from a deque, or 0 when empty
	long long deque_back(const deque<long long>& values) {
		return values.empty() ? 0 : values.back();
	}
}

namespace Monitor {

	void init() {
		if (not Config::getB("monitor_enabled")) return;

		fs::path db_path;
		const auto& cfg_path = Config::getS("monitor_db");
		if (not cfg_path.empty()) {
			db_path = cfg_path;
		}
		else {
			const auto state_dir = Config::get_state_dir();
			if (not state_dir.has_value()) {
				Logger::error("Monitor: cannot determine state directory for default database path");
				return;
			}
			db_path = state_dir.value() / "btop.db";
		}

		g_debounce = std::max(Config::getI("monitor_debounce"), 1);

		if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
			Logger::error("Monitor: cannot open database '{}': {}", db_path.string(), g_db ? sqlite3_errmsg(g_db) : "unknown error");
			if (g_db != nullptr) {
				sqlite3_close(g_db);
				g_db = nullptr;
			}
			return;
		}

		//? WAL + normal sync as requested, and a busy timeout for concurrent readers
		if (not exec("PRAGMA journal_mode=WAL;", "set journal_mode") or
			not exec("PRAGMA synchronous=NORMAL;", "set synchronous") or
			not exec("PRAGMA busy_timeout=5000;", "set busy_timeout")) {
			sqlite3_close(g_db);
			g_db = nullptr;
			return;
		}

		if (not exec(SCHEMA, "create schema")) {
			sqlite3_close(g_db);
			g_db = nullptr;
			return;
		}

		Logger::info("Monitor: threshold monitoring enabled, database at {}", db_path.string());
	}

	void evaluate() {
		if (g_db == nullptr) return;

		//? CPU (total percent)
		if (const int threshold = Config::getI("monitor_cpu"); threshold > 0) {
			if (const auto it = Cpu::current_cpu.cpu_percent.find("total"); it != Cpu::current_cpu.cpu_percent.end())
				check("cpu", "total", static_cast<double>(deque_back(it->second)), static_cast<double>(threshold));
		}

		//? RAM (used percent)
		if (const int threshold = Config::getI("monitor_mem"); threshold > 0) {
			if (const auto it = Mem::current_mem.percent.find("used"); it != Mem::current_mem.percent.end())
				check("mem", "total", static_cast<double>(deque_back(it->second)), static_cast<double>(threshold));
		}

		//? Disks (used percent per mountpoint)
		if (const int threshold = Config::getI("monitor_disk"); threshold > 0) {
			for (const auto& name : Mem::current_mem.disks_order) {
				const auto it = Mem::current_mem.disks.find(name);
				if (it == Mem::current_mem.disks.end()) continue;
				check("disk", name, static_cast<double>(it->second.used_percent), static_cast<double>(threshold));
			}
		}

		//? Network (Mbit/s, download+upload combined, per interface)
		if (const int threshold = Config::getI("monitor_net"); threshold > 0) {
			for (const auto& [iface, netif] : Net::current_net) {
				const auto dl_it = netif.bandwidth.find("download");
				const auto ul_it = netif.bandwidth.find("upload");
				if (dl_it == netif.bandwidth.end() or ul_it == netif.bandwidth.end()) continue;
				const auto bytes_per_sec = deque_back(dl_it->second) + deque_back(ul_it->second);
				const auto mbit_per_sec = static_cast<double>(bytes_per_sec) * 8.0 / 1'000'000.0;
				check("net", iface, mbit_per_sec, static_cast<double>(threshold));
			}
		}
	}

	void shutdown() {
		if (g_db != nullptr) {
			sqlite3_close(g_db);
			g_db = nullptr;
		}
		g_states.clear();
	}
}
