// SPDX-License-Identifier: Apache-2.0

#include "btop_json.hpp"

#include <csignal>
#include <deque>
#include <fstream>
#include <iostream>
#include <numeric>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <nlohmann/json.hpp>

#include "btop_config.hpp"
#include "btop_log.hpp"
#include "btop_shared.hpp"
#include "btop_tools.hpp"
#include "config.h"

using namespace Tools;
using std::string;
using std::vector;

using json = nlohmann::json;

namespace {

	//* Set by signal handlers for graceful shutdown
	volatile std::sig_atomic_t g_quit = 0;

	void signal_handler(int) {
		g_quit = 1;
	}

	//* Serialize any container of values to a JSON array
	template <typename T>
	auto to_json_array(const T& container) -> json {
		json arr = json::array();
		for (const auto& value : container) arr.push_back(value);
		return arr;
	}

	//* Serialize a map of string -> deque to a JSON object of arrays
	auto percent_map_to_json(const std::unordered_map<string, std::deque<long long>>& map) -> json {
		json out = json::object();
		for (const auto& [key, values] : map) out[key] = to_json_array(values);
		return out;
	}

	auto cpu_to_json(const Cpu::cpu_info& cpu) -> json {
		json out = json::object();
		out["name"] = Cpu::cpuName;
		out["frequency"] = Cpu::cpuHz;
		out["cores"] = Shared::coreCount;
		out["percent"] = percent_map_to_json(cpu.cpu_percent);
		out["core_percent"] = json::array();
		for (const auto& core : cpu.core_percent) out["core_percent"].push_back(to_json_array(core));
		out["temp"] = to_json_array(cpu.temp);
		out["temp_max"] = cpu.temp_max;
		out["load_avg"] = cpu.load_avg;
		out["usage_watts"] = cpu.usage_watts;
		if (cpu.active_cpus.has_value()) out["active_cpus"] = cpu.active_cpus.value();
		else out["active_cpus"] = nullptr;

		const auto& [bat_percent, bat_watts, bat_seconds, bat_status] = Cpu::current_bat;
		out["battery"] = {
			{"charge", bat_percent},
			{"watts", bat_watts},
			{"time_left", bat_seconds},
			{"status", bat_status},
		};
		return out;
	}

	auto disk_to_json_object(const Mem::disk_info& disk) -> json {
		return {
			{"dev", disk.dev.string()},
			{"name", disk.name},
			{"fstype", disk.fstype},
			{"total", disk.total},
			{"used", disk.used},
			{"free", disk.free},
			{"used_percent", disk.used_percent},
			{"free_percent", disk.free_percent},
			{"io_read", to_json_array(disk.io_read)},
			{"io_write", to_json_array(disk.io_write)},
			{"io_activity", to_json_array(disk.io_activity)},
		};
	}

	auto mem_to_json(const Mem::mem_info& mem) -> json {
		json out = json::object();
		out["stats"] = mem.stats;
		out["percent"] = percent_map_to_json(mem.percent);
		out["disks"] = json::array();
		for (const auto& name : mem.disks_order) {
			if (not mem.disks.contains(name)) continue;
			out["disks"].push_back(disk_to_json_object(mem.disks.at(name)));
		}
		return out;
	}

	auto net_to_json() -> json {
		json out = json::object();
		out["interfaces"] = Net::interfaces;
		out["current"] = json::object();
		for (const auto& iface : Net::interfaces) {
			if (not Net::current_net.contains(iface)) continue;
			const auto& netif = Net::current_net.at(iface);
			const auto& dl = netif.bandwidth.contains("download") ? netif.bandwidth.at("download") : std::deque<long long>{};
			const auto& ul = netif.bandwidth.contains("upload") ? netif.bandwidth.at("upload") : std::deque<long long>{};
			out["current"][iface] = {
				{"connected", netif.connected},
				{"ipv4", netif.ipv4},
				{"ipv6", netif.ipv6},
				{"download", {
					{"speed", dl.empty() ? 0 : dl.back()},
					{"total", netif.stat.contains("download") ? netif.stat.at("download").total : 0},
				}},
				{"upload", {
					{"speed", ul.empty() ? 0 : ul.back()},
					{"total", netif.stat.contains("upload") ? netif.stat.at("upload").total : 0},
				}},
			};
		}
		return out;
	}

