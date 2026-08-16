// SPDX-License-Identifier: Apache-2.0

#include "btop_cli.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <unistd.h>

#include <fmt/base.h>
#include <fmt/format.h>

#include "btop_config.hpp"
#include "btop_shared.hpp"
#include "config.h"

using namespace std::string_view_literals;

static constexpr auto BOLD = "\033[1m"sv;
static constexpr auto BOLD_UNDERLINE = "\033[1;4m"sv;
static constexpr auto BOLD_RED = "\033[1;31m"sv;
static constexpr auto BOLD_GREEN = "\033[1;32m"sv;
static constexpr auto BOLD_YELLOW = "\033[1;33m"sv;
static constexpr auto BOLD_BRIGHT_BLACK = "\033[1;90m"sv;
static constexpr auto YELLOW = "\033[33m"sv;
static constexpr auto RESET = "\033[0m"sv;

static void version() noexcept {
	if constexpr (GIT_COMMIT.empty()) {
		fmt::println("btop version: {}{}{}", BOLD, Global::Version, RESET);
	} else {
		fmt::println("btop version: {}{}+{}{}", BOLD, Global::Version, GIT_COMMIT, RESET);
	}
	fmt::println("based on btop v1.4.7");
}

static void build_info() noexcept {
	fmt::println("Compiled with: {} ({})", COMPILER, COMPILER_VERSION);
	fmt::println("Configured with: {}", CONFIGURE_COMMAND);
}

static void error(std::string_view msg) noexcept {
	fmt::println("{}error:{} {}\n", BOLD_RED, RESET, msg);
}

//* Parse a non-negative integer from a string_view that may not be null-terminated
//* (unlike std::stoi). Prints an error message for <name> and returns nullopt on
//* malformed or out of range input.
static auto parse_uint(const std::string_view value, const std::string_view name) -> std::optional<int> {
	int out = 0;
	const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
	if (ec == std::errc::result_out_of_range) {
		error(fmt::format("{} argument is out of range: {}", name, value));
		return std::nullopt;
	}
	if (ec != std::errc() or ptr != value.data() + value.size()) {
		error(fmt::format("{} must be a positive number", name));
		return std::nullopt;
	}
	return out;
}

