#!/usr/bin/env bash
# Starts a built or packaged sprat-gui with --smoke-test and fails if it does not open.
#
# Usage: deploy/smoke-test.sh <executable> [--require-cli]
#   --require-cli  also fail when the sprat-cli tools, transforms or profiles config are missing
#
# On Linux without a display, the app runs under xvfb-run. Exit codes from the app:
#   2 = main window not shown, 3 = sprat-cli assets missing,
#   124 (timeout) or 142 (perl alarm) = timed out.
set -uo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <executable> [--require-cli]" >&2
    exit 64
fi

executable="$1"
shift
timeout_seconds="${SMOKE_TEST_TIMEOUT:-120}"

if [ ! -e "$executable" ]; then
    echo "[smoke-test] FAIL: $executable does not exist" >&2
    exit 1
fi

runner=()
if [ "$(uname -s)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    runner=(xvfb-run -a)
fi
# macOS has no timeout(1); perl's alarm survives the exec and kills a hung app.
if command -v timeout >/dev/null 2>&1; then
    runner+=(timeout "$timeout_seconds")
else
    runner+=(perl -e 'alarm shift; exec @ARGV or die "exec failed: $!\n"' "$timeout_seconds")
fi

echo "[smoke-test] running $executable"
"${runner[@]}" "$executable" --smoke-test "$@"
exit_code=$?

if [ "$exit_code" -ne 0 ]; then
    echo "[smoke-test] FAIL: $executable exited with code $exit_code" >&2
fi
exit "$exit_code"
