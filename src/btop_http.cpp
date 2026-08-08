// SPDX-License-Identifier: Apache-2.0

#include "btop_http.hpp"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <fmt/core.h>

#include <iostream>

#include "btop_log.hpp"
#include "btop_shared.hpp"
#include "btop_tools.hpp"

using nlohmann::json;
using std::string;

namespace {

	//? Set by signal handlers for graceful shutdown
	std::atomic_bool g_quit { false };

	void signal_handler(int) {
		g_quit.store(true);
	}

	//? Parse a decimal port string into a 16-bit value. Returns false on malformed or out of range input.
	auto parse_port(const std::string_view value, std::uint16_t& out) -> bool {
		if (value.empty()) return false;
		std::uint32_t port = 0;
		for (const char c : value) {
			if (c < '0' or c > '9') return false;
			port = port * 10 + static_cast<std::uint32_t>(c - '0');
			if (port > 65535) return false;
		}
		out = static_cast<std::uint16_t>(port);
		return true;
	}

	//? Collects snapshots in a single background thread, keeping the latest one cached.
	//? HTTP handlers only ever read the cached snapshot, so the platform collectors are never
	//? touched from more than one thread at a time.
	class Sampler {
	public:
		Sampler(const Json::Options& options, const std::uint32_t update_ms)
			: options_(options), update_ms_(update_ms) {}

		Sampler(const Sampler&) = delete;
		auto operator=(const Sampler&) -> Sampler& = delete;

		~Sampler() { shutdown(); }

		void start() {
			thread_ = std::thread { [this] { run(); } };
		}

		//? Block until a snapshot newer than <since_seq> exists, or until shutdown (returns nullopt).
		auto wait_next(const std::uint64_t since_seq) -> std::optional<std::pair<string, std::uint64_t>> {
			std::unique_lock lock { mtx_ };
			cv_.wait(lock, [this, since_seq] { return quit_ or g_quit.load() or latest_seq_ > since_seq; });
			if (quit_ or g_quit.load()) return std::nullopt;
			return std::make_pair(latest_json_, latest_seq_);
		}

		//? True only if collection failed before any snapshot was ever produced. Once at least one
		//? snapshot exists a transient later failure still lets clients read the latest cached one.
		[[nodiscard]] auto failed() const -> bool {
			std::lock_guard lock { mtx_ };
			return latest_seq_ == 0 and not last_error_.empty();
		}

		//? Human readable description of the last collection error, or an empty string.
		[[nodiscard]] auto error_message() -> string {
			std::lock_guard lock { mtx_ };
			return last_error_;
		}

		void shutdown() {
			{
				std::lock_guard lock { mtx_ };
				if (quit_) return;
				quit_ = true;
			}
			cv_.notify_all();
			if (thread_.joinable()) thread_.join();
		}

	private:
		void run() {
			//? A failed warmup means no deltas can ever be computed; record the error and stop
			//? so /api/json can report it instead of hanging clients.
			try {
				Json::warmup(options_);
			}
			catch (const std::exception& e) {
				Logger::error("Sampler: warmup failed: {}", e.what());
				{
					std::lock_guard lock { mtx_ };
					last_error_ = fmt::format("warmup failed: {}", e.what());
				}
				return;
			}

			//? Sleep in small increments so shutdown stays responsive even with a large update interval.
			while (not quit_ and not g_quit.load()) {
				for (std::uint32_t remaining = update_ms_; remaining > 0; remaining -= std::min(remaining, std::uint32_t { 100 })) {
					Tools::sleep_ms(std::min(remaining, std::uint32_t { 100 }));
					if (quit_ or g_quit.load()) break;
				}
				if (quit_ or g_quit.load()) break;

				//? A transient collection failure must not kill the sampler thread, otherwise every
				//? SSE client would hang forever waiting for a snapshot that never arrives.
				try {
					const string out = Json::snapshot(options_, false);
					{
						std::lock_guard lock { mtx_ };
						latest_json_ = out;
						++latest_seq_;
						last_error_.clear();
					}
					cv_.notify_all();
				}
				catch (const std::exception& e) {
					Logger::error("Sampler: snapshot failed: {}", e.what());
					{
						std::lock_guard lock { mtx_ };
						last_error_ = fmt::format("snapshot failed: {}", e.what());
					}
				}
			}
		}

		Json::Options options_;
		std::uint32_t update_ms_ {};
		std::thread thread_ {};
		mutable std::mutex mtx_ {};
		std::condition_variable cv_ {};
		string latest_json_ {};
		std::uint64_t latest_seq_ = 0;
		string last_error_ {};
		bool quit_ = false;
	};
}

namespace Http {

