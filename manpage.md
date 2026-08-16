% btop(1) | User Commands
%
% 2025-05-01

# NAME

btop - Resource monitor that shows usage and stats for processor, memory, disks, network, and processes.

# SYNOPSIS

**btop** [**-c** _file_] [**-d**] [**-f** _filter_] [**-l**] [**-p** _id_] [**-t**] [**-u** _ms_] [**\-\-force-utf**] [**\-\-themes-dir** _dir_]

**btop** [**\-\-json** [**-o** _file_] [**-n** _count_] [**\-\-sections** _list_] [**\-\-top-procs** _n_] [**\-\-pid** _pid_] [**\-\-daemon**] [**\-\-pidfile** _path_]]

**btop** [**\-\-http** [_addr:port_] [**\-\-sections** _list_] [**\-\-top-procs** _n_] [**\-\-pid** _pid_] [**\-\-daemon**] [**\-\-pidfile** _path_]]

**btop** [**\-\-default-config** | {**-h** | **\-\-help**} | {**-V** | **\-\-version**}]

# DESCRIPTION

**btop** is a program that shows usage and stats for processor, memory, disks, network, and processes.

# OPTIONS

The program follows the usual GNU command line syntax, with long options
starting with two dashes ('-'). A summary of options is included below.

**-c**, **\-\-config _file_**
:   Path to a config file.

**-d**, **\-\-debug**
:   Start in debug mode with additional logs and metrics.

**-f**, **\-\-filter _filter_**
:   Set an initial process filter.

**\-\-force-utf**
:   Force start even if no UTF-8 locale was detected.

**-l**, **\-\-low-color**
:   Disable true color, 256 colors only.

**-p**, **\-\-preset _id_**
:   Start with a preset (0-9).

**-t**, **\-\-tty**
:   Force tty mode with ANSI graph symbols and 16 colors only.

**\-\-no-tty**
:   Force disable tty mode.

**\-\-themes-dir _dir_**
:   Path to a custom themes directory. When specified, this directory takes priority over the default theme search paths.

**-u**, **\-\-update _ms_**
:   Set an initial update rate in milliseconds.

**\-\-default-config**
:   Print default config to standard output.

# JSON OUTPUT OPTIONS

The following options are only valid together with **\-\-json** and run **btop** in a headless
mode that does not require a terminal. Data is collected with the same collectors as the TUI
and serialized as JSON. A single snapshot waits one update interval (default 2000 ms, see **-u**)
so that usage deltas (cpu percent, network bandwidth, process cpu) are accurate, like **iostat**(1).
When more than one snapshot is written the output is compact newline-delimited JSON (one snapshot
per line). Daemon mode rewrites the output file with the latest snapshot on every update.

**\-\-json**
:   Headless mode, output system stats as JSON. Does not require a TTY.

**-o**, **\-\-output _file_**
:   Write JSON output to _file_ instead of standard output ('-' for standard output).

**-n**, **\-\-iterations _count_**
:   Number of snapshots to write before exiting. 0 runs forever. Default is 1.

**\-\-daemon**
:   Fork and run in the background as a daemon. Requires **\-\-output**. The daemon runs forever
    and rewrites the output file on every update. Kill it with **SIGTERM** or **SIGINT** for a
    clean shutdown.

**\-\-pidfile _path_**
:   Write the daemon's PID to _path_. Only used with **\-\-daemon**.

**\-\-sections _list_**
:   Comma separated list of sections to collect and output: cpu, mem, net, proc, gpu. Default is
    all available sections.

**\-\-top-procs _n_**
:   Only output the top _n_ processes, sorted by cpu usage.

**\-\-pid _pid_**
:   Include detailed information for the process with the given pid.

# HTTP SERVER MODE

HTTP server mode runs **btop** as a headless HTTP server that does not require a terminal. It is
enabled by setting **http** in the btop config file (for example `~/.config/btop/btop.conf`). The
same collectors as the TUI and the JSON mode run in a background thread, collecting a snapshot
every update interval (default 2000 ms, see **-u**). The latest snapshot is served at
`GET /api/json` (one-shot). All responses include `Access-Control-Allow-Origin: *`. Kill the
server with **SIGTERM** or **SIGINT** for a clean shutdown.

**http=_addr_:port**
:   Start the HTTP server, defaulting to `127.0.0.1:8080`. The value may be `host:port`, `:port`
    or just `port`; a missing host defaults to `127.0.0.1`. Port `0` picks a free ephemeral port
    and prints the chosen address to standard output. An empty string (the default) disables the
    HTTP server.

**http_auth=_user_:password**
:   Require HTTP basic auth credentials for every endpoint. Only used together with **http**.

**monitor_enabled=_true|false_**
:   Enable threshold monitoring (default _false_). When enabled, **btop** records events to a SQLite
    database whenever a monitored metric exceeds its configured threshold and records a matching
    "resolved" event once the metric drops back below. The database runs in WAL mode with
    `synchronous=NORMAL`. Only used together with **http**.

**monitor_db=_path_**
:   Path to the SQLite database used for threshold events. An empty string (the default) uses
    `~/.local/state/btop/btop.db`.

**monitor_cpu=_percent_**
:   CPU usage threshold in percent of total CPU time. `0` (the default) disables CPU monitoring.

**monitor_mem=_percent_**
:   RAM usage threshold in percent of total memory. `0` (the default) disables memory monitoring.

**monitor_disk=_percent_**
:   Disk usage threshold in percent of used space, checked per mountpoint. `0` (the default)
    disables disk monitoring.

**monitor_net=_mbit/s_**
:   Network bandwidth threshold in Mbit/s, checked per interface as download+upload combined.
    `0` (the default) disables network monitoring.

**monitor_debounce=_samples_**
:   Number of consecutive samples that must exceed (or drop below) a threshold before an event is
    recorded, preventing flapping around the threshold. Default `3`.

**\-\-sections _list_**
:   Comma separated list of sections to collect and serve: cpu, mem, net, proc, gpu. Default is
    all available sections.

**\-\-top-procs _n_**
:   Only output the top _n_ processes, sorted by cpu usage.

**\-\-pid _pid_**
:   Include detailed information for the process with the given pid.

**\-\-daemon**
:   Fork and run in the background as a daemon. Does not require **\-\-output**.

**\-\-pidfile _path_**
:   Write the daemon's PID to _path_. Only used with **\-\-daemon**.

**-h**, **\-\-help**
:   Show summary of options.

**-V**, **\-\-version**
:   Show version of program.

# BUGS

The upstream bug tracker can be found at https://github.com/aristocratos/btop/issues.

# SEE ALSO

**top**(1), **htop**(1)

# AUTHOR

**btop** was written by Jakob P. Liljenberg a.k.a. "Aristocratos".
