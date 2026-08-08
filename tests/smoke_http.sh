#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Smoke test for the headless HTTP server mode: starts btop on an ephemeral port,
# checks the /healthz, /api/json and /api/stream endpoints, then verifies graceful
# shutdown on SIGTERM.

set -u

BTOP="${1:?usage: smoke_http.sh /path/to/btop}"
LOG="$(mktemp)"
CFG_DIR="$(mktemp -d)"
PID=""

cleanup() {
	if [ -n "$PID" ]; then
		kill -9 "$PID" 2>/dev/null || true
	fi
	rm -rf "$LOG" "$CFG_DIR"
}

trap cleanup EXIT

fail() {
	echo "FAIL: $1" >&2
	exit 1
}

# Isolate config so the test doesn't touch the user's real btop config
export XDG_CONFIG_HOME="$CFG_DIR"
mkdir -p "$CFG_DIR"

"$BTOP" --http 127.0.0.1:0 -u 200 --sections cpu,mem >"$LOG" 2>&1 &
PID=$!

# Wait for the "listening on" line
for _ in $(seq 1 100); do
	if grep -q "listening on" "$LOG"; then
		break
	fi
	if ! kill -0 "$PID" 2>/dev/null; then
		cat "$LOG" >&2
		fail "btop exited before reporting a listening address"
	fi
	sleep 0.1
done
if ! grep -q "listening on" "$LOG"; then
	cat "$LOG" >&2
	fail "btop never reported a listening address"
fi

PORT="$(sed -n 's/.*listening on http:\/\/[^:]*:\([0-9]*\).*/\1/p' "$LOG")"
if [ -z "$PORT" ]; then
	cat "$LOG" >&2
	fail "could not parse the listening port"
fi
BASE="http://127.0.0.1:$PORT"

# /healthz
HEALTH="$(curl -fsS "$BASE/healthz")" || fail "GET /healthz failed"
[ "$HEALTH" = '{"status":"ok"}' ] || fail "unexpected /healthz body: $HEALTH"

# /api/json: must be valid JSON containing the requested sections only
JSON="$(curl -fsS "$BASE/api/json")" || fail "GET /api/json failed"
echo "$JSON" | grep -q '"meta"' || fail "/api/json missing meta"
echo "$JSON" | grep -q '"cpu"' || fail "/api/json missing cpu"
echo "$JSON" | grep -q '"mem"' || fail "/api/json missing mem"
echo "$JSON" | grep -q '"proc"' && fail "/api/json should not include proc"

# /api/stream: the first snapshot event must arrive
EVENT="$(curl -sN --max-time 10 "$BASE/api/stream" 2>/dev/null | head -c 400)"
echo "$EVENT" | grep -q "event: snapshot" || fail "/api/stream missing snapshot event"

# Graceful shutdown on SIGTERM
kill -TERM "$PID" 2>/dev/null || fail "could not send SIGTERM"
for _ in $(seq 1 50); do
	if ! kill -0 "$PID" 2>/dev/null; then
		break
	fi
	sleep 0.1
done
if kill -0 "$PID" 2>/dev/null; then
	fail "btop did not exit after SIGTERM"
fi
wait "$PID"
CODE=$?
[ "$CODE" -eq 0 ] || fail "btop exited with code $CODE"

echo "PASS: HTTP smoke test"