	auto parse_address(const std::string_view value, Address& out) noexcept -> bool {
		out = Address {};
		const auto colon = value.find_last_of(':');
		std::string_view host;
		std::string_view port;
		if (colon == std::string_view::npos) {
			port = value;
		}
		else {
			host = value.substr(0, colon);
			port = value.substr(colon + 1);
		}
		if (host.empty()) host = "127.0.0.1";
		if (not parse_port(port, out.port)) return false;
		out.host = string { host };
		return true;
	}

	bool daemonize(const std::optional<std::filesystem::path>& pidfile) {
		return Json::daemonize_process(pidfile);
	}

	int run(const Json::Options& options, const Address& address, std::uint32_t update_ms) {
		//? Graceful shutdown on SIGINT/SIGTERM
		std::signal(SIGINT, signal_handler);
		std::signal(SIGTERM, signal_handler);

		Sampler sampler { options, update_ms };
		sampler.start();

		httplib::Server svr;
		svr.set_default_headers({
			{ "Access-Control-Allow-Origin", "*" },
			{ "Access-Control-Allow-Methods", "GET, OPTIONS" },
			{ "Access-Control-Allow-Headers", "Content-Type" },
		});

		//? Endpoint index
		svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
			json root = {
				{ "name", "btop" },
				{ "version", Global::Version },
				{ "uptime", Tools::system_uptime() },
				{ "endpoints", json::array({
					json { { "method", "GET" }, { "path", "/healthz" }, { "description", "Liveness probe" } },
					json { { "method", "GET" }, { "path", "/api/json" }, { "description", "Latest collected snapshot as JSON" } },
					json { { "method", "GET" }, { "path", "/api/stream" }, { "description", "Server-Sent Events stream of snapshots" } },
				}) },
			};
			res.set_content(root.dump(2), "application/json");
		});

		//? Liveness probe
		svr.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
			res.set_content("{\"status\":\"ok\"}", "application/json");
		});

		//? One-shot: latest cached snapshot
		svr.Get("/api/json", [&sampler](const httplib::Request&, httplib::Response& res) {
			//? If collection has failed so far, report the error instead of blocking the request
			//? forever waiting for a snapshot that may never come.
			if (sampler.failed()) {
				res.status = httplib::StatusCode::ServiceUnavailable_503;
				res.set_content(json { { "error", sampler.error_message() } }.dump(), "application/json");
				return;
			}
			const auto snap = sampler.wait_next(0);
			if (not snap.has_value()) {
				res.status = httplib::StatusCode::ServiceUnavailable_503;
				res.set_content("{\"error\":\"no snapshot available\"}", "application/json");
				return;
			}
			res.set_content(snap.value().first, "application/json");
		});

		//? Continuous: SSE stream of snapshots
		svr.Get("/api/stream", [&sampler](const httplib::Request&, httplib::Response& res) {
			res.set_header("Cache-Control", "no-cache");
			res.set_header("Connection", "keep-alive");
			res.set_header("X-Accel-Buffering", "no");
			std::uint64_t last_seq = 0;
			res.set_chunked_content_provider(
				"text/event-stream",
				[&sampler, last_seq](const std::size_t, httplib::DataSink& sink) mutable -> bool {
					const auto snap = sampler.wait_next(last_seq);
					if (not snap.has_value()) return false;
					last_seq = snap.value().second;
					const string msg = "event: snapshot\nid: " + std::to_string(last_seq) + "\ndata: " + snap.value().first + "\n\n";
					return sink.write(msg.data(), msg.size());
				},
				nullptr
			);
		});

		const bool ephemeral = (address.port == 0);
		if (ephemeral) {
			const int actual_port = svr.bind_to_any_port(address.host);
			if (actual_port < 0) {
				fmt::println("error: failed to bind HTTP server to {}:{}", address.host, address.port);
				return 1;
			}
			fmt::println("btop: HTTP server listening on http://{}:{}", address.host, actual_port);
		}
		else {
			if (not svr.bind_to_port(address.host, address.port)) {
				fmt::println("error: failed to bind HTTP server to {}:{}", address.host, address.port);
				return 1;
			}
			fmt::println("btop: HTTP server listening on http://{}:{}", address.host, address.port);
		}
		//? Make the listening address visible immediately, even when stdout is redirected
		std::cout.flush();

		std::thread listen_thread { [&svr] { svr.listen_after_bind(); } };

		//? Wait for shutdown signal, then stop the sampler (which releases blocked SSE handlers)
		//? before stopping the accept loop.
		while (not g_quit.load()) Tools::sleep_ms(100);
		sampler.shutdown();
		svr.stop();
		listen_thread.join();

		return 0;
	}
}
