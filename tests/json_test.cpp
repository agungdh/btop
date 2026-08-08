// SPDX-License-Identifier: Apache-2.0

#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "btop_cli.hpp"
#include "btop_json.hpp"

using nlohmann::json;

TEST(json, proc_serialization_structure) {
	Proc::proc_info proc {};
	proc.pid = 1337;
	proc.name = "bash";
	proc.cmd = "/bin/bash";
	proc.short_cmd = "bash";
	proc.threads = 1;
	proc.user = "root";
	proc.mem = 4096;
	proc.cpu_p = 12.5;
	proc.cpu_c = 99.0;
	proc.state = 'S';
	proc.p_nice = -5;
	proc.ppid = 1;
	proc.cpu_s = 100;
	proc.cpu_t = 200;
	proc.death_time = 0;
	proc.depth = 1;
	proc.collapsed = false;
	proc.filtered = true;

	const auto parsed = json::parse(Json::proc_to_json(proc));
	EXPECT_EQ(parsed["pid"], 1337);
	EXPECT_EQ(parsed["name"], "bash");
	EXPECT_EQ(parsed["cmd"], "/bin/bash");
	EXPECT_EQ(parsed["threads"], 1);
	EXPECT_EQ(parsed["user"], "root");
	EXPECT_EQ(parsed["mem"], 4096);
	EXPECT_DOUBLE_EQ(parsed["cpu_percent"], 12.5);
	EXPECT_DOUBLE_EQ(parsed["cpu_cumulative"], 99.0);
	EXPECT_EQ(parsed["state"], "S");
	EXPECT_EQ(parsed["nice"], -5);
	EXPECT_EQ(parsed["ppid"], 1);
	EXPECT_EQ(parsed["cpu_system"], 100);
	EXPECT_EQ(parsed["cpu_time"], 200);
	EXPECT_EQ(parsed["depth"], 1);
	EXPECT_EQ(parsed["collapsed"], false);
	EXPECT_EQ(parsed["filtered"], true);
}

TEST(json, proc_serialization_escaping) {
	Proc::proc_info proc {};
	proc.pid = 1;
	proc.name = "quo\"te\\back";
	proc.cmd = "line1\nline2\ttab";

	const auto parsed = json::parse(Json::proc_to_json(proc));
	EXPECT_EQ(parsed["name"], "quo\"te\\back");
	EXPECT_EQ(parsed["cmd"], "line1\nline2\ttab");
}

TEST(json, disk_serialization_structure) {
	Mem::disk_info disk {};
	disk.dev = "/dev/sda1";
	disk.name = "root";
	disk.fstype = "ext4";
	disk.total = 1000000;
	disk.used = 250000;
	disk.free = 750000;
	disk.used_percent = 25;
	disk.free_percent = 75;
	disk.io_read = { 1, 2, 3 };
	disk.io_write = { 4, 5 };
	disk.io_activity = { 0, 1 };

	const auto parsed = json::parse(Json::disk_to_json(disk));
	EXPECT_EQ(parsed["dev"], "/dev/sda1");
	EXPECT_EQ(parsed["name"], "root");
	EXPECT_EQ(parsed["fstype"], "ext4");
	EXPECT_EQ(parsed["total"], 1000000);
	EXPECT_EQ(parsed["used"], 250000);
	EXPECT_EQ(parsed["free"], 750000);
	EXPECT_EQ(parsed["used_percent"], 25);
	EXPECT_EQ(parsed["free_percent"], 75);
	EXPECT_EQ(parsed["io_read"], json(std::vector<long long> { 1, 2, 3 }));
	EXPECT_EQ(parsed["io_write"], json(std::vector<long long> { 4, 5 }));
	EXPECT_EQ(parsed["io_activity"], json(std::vector<long long> { 0, 1 }));
}

TEST(json, cli_parse_json_flags) {
	const std::vector<std::string_view> args {
		"--json", "-u", "500", "-n", "3", "-o", "/tmp/out.json", "--sections", "cpu,mem",
		"--top-procs", "10", "--pid", "42", "--daemon", "--pidfile", "/tmp/btop.pid",
	};
	auto result = Cli::parse(args);
	ASSERT_TRUE(result.has_value());
	const auto& cli = result.value();
	EXPECT_TRUE(cli.json_output);
	EXPECT_TRUE(cli.daemon);
	ASSERT_TRUE(cli.updates.has_value());
	EXPECT_EQ(cli.updates.value(), 500);
	ASSERT_TRUE(cli.iterations.has_value());
	EXPECT_EQ(cli.iterations.value(), 3);
	ASSERT_TRUE(cli.output_file.has_value());
	EXPECT_EQ(cli.output_file.value(), std::filesystem::path { "/tmp/out.json" });
	ASSERT_TRUE(cli.pidfile.has_value());
	EXPECT_EQ(cli.pidfile.value(), std::filesystem::path { "/tmp/btop.pid" });
	ASSERT_TRUE(cli.sections.has_value());
	EXPECT_EQ(cli.sections.value(), "cpu,mem");
	ASSERT_TRUE(cli.top_procs.has_value());
	EXPECT_EQ(cli.top_procs.value(), 10);
	ASSERT_TRUE(cli.pid.has_value());
	EXPECT_EQ(cli.pid.value(), 42);
}

TEST(json, cli_parse_json_flags_require_json) {
	for (const auto& args : std::vector<std::vector<std::string_view>> {
		{ "-o", "/tmp/out.json" },
		{ "-n", "5" },
		{ "--sections", "cpu" },
		{ "--top-procs", "10" },
		{ "--pid", "1" },
		{ "--daemon" },
	}) {
		auto result = Cli::parse(args);
		EXPECT_FALSE(result.has_value()) << "expected error for flags without --json";
		EXPECT_NE(result.error(), 0);
	}
}

TEST(json, cli_parse_daemon_requires_output) {
	{
		const std::vector<std::string_view> args { "--json", "--daemon" };
		auto result = Cli::parse(args);
		EXPECT_FALSE(result.has_value());
		EXPECT_NE(result.error(), 0);
	}
	{
		const std::vector<std::string_view> args { "--json", "--daemon", "-o", "-" };
		auto result = Cli::parse(args);
		EXPECT_FALSE(result.has_value());
		EXPECT_NE(result.error(), 0);
	}
}
