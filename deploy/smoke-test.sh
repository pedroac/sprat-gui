#!/usr/bin/env bash
# Starts a built or packaged sprat-gui with --smoke-test and fails if it does not open.
#
# Usage: deploy/smoke-test.sh <executable> [options]
#   --require-cli                   fail when sprat-cli's transforms or profiles config are missing
#   --smoke-test-zip <zip>          then open this ZIP of images through the GUI and lay it out
#   --smoke-test-profile <name>     layout profile for the ZIP (default: fast)
#   --smoke-test-folder <folder>    folder to tick in the folder picker, relative to the ZIP root;
#                                   repeatable (default: the top-level folders)
#   --smoke-test-expect-sprites <n> fail unless the layout has exactly n sprites
#
# On Linux without a display, the app runs under xvfb-run. Exit codes from the app:
#   2 = main window not shown, 3 = sprat-cli assets missing,
#   4 = ZIP import or layout failed (including any unexpected dialog), 5 = import timed out,
#   6 = the app did not quit within 10 s of the result,
#   124 (timeout) or 142 (perl alarm) = the whole run timed out.
set -uo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <executable> [options]" >&2
    exit 64
fi

executable="$1"
shift
timeout_seconds="${SMOKE_TEST_TIMEOUT:-120}"

if [ ! -e "$executable" ]; then
    echo "[smoke-test] FAIL: $executable does not exist" >&2
    exit 1
fi
# timeout and xvfb-run look up a name without a slash in PATH, not in the current folder.
case "$executable" in
    */*) ;;
    *) executable="./$executable" ;;
esac

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

# Prints the crashed thread's stack from the crash report macOS writes for a crashed app
# (ReportCrash writes it a few seconds after the crash).
print_macos_crash_report() {
    local reports_dir="$HOME/Library/Logs/DiagnosticReports"
    local name report=""
    name="$(basename "$executable")"
    for _ in $(seq 1 30); do
        report="$(find "$reports_dir" -maxdepth 1 -name "$name*.ips" -newer "$started_marker" 2>/dev/null | head -n 1)"
        [ -n "$report" ] && break
        sleep 1
    done
    if [ -z "$report" ]; then
        echo "[smoke-test] no crash report found in $reports_dir" >&2
        return
    fi
    echo "[smoke-test] crash report: $report" >&2
    # A .ips file is a JSON header line followed by a JSON body.
    python3 - "$report" >&2 <<'PYTHON' || cat "$report" >&2
import json, sys
with open(sys.argv[1]) as f:
    f.readline()
    body = json.load(f)
exception = body.get("exception", {})
print("exception:", exception.get("type"), exception.get("signal"), exception.get("subtype", ""))
images = body.get("usedImages", [])
crashed = body.get("faultingThread", 0)
for index, thread in enumerate(body.get("threads", [])):
    if index != crashed and not thread.get("triggered"):
        continue
    print(f"thread {index} ({thread.get('name') or thread.get('queue') or 'unnamed'}):")
    for number, frame in enumerate(thread.get("frames", [])):
        image = images[frame["imageIndex"]].get("name", "?") if frame.get("imageIndex", -1) < len(images) else "?"
        symbol = frame.get("symbol", hex(frame.get("imageOffset", 0)))
        print(f"  {number:2} {image:28} {symbol} + {frame.get('symbolLocation', 0)}")
PYTHON
}

started_marker="$(mktemp)"
echo "[smoke-test] running $executable"
"${runner[@]}" "$executable" --smoke-test "$@"
exit_code=$?

if [ "$exit_code" -ne 0 ]; then
    echo "[smoke-test] FAIL: $executable exited with code $exit_code" >&2
    # 128 + signal number means the app crashed; 142 is perl's alarm ending a hung run.
    if [ "$(uname -s)" = "Darwin" ] && [ "$exit_code" -gt 128 ] && [ "$exit_code" -ne 142 ]; then
        print_macos_crash_report
    fi
fi
rm -f "$started_marker"
exit "$exit_code"
