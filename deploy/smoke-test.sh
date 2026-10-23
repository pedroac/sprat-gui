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
# On Linux without a display, the app runs under xvfb-run. On an Apple Silicon Mac, a universal
# app runs twice: natively, then its x86_64 slice under Rosetta (which must be installed).
# Exit codes from the app:
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
# $1 is a file created just before the app started.
print_macos_crash_report() {
    local started_marker="$1"
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

# Runs the app once and reports a failure. $1 is the slice to run (an arch(1) architecture),
# or empty for the default; the rest are the app's options.
run_app() {
    local slice="$1"
    shift
    local launcher=() started_marker="" exit_code
    [ -n "$slice" ] && launcher=(arch "-$slice")
    [ "$(uname -s)" = "Darwin" ] && started_marker="$(mktemp)"

    echo "[smoke-test] running $executable${slice:+ ($slice)}"
    # ${a[@]+...} because macOS's bash 3.2 treats an empty array as unset under set -u.
    "${runner[@]}" ${launcher[@]+"${launcher[@]}"} "$executable" --smoke-test "$@"
    exit_code=$?

    if [ "$exit_code" -ne 0 ]; then
        echo "[smoke-test] FAIL: $executable${slice:+ ($slice)} exited with code $exit_code" >&2
        # 128 + signal number means the app crashed; 142 is perl's alarm ending a hung run.
        if [ -n "$started_marker" ] && [ "$exit_code" -gt 128 ] && [ "$exit_code" -ne 142 ]; then
            print_macos_crash_report "$started_marker"
        fi
    fi
    [ -n "$started_marker" ] && rm -f "$started_marker"
    return "$exit_code"
}

slices=("")
if [ "$(uname -s)" = "Darwin" ] && [ "$(sysctl -n hw.optional.arm64 2>/dev/null)" = "1" ]; then
    archs=" $(lipo -archs "$executable" 2>/dev/null) "
    if [[ "$archs" == *" arm64 "* && "$archs" == *" x86_64 "* ]]; then
        slices=(arm64 x86_64)
    fi
fi

for slice in "${slices[@]}"; do
    run_app "$slice" "$@" || exit $?
done