	auto proc_to_json(const Proc::proc_info& proc) -> json {
		return {
			{"pid", proc.pid},
			{"name", proc.name},
			{"cmd", proc.cmd},
			{"short_cmd", proc.short_cmd},
			{"threads", proc.threads},
			{"user", proc.user},
			{"mem", proc.mem},
			{"cpu_percent", proc.cpu_p},
			{"cpu_cumulative", proc.cpu_c},
			{"state", string(1, proc.state)},
			{"nice", proc.p_nice},
			{"ppid", proc.ppid},
			{"cpu_system", proc.cpu_s},
			{"cpu_time", proc.cpu_t},
			{"death_time", proc.death_time},
			{"depth", proc.depth},
			{"collapsed", proc.collapsed},
			{"filtered", proc.filtered},
		};
	}

	auto proc_detail_to_json(const Proc::detail_container& detailed) -> json {
		return {
			{"pid", detailed.entry.pid},
			{"name", detailed.entry.name},
			{"cmd", detailed.entry.cmd},
			{"user", detailed.entry.user},
			{"state", string(1, detailed.entry.state)},
			{"status", detailed.status},
			{"elapsed", detailed.elapsed},
			{"parent", detailed.parent},
			{"ppid", detailed.entry.ppid},
			{"mem", detailed.entry.mem},
			{"memory", detailed.memory},
			{"io_read", detailed.io_read},
			{"io_write", detailed.io_write},
			{"cpu_percent", detailed.entry.cpu_p},
			{"cpu_percent_history", to_json_array(detailed.cpu_percent)},
			{"mem_bytes_history", to_json_array(detailed.mem_bytes)},
		};
	}

	auto proc_list_to_json(const vector<Proc::proc_info>& procs, const Json::Options& options) -> json {
		json out = json::object();
		out["count"] = procs.size();
		if (options.pid.has_value()) out["detailed"] = proc_detail_to_json(Proc::detailed);

		out["list"] = json::array();
		if (options.top_procs.has_value()) {
			//? Sort by cpu usage descending and keep only the top N entries
			vector<std::size_t> idx(procs.size());
			std::iota(idx.begin(), idx.end(), 0);
			std::sort(idx.begin(), idx.end(), [&procs](const std::size_t a, const std::size_t b) {
				return procs.at(a).cpu_p > procs.at(b).cpu_p;
			});
			if (idx.size() > options.top_procs.value()) idx.resize(options.top_procs.value());
			for (const std::size_t i : idx) out["list"].push_back(proc_to_json(procs.at(i)));
		}
		else {
			for (const auto& proc : procs) out["list"].push_back(proc_to_json(proc));
		}
		return out;
	}

#ifdef GPU_SUPPORT
	auto gpu_to_json(const vector<Gpu::gpu_info>& gpus) -> json {
		json out = json::array();
		for (std::size_t i = 0; i < gpus.size(); ++i) {
			const auto& gpu = gpus.at(i);
			json gpu_json = {
				{"name", i < Gpu::gpu_names.size() ? Gpu::gpu_names.at(i) : ""},
				{"gpu_percent", percent_map_to_json(gpu.gpu_percent)},
				{"utilization", gpu.gpu_percent.at("gpu-totals").empty() ? json(0) : json(gpu.gpu_percent.at("gpu-totals").back())},
				{"vram_total", gpu.mem_total},
				{"vram_used", gpu.mem_used},
				{"temp", gpu.temp.empty() ? json(0) : json(gpu.temp.back())},
				{"power_mw", gpu.pwr_usage},
				{"power_max_mw", gpu.pwr_max_usage},
				{"power_state", gpu.pwr_state},
				{"gpu_clock_mhz", gpu.gpu_clock_speed},
				{"mem_clock_mhz", gpu.mem_clock_speed},
				{"pcie_tx_kbs", gpu.pcie_tx},
				{"pcie_rx_kbs", gpu.pcie_rx},
				{"encoder_utilization", gpu.encoder_utilization},
				{"decoder_utilization", gpu.decoder_utilization},
			};
			out.push_back(gpu_json);
		}
		return out;
	}
#endif

	//* Collect a warm-up sample of every selected section so deltas are valid from the first real snapshot
	void warmup(const Json::Options& options) {
		if (options.has("cpu")) Cpu::collect();
		if (options.has("mem")) Mem::collect();
		if (options.has("net")) Net::collect();
		if (options.has("proc")) Proc::collect();
#ifdef GPU_SUPPORT
		if (options.has("gpu") and Gpu::count > 0) Gpu::collect();
#endif
	}

	auto write_output(const Json::Options& options, const string& out) -> bool {
		if (options.output_file.empty() or options.output_file == "-") {
			std::cout << out << '\n' << std::flush;
			return true;
		}
		std::ofstream of(options.output_file, std::ios::trunc);
		if (not of.good()) return false;
		of << out << '\n';
		of.close();
		return true;
	}

