// SPDX-License-Identifier: Apache-2.0

#pragma once

//* Threshold monitoring for HTTP server mode. Records an event to a SQLite
//* database (WAL mode, synchronous=NORMAL) whenever a monitored metric exceeds
//* its configured threshold, and a matching "resolved" event once it drops back
//* below. Debounced with a configurable number of consecutive samples.
namespace Monitor {

	//* Open the database and create the schema. Reads the monitor_* config values,
	//* and does nothing when monitor_enabled is false or the database can't be opened.
	void init();

	//* Compare the latest collected metrics against the configured thresholds and
	//* record triggered/resolved events. No-op until init() has opened a database.
	void evaluate();

	//* Close the database. Must not run concurrently with evaluate().
	void shutdown();
}
