// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "btop_shared.hpp"

//* Headless JSON output mode, collects system stats and serializes them without requiring a terminal
namespace Json {

	//* Which sections to include in the JSON output and how to limit it
	struct Options {
		//? Subset of {"cpu", "mem", "net", "proc", "gpu"}
		std::vector<std::string> sections;
		//? Include detailed information for this pid (optional)
		std::optional<std::uint32_t> pid;
		//? Only output the top N processes sorted by cpu usage (optional)
		std::optional<std::uint32_t> top_procs;
		//? Path to output file, empty string or "-" means stdout
		std::filesystem::path output_file;
		//? Path to write the daemon PID to (only used with --daemon)
		std::filesystem::path pidfile;

		[[nodiscard]] auto has(const std::string_view section) const noexcept -> bool {
			return std::ranges::find(sections, section) != sections.end();
		}
	};

	//* Serialize a single process entry to a JSON string (exposed for testing and reuse)
	[[nodiscard]] std::string proc_to_json(const Proc::proc_info& proc);

	//* Serialize a single disk entry to a JSON string (exposed for testing and reuse)
	[[nodiscard]] std::string disk_to_json(const Mem::disk_info& disk);

	//* Collect all selected sections and return a JSON string. If <pretty> is false the output is
	//* compact and a trailing newline is appended, making it suitable for newline-delimited streaming.
	[[nodiscard]] std::string snapshot(const Options& options, bool pretty = true);

	//* Collect a warm-up sample of every selected section so deltas are valid from the first real
	//* snapshot. Must run once before the first real snapshot.
	void warmup(const Options& options);

	//* Fork and detach from the terminal, redirecting std streams to /dev/null and writing the
	//* pidfile. Unlike daemonize() no output file is required. Must be called before any collection
	//* so forked processes don't inherit thread/async state.
	bool daemonize_process(const std::optional<std::filesystem::path>& pidfile);

	//* Fork and detach from the terminal, redirecting std streams to /dev/null and writing the pidfile.
	//* Requires a real output file. Must be called before any collection so forked processes don't
	//* inherit thread/async state.
	bool daemonize(const Options& options);

	//* Run headless JSON mode, writing <iterations> snapshots (0 = forever) at <update_ms> intervals.
	int run(const Options& options, std::uint32_t update_ms, std::uint32_t iterations);
}
