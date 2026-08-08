% btop(1) | User Commands
%
% 2025-05-01

# NAME

btop - Resource monitor that shows usage and stats for processor, memory, disks, network, and processes.

# SYNOPSIS

**btop** [**-c** _file_] [**-d**] [**-f** _filter_] [**-l**] [**-p** _id_] [**-t**] [**-u** _ms_] [**\-\-force-utf**] [**\-\-themes-dir** _dir_]

**btop** [**\-\-json** [**-o** _file_] [**-n** _count_] [**\-\-sections** _list_] [**\-\-top-procs** _n_] [**\-\-pid** _pid_] [**\-\-daemon**] [**\-\-pidfile** _path_]]

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
