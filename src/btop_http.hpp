// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "btop_json.hpp"

//* Headless HTTP server mode, serves collected system stats as JSON (one-shot)
//* without requiring a terminal.
namespace Http {

	//* Listen address: host to bind to and TCP port.
	struct Address {
		std::string host = "127.0.0.1";
		std::uint16_t port = 8080;
	};

	//* HTTP basic auth credentials required from every client.
	struct BasicAuth {
		std::string username;
		std::string password;
	};

	//* Parse an "[addr:]port" CLI value. Accepts "host:port", ":port" and "port" forms.
	//* An empty host (":port" / "port") defaults to 127.0.0.1. Port 0 selects an ephemeral port.
	//* Returns false on malformed input.
	[[nodiscard]] auto parse_address(const std::string_view value, Address& out) noexcept -> bool;

	//* Parse a "user:password" CLI value into <out>. Returns false when either part is empty or
	//* the colon separator is missing.
	[[nodiscard]] auto parse_basic_auth(const std::string_view value, BasicAuth& out) noexcept -> bool;

	//* Fork and detach from the terminal, writing the pidfile if requested. Unlike Json::daemonize
	//* no output file is required. Must be called before Shared::init() so forked processes don't
	//* inherit thread/async state.
	bool daemonize(const std::optional<std::filesystem::path>& pidfile);

	//* Run the headless HTTP server: a background thread collects snapshots at <update_ms> intervals
	//* while HTTP handlers serve the latest cached snapshot. If <auth> is set, every request must
	//* carry the matching HTTP basic auth credentials.
	int run(const Json::Options& options, const Address& address, std::uint32_t update_ms, const std::optional<BasicAuth>& auth = std::nullopt);
}