namespace Cli {
	[[nodiscard]] auto parse(const std::span<const std::string_view> args) noexcept -> Result {
		Cli cli {};

		for (auto it = args.begin(); it != args.end(); ++it) {
			auto arg = *it;

			if (arg == "--default-config") {
				return default_config();
			}
			if (arg == "-h" || arg == "--help") {
				usage();
				help();
				return std::unexpected { 0 };
			}
			if (arg == "-v" || arg == "-V") {
				version();
				return std::unexpected { 0 };
			}
			if (arg == "--version") {
				version();
				build_info();
				return std::unexpected { 0 };
			}

			if (arg == "-d" || arg == "--debug") {
				cli.debug = true;
				continue;
			}
			if (arg == "--force-utf") {
				cli.force_utf = true;
				continue;
			}
			if (arg == "-l" || arg == "--low-color") {
				cli.low_color = true;
				continue;
			}
			if (arg == "-t" || arg == "--tty") {
				if (cli.force_tty.has_value()) {
					error("tty mode can't be set twice");
					return std::unexpected { 1 };
				}
				cli.force_tty = std::make_optional(true);
				continue;
			}
			if (arg == "--no-tty") {
				if (cli.force_tty.has_value()) {
					error("tty mode can't be set twice");
					return std::unexpected { 1 };
				}
				cli.force_tty = std::make_optional(false);
				continue;
			}

			if (arg == "-c" || arg == "--config") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Config requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				auto config_file = stdfs::path { arg };

				if (stdfs::is_directory(config_file)) {
					error("Config file can't be a directory");
					return std::unexpected { 1 };
				}

				cli.config_file = std::make_optional(config_file);
				continue;
			}
			if (arg == "-f" || arg == "--filter") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Filter requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				cli.filter = std::make_optional(arg);
				continue;
			}
			if (arg == "-p" || arg == "--preset") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Preset requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				const auto preset = parse_uint(arg, "Preset");
				if (not preset.has_value()) return std::unexpected { 1 };
				cli.preset = std::make_optional(std::clamp(preset.value(), 0, 9));
				continue;
			}
			if (arg == "--themes-dir") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Themes directory requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				auto themes_dir = stdfs::path { arg };

				if (not stdfs::is_directory(themes_dir)) {
					error("Themes directory does not exist or is not a directory");
					return std::unexpected { 1 };
				}

				cli.themes_dir = std::make_optional(themes_dir);
				continue;
			}
			if (arg == "-u" || arg == "--update") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Update requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				const auto refresh = parse_uint(arg, "Update");
				if (not refresh.has_value()) return std::unexpected { 1 };
				cli.updates = std::max(refresh.value(), 100);
				continue;
			}
			if (arg == "--json") {
				cli.json_output = true;
				continue;
			}
			if (arg == "-o" || arg == "--output") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Output requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				cli.output_file = std::make_optional(stdfs::path { arg });
				continue;
			}
			if (arg == "-n" || arg == "--iterations") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Iterations requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				const auto iterations = parse_uint(arg, "Iterations");
				if (not iterations.has_value()) return std::unexpected { 1 };
				cli.iterations = std::make_optional(std::max(iterations.value(), 0));
				continue;
			}
			if (arg == "--daemon") {
				cli.daemon = true;
				continue;
			}
			if (arg == "--pidfile") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Pidfile requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				cli.pidfile = std::make_optional(stdfs::path { arg });
				continue;
			}
			if (arg == "--sections") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Sections requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				cli.sections = std::make_optional(arg);
				continue;
			}
			if (arg == "--top-procs") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Top procs requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				const auto top = parse_uint(arg, "Top procs");
				if (not top.has_value()) return std::unexpected { 1 };
				cli.top_procs = std::make_optional(std::max(top.value(), 1));
				continue;
			}
			if (arg == "--pid") {
				// This flag requires an argument.
				if (++it == args.end()) {
					error("Pid requires an argument");
					return std::unexpected { 1 };
				}

				auto arg = *it;
				const auto pid = parse_uint(arg, "Pid");
				if (not pid.has_value()) return std::unexpected { 1 };
				cli.pid = std::make_optional(std::max(pid.value(), 1));
				continue;
			}

			error(fmt::format("Unknown argument '{}{}{}'", YELLOW, arg, RESET));
			return std::unexpected { 1 };
		}

		//? Validate --sections against the known set of sections
		if (cli.sections.has_value()) {
			static constexpr std::array valid_sections = { "cpu"sv, "mem"sv, "net"sv, "proc"sv, "gpu"sv };
			for (const auto section : std::views::split(cli.sections.value(), ',')) {
				const auto name = std::string_view { section };
				if (name.empty()) {
					error("--sections contains an empty entry (expected cpu,mem,net,proc,gpu)");
					return std::unexpected { 1 };
				}
				if (std::ranges::find(valid_sections, name) == valid_sections.end()) {
					error(fmt::format("Unknown section '{}' (valid: cpu, mem, net, proc, gpu)", name));
					return std::unexpected { 1 };
				}
			#ifndef GPU_SUPPORT
				if (name == "gpu"sv) {
					error("Section 'gpu' requires a build with GPU support");
					return std::unexpected { 1 };
				}
			#endif
			}
		}

		//? --output and --iterations are only meaningful with --json
		if (not cli.json_output and (cli.output_file.has_value() or cli.iterations.has_value())) {
			error("Options --output and --iterations can only be used together with --json");
			return std::unexpected { 1 };
		}

		//? Daemon mode requires an output file in json mode (http mode has no output file)
		if (cli.daemon and cli.json_output and not cli.output_file.has_value()) {
			error("--daemon requires an output file (use --output <file>)");
			return std::unexpected { 1 };
		}
		if (cli.daemon and cli.json_output and cli.output_file.has_value() and cli.output_file.value() == "-") {
			error("--daemon can't write to stdout, use --output <file>");
			return std::unexpected { 1 };
		}

		return cli;
	}

	auto default_config() noexcept -> Result {
		// The idea of using `current_config` is that the CLI parser is run before loading the actual config and thus
		// provides default values.
		auto config = Config::current_config();
		
		if (isatty(STDOUT_FILENO)) {
			std::string buffer {};
			// The config buffer ends in `\n`. `std::views::split` will then create an empty element after the last
			// newline, which we would write as an additional empty line at the very end.
			auto trimmed_config = config.substr(0, config.length() - 1);
			for (const auto line : std::views::split(trimmed_config, '\n')) {
				auto line_view = std::string_view { line };
				if (line_view.starts_with("#")) {
					fmt::format_to(
						std::back_inserter(buffer), "{1}{0}{2}\n", line_view, BOLD_BRIGHT_BLACK, RESET
					);
				} else if (!line_view.empty()) {
					auto pos = line_view.find("=");
					if (pos == line_view.npos) {
						error("invalid default config: '=' not found");
						return std::unexpected { 1 };
					}
					auto name = line_view.substr(0, pos);
					auto value = line_view.substr(pos + 1);
					fmt::format_to(
						std::back_inserter(buffer),
						"{2}{0}{4}={3}{1}{4}\n",
						name,
						value,
						BOLD_YELLOW,
						BOLD_GREEN,
						RESET
					);
				} else {
					fmt::format_to(std::back_inserter(buffer), "\n");
				}
			}
			fmt::print("{}", buffer);
		} else {
			fmt::print("{}", config);
		}
		return std::unexpected { 0 };
	}

	void usage() noexcept {
		fmt::println("{0}Usage:{1} {2}btop{1} [OPTIONS]\n", BOLD_UNDERLINE, RESET, BOLD);
	}

	void help() noexcept {
		fmt::print(
			"{0}Options:{1}\n"
			"  {2}-c, --config{1} <file>     Path to a config file\n"
			"  {2}-d, --debug{1}             Start in debug mode with additional logs and metrics\n"
			"  {2}-f, --filter{1} <filter>   Set an initial process filter\n"
			"  {2}    --force-utf{1}         Override automatic UTF locale detection\n"
			"  {2}-l, --low-color{1}         Disable true color, 256 colors only\n"
			"  {2}-p, --preset{1} <id>       Start with a preset (0-9)\n"
			"  {2}-t, --tty{1}               Force tty mode with ANSI graph symbols and 16 colors only\n"
			"  {2}    --themes-dir{1} <dir>  Path to a custom themes directory\n"
			"  {2}    --no-tty{1}            Force disable tty mode\n"
			"  {2}-u, --update{1} <ms>       Set an initial update rate in milliseconds\n"
			"  {2}    --default-config{1}    Print default config to standard output\n"
			"  {2}-h, --help{1}              Show this help message and exit\n"
			"  {2}-V, --version{1}           Show a version message and exit (more with --version)\n"
			"{0}JSON output options:{1}\n"
			"  {2}    --json{1}              Headless mode, output system stats as JSON. Does not require a TTY\n"
			"  {2}-o, --output{1} <file>     Write JSON output to <file> instead of stdout ('-' for stdout)\n"
			"  {2}-n, --iterations{1} <n>    Number of snapshots to write before exiting (0 = run forever, default 1)\n"
			"  {2}    --daemon{1}            Fork and run in the background as a daemon (requires --output)\n"
			"  {2}    --pidfile{1} <path>    Write the daemon PID to <path> (only with --daemon)\n"
			"  {2}    --sections{1} <list>   Comma separated list of sections: cpu,mem,net,proc,gpu (default all)\n"
			"  {2}    --top-procs{1} <n>     Only output the top <n> processes sorted by cpu usage\n"
			"  {2}    --pid{1} <pid>         Include detailed information for process <pid>\n"
			"{0}HTTP server mode:{1}\n"
			"  Headless HTTP server mode is enabled by setting {2}http{1} in the config file\n"
			"  (e.g. {2}~/.config/btop/btop.conf{1}). Run {2}btop --default-config{1} to print all options.\n"
			"  {2}http{1}=[addr:port]    Listen address, empty string disables the server\n"
			"  {2}http_auth{1}=user:pass  Require HTTP basic auth on all endpoints\n",
			BOLD_UNDERLINE, RESET, BOLD
		);
	}

	void help_hint() noexcept {
		fmt::println("For more information, try '{}--help{}'", BOLD, RESET);
	}
} // namespace Cli