	//* Fork and detach from the terminal, redirecting std streams to /dev/null
	auto daemonize_impl(const std::filesystem::path& output_file, const std::optional<std::filesystem::path>& pidfile) -> bool {
		if (output_file.empty() or output_file == "-") {
			fmt::println(std::cerr, "error: daemon mode requires a real output file");
			return false;
		}

		//? Make sure the output file can be written to before detaching
		{
			std::ofstream probe(output_file, std::ios::app);
			if (not probe.good()) {
				fmt::println(std::cerr, "error: cannot open output file '{}'", output_file.string());
				return false;
			}
		}

		//? Prevent SIGHUP when the controlling terminal closes
		std::signal(SIGHUP, SIG_IGN);

		const pid_t pid = fork();
		if (pid < 0) return false;
		if (pid > 0) _Exit(0); //? Parent exits immediately
		if (setsid() == -1) return false;

		//? Second fork so the daemon is not a session leader and can never acquire a terminal
		const pid_t pid2 = fork();
		if (pid2 < 0) return false;
		if (pid2 > 0) _Exit(0);

		if (chdir("/") != 0) return false;
		umask(0);

		const int devnull = open("/dev/null", O_RDWR);
		if (devnull != -1) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO) close(devnull);
		}

		if (pidfile.has_value() and not pidfile.value().empty()) {
			std::ofstream pf(pidfile.value(), std::ios::trunc);
			if (pf.good()) pf << getpid() << '\n';
			else Logger::warning("Failed to write pidfile: {}", pidfile.value().string());
		}
		return true;
	}
}

namespace Json {

	auto proc_to_json(const Proc::proc_info& proc) -> std::string {
		return ::proc_to_json(proc).dump();
	}

	auto disk_to_json(const Mem::disk_info& disk) -> std::string {
		return disk_to_json_object(disk).dump();
	}

	auto snapshot(const Options& options, bool pretty) -> std::string {
		json root = json::object();

		//? Meta
		json meta = {
			{"version", Global::Version},
			{"git_commit", GIT_COMMIT},
			{"timestamp", Tools::strf_time("%Y-%m-%dT%H:%M:%S%z")},
			{"uptime", Tools::system_uptime()},
			{"hostname", Tools::hostname()},
			{"username", Tools::username()},
			{"cpu_name", Cpu::cpuName},
			{"cpu_frequency", Cpu::cpuHz},
			{"cores", Shared::coreCount},
		};
		struct utsname uts {};
		if (uname(&uts) == 0) {
			meta["platform"] = uts.sysname;
			meta["kernel"] = uts.release;
		}
		else {
			meta["platform"] = nullptr;
			meta["kernel"] = nullptr;
		}
		meta["container_engine"] = Cpu::container_engine.has_value() ? json(Cpu::container_engine.value()) : json(nullptr);
		root["meta"] = meta;

		//? CPU
		if (options.has("cpu")) root["cpu"] = cpu_to_json(Cpu::collect());

#ifdef GPU_SUPPORT
		//? GPU
		if (options.has("gpu")) {
			if (Gpu::count > 0) root["gpu"] = gpu_to_json(Gpu::collect());
			else root["gpu"] = json::array();
		}
#endif

		//? MEM
		if (options.has("mem")) root["mem"] = mem_to_json(Mem::collect());

		//? NET
		if (options.has("net")) {
			Net::collect();
			root["net"] = net_to_json();
		}

		//? PROC
		if (options.has("proc")) {
			if (options.pid.has_value()) {
				Config::set("show_detailed", true);
				Config::set("detailed_pid", static_cast<int>(options.pid.value()));
			}
			root["proc"] = proc_list_to_json(Proc::collect(), options);
		}

		return pretty ? root.dump(2) : root.dump();
	}

	auto run(const Options& options, std::uint32_t update_ms, std::uint32_t iterations) -> int {
		warmup(options);

		//? Graceful shutdown on SIGINT/SIGTERM for loop mode
		std::signal(SIGINT, signal_handler);
		std::signal(SIGTERM, signal_handler);

		//? Pretty output for a single snapshot, newline-delimited compact output when streaming
		const bool pretty = (iterations == 1);

		std::uint32_t written = 0;
		while (true) {
			//? Sleep before sampling so deltas are measured over a full update interval (like iostat/sar)
			sleep_ms(update_ms);
			if (g_quit) break;

			const string out = snapshot(options, pretty);
			if (not write_output(options, out)) {
				fmt::println(std::cerr, "error: failed to write JSON output");
				return 1;
			}
			if (++written >= iterations and iterations > 0) break;
		}

		return 0;
	}

	auto daemonize(const Options& options) -> bool {
		return daemonize_impl(
			options.output_file,
			options.pidfile.empty() ? std::nullopt : std::optional { options.pidfile }
		);
	}
}
