#!/usr/bin/env bash
#
# Builds sprat-cli and sprat-gui from source on Linux or macOS.
#
# Usage:
#   bash build-from-source.sh
#
# Everything goes into a "sprat" folder created in the current directory:
#
#   sprat/
#     sprat-cli/        sprat-cli source (git clone)
#     sprat-gui/        sprat-gui source (git clone)
#     bin/              spratlayout, spratpack, ..., spratprofiles.cfg, transforms/
#                       and, on Linux, sprat-gui
#     sprat-gui.app     macOS only
#     .build/           build trees and logs
#
# Run it again at any time to update: it pulls both repositories and rebuilds.
#
# Environment variables:
#   SPRAT_DIR          Folder to use instead of ./sprat
#   SPRAT_BUILD_JOBS   Number of parallel compile jobs (default: based on CPUs and memory)
#   SPRAT_ALLOW_ROOT   Set to 1 to allow running as root
#
# Keep this script compatible with bash 3.2, the version macOS ships.

set -euo pipefail

readonly CLI_REPO_URL="https://github.com/pedroac/sprat-cli.git"
readonly GUI_REPO_URL="https://github.com/pedroac/sprat-gui.git"
readonly MIN_CMAKE_VERSION="3.16"
readonly CLI_TOOLS="spratlayout spratpack spratconvert spratframes spratunpack"
readonly TOTAL_STEPS=7

# --- Output helpers -----------------------------------------------------------

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'; C_RED=$'\033[31m'; C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'; C_BLUE=$'\033[34m'; C_RESET=$'\033[0m'
else
    C_BOLD=""; C_DIM=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_BLUE=""; C_RESET=""
fi

case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
    *UTF-8*|*utf8*|*UTF8*|*utf-8*) MARK_OK="✓"; MARK_FAIL="✗" ;;
    *) MARK_OK="ok"; MARK_FAIL="x" ;;
esac
MARK_WARN="!"

step_number=0
step() {
    step_number=$((step_number + 1))
    printf '\n%s==> [%d/%d] %s%s\n' "$C_BOLD$C_BLUE" "$step_number" "$TOTAL_STEPS" "$1" "$C_RESET"
}
ok()   { printf '  %s%s%s %s\n' "$C_GREEN" "$MARK_OK" "$C_RESET" "$1"; }
fail() { printf '  %s%s%s %s\n' "$C_RED" "$MARK_FAIL" "$C_RESET" "$1"; }
warn() { printf '  %s%s%s %s\n' "$C_YELLOW" "$MARK_WARN" "$C_RESET" "$1"; }
info() { printf '    %s\n' "$1"; }
hint() { printf '    %s%s%s\n' "$C_DIM" "$1" "$C_RESET"; }

# Prints an error with optional hint lines and stops the script.
die() {
    printf '\n%sError:%s %s\n' "$C_BOLD$C_RED" "$C_RESET" "$1" >&2
    shift
    for line in "$@"; do
        printf '  %s\n' "$line" >&2
    done
    printf '\n' >&2
    exit 1
}

# Runs a command, showing its output and appending it to $log_file
# (set by the caller).
run_logged() {
    printf '\n$ %s\n' "$*" >> "$log_file"
    "$@" 2>&1 | tee -a "$log_file"
}

have() { command -v "$1" > /dev/null 2>&1; }

# True when version $1 >= version $2 (dotted numbers).
version_at_least() {
    local IFS=.
    # shellcheck disable=SC2206
    local a=($1) b=($2) i
    for i in 0 1 2; do
        local x="${a[$i]:-0}" y="${b[$i]:-0}"
        x="${x%%[!0-9]*}"; y="${y%%[!0-9]*}"
        [ "${x:-0}" -gt "${y:-0}" ] && return 0
        [ "${x:-0}" -lt "${y:-0}" ] && return 1
    done
    return 0
}

# --- Platform -----------------------------------------------------------------

case "$(uname -s)" in
    Linux)  OS="linux" ;;
    Darwin) OS="macos" ;;
    *) die "This script supports Linux and macOS only (detected: $(uname -s))." \
           "On Windows, download a release from https://github.com/pedroac/sprat-gui/releases" ;;
esac

if [ "$(id -u)" -eq 0 ] && [ "${SPRAT_ALLOW_ROOT:-0}" != "1" ]; then
    die "Please don't run this script as root or with sudo." \
        "It builds in your current folder and configures sprat-gui for the current user." \
        "If you really mean it, run it again with SPRAT_ALLOW_ROOT=1."
fi

# Detects the package manager used to suggest install commands.
detect_package_manager() {
    if [ "$OS" = "macos" ]; then
        echo "brew"
    elif have apt-get; then echo "apt"
    elif have dnf;     then echo "dnf"
    elif have pacman;  then echo "pacman"
    elif have zypper;  then echo "zypper"
    else echo "unknown"
    fi
}
PKG_MANAGER="$(detect_package_manager)"

# Maps a dependency name to the package(s) that provide it.
packages_for() {
    local dep="$1"
    case "$PKG_MANAGER:$dep" in
        brew:git|brew:compiler|brew:build-tool) ;;  # Command Line Tools, not Homebrew
        brew:pkg-config)   echo "pkgconf" ;;
        brew:libarchive)   echo "libarchive" ;;
        brew:qt6)          echo "qt" ;;
        apt:compiler)      echo "build-essential" ;;
        apt:libarchive)    echo "libarchive-dev" ;;
        apt:qt6)           echo "qt6-base-dev qt6-svg-dev libgl1-mesa-dev" ;;
        dnf:compiler|zypper:compiler) echo "gcc-c++" ;;
        dnf:pkg-config)    echo "pkgconf-pkg-config" ;;
        dnf:libarchive|zypper:libarchive) echo "libarchive-devel" ;;
        dnf:qt6)           echo "qt6-qtbase-devel qt6-qtsvg-devel" ;;
        zypper:qt6)        echo "qt6-base-devel qt6-svg-devel" ;;
        pacman:compiler)   echo "base-devel" ;;
        pacman:pkg-config) echo "pkgconf" ;;
        pacman:libarchive) echo "libarchive" ;;
        pacman:qt6)        echo "qt6-base qt6-svg" ;;
        unknown:*) ;;
        *:build-tool)      echo "make" ;;
        *:git|*:cmake|*:pkg-config) echo "$dep" ;;
    esac
}

install_command_for() {
    local packages="$1"
    case "$PKG_MANAGER" in
        apt)    echo "sudo apt-get install -y $packages" ;;
        dnf)    echo "sudo dnf install -y $packages" ;;
        pacman) echo "sudo pacman -S --needed $packages" ;;
        zypper) echo "sudo zypper install -y $packages" ;;
        brew)   echo "brew install $packages" ;;
    esac
}

# --- Folder -------------------------------------------------------------------

SPRAT_DIR="${SPRAT_DIR:-$PWD/sprat}"
case "$SPRAT_DIR" in
    /*) ;;
    *) SPRAT_DIR="$PWD/$SPRAT_DIR" ;;
esac
BIN_DIR="$SPRAT_DIR/bin"
WORK_DIR="$SPRAT_DIR/.build"
LOG_DIR="$WORK_DIR/logs"

printf '%sSprat build from source%s (%s)\n' "$C_BOLD" "$C_RESET" "$OS"
printf 'This builds sprat-cli and sprat-gui into: %s%s%s\n' "$C_BOLD" "$SPRAT_DIR" "$C_RESET"

# --- Step 1: Git --------------------------------------------------------------

step "Checking Git"

macos_tools_installed=1
if [ "$OS" = "macos" ]; then
    # On a Mac without the Command Line Tools, /usr/bin/git is a stub: running it
    # opens the system dialog that installs them (Git, compilers, make).
    if ! xcode-select -p > /dev/null 2>&1; then
        macos_tools_installed=0
        info "The Xcode Command Line Tools (Git, C++ compiler) aren't installed yet."
        info "macOS should now open a window offering to install them."
        git --version > /dev/null 2>&1 || true
    elif case "$({ xcrun clang --version; git --version; } 2>&1 || true)" in
             *[Ll]icense*) true ;; *) false ;; esac; then
        # With Xcode selected, Git and the compiler refuse to run until its license is accepted.
        fail "The Xcode license hasn't been accepted yet, so Git and the compiler can't run."
        die "Accept the Xcode license with:" \
            "  sudo xcodebuild -license accept" \
            "Then run this script again."
    fi
fi

git_output=""
if have git; then
    git_output="$(git --version 2>&1 || true)"
fi
case "$git_output" in
    "git version"*) ok "$git_output" ;;
    *)
        if [ "$OS" = "macos" ]; then
            if [ "$macos_tools_installed" = "0" ]; then
                fail "Git is not installed."
                die "Git is required." \
                    "Finish installing the Command Line Tools from the window that just opened," \
                    "or run:  xcode-select --install" \
                    "Then run this script again."
            fi
            fail "Git is installed but doesn't run: $(echo "$git_output" | head -n 1)"
            die "Git is required." \
                "Reinstall the Command Line Tools:" \
                "  sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install" \
                "Then run this script again."
        fi
        fail "Git is not installed."
        cmd="$(install_command_for "$(packages_for git)")"
        if [ -n "$cmd" ]; then
            die "Git is required. Install it with:" "  $cmd" "Then run this script again."
        fi
        die "Git is required. Install it with your system's package manager, then run this script again."
        ;;
esac

# --- Step 2: Build dependencies -----------------------------------------------

step "Checking build dependencies"

missing=""
add_missing() { missing="$missing $1"; }

# Homebrew keeps libarchive (and sometimes Qt) out of the default search paths.
if [ "$OS" = "macos" ]; then
    if have brew; then
        ok "Homebrew $(brew --version 2> /dev/null | head -n 1 | sed 's/^Homebrew //')"
        brew_prefix="$(brew --prefix)"
        for formula in libarchive qt; do
            prefix="$brew_prefix/opt/$formula"
            if [ -d "$prefix" ]; then
                export CMAKE_PREFIX_PATH="$prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
                export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
            fi
        done
    else
        fail "Homebrew is not installed."
        die "Homebrew is needed to install the build dependencies (CMake, Qt, libarchive)." \
            "Install it by following the instructions on https://brew.sh" \
            "Then run this script again."
    fi
fi

# C++20 compiler
CXX_BIN=""
for candidate in "${CXX:-}" c++ g++ clang++; do
    if [ -n "$candidate" ] && have "$candidate"; then
        CXX_BIN="$(command -v "$candidate")"
        break
    fi
done
probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/sprat-probe.XXXXXX")"
trap 'rm -rf "$probe_dir"' EXIT
if [ -z "$CXX_BIN" ]; then
    fail "No C++ compiler found."
    add_missing compiler
else
    cat > "$probe_dir/probe.cpp" << 'EOF'
#include <concepts>
#include <span>
int main() { int v[1] = {0}; std::span<int> s(v); return static_cast<int>(s.size()) - 1; }
EOF
    cxx_name="$("$CXX_BIN" --version 2> /dev/null | head -n 1)"
    if "$CXX_BIN" -std=c++20 "$probe_dir/probe.cpp" -o "$probe_dir/probe" > "$probe_dir/cxx.log" 2>&1; then
        ok "C++20 compiler: $cxx_name"
        export CXX="$CXX_BIN"
    else
        fail "$cxx_name can't compile C++20; a newer compiler is needed."
        cxx_error="$(grep -m 1 -i "error" "$probe_dir/cxx.log" || true)"
        [ -n "$cxx_error" ] && hint "$cxx_error"
        add_missing compiler
    fi
fi

# CMake
if have cmake; then
    cmake_version="$(cmake --version | head -n 1 | sed 's/[^0-9.]*\([0-9.]*\).*/\1/')"
    if version_at_least "$cmake_version" "$MIN_CMAKE_VERSION"; then
        ok "CMake $cmake_version"
    else
        fail "CMake $cmake_version is too old; $MIN_CMAKE_VERSION or newer is needed."
        add_missing cmake
    fi
else
    fail "CMake is not installed."
    add_missing cmake
fi

# Build tool: Ninja when available (faster), otherwise make
CMAKE_GENERATOR_NAME=""
if have ninja; then
    CMAKE_GENERATOR_NAME="Ninja"
    ok "Ninja $(ninja --version)"
elif have make; then
    CMAKE_GENERATOR_NAME="Unix Makefiles"
    ok "make"
else
    fail "Neither make nor ninja is installed."
    add_missing build-tool
fi

# pkg-config and libarchive
if have pkg-config; then
    ok "pkg-config $(pkg-config --version)"
    if pkg-config --exists libarchive; then
        ok "libarchive $(pkg-config --modversion libarchive)"
    else
        fail "libarchive development files not found."
        add_missing libarchive
    fi
else
    fail "pkg-config is not installed."
    add_missing pkg-config
    add_missing libarchive
fi

# Qt 6: ask CMake, exactly as the real build will. After a successful build, its
# CMake cache already says where Qt is, which saves a slow configure on updates.
gui_cache="$WORK_DIR/sprat-gui/CMakeCache.txt"
cached_qt_dir="$(sed -n 's/^Qt6_DIR:PATH=//p' "$gui_cache" 2> /dev/null || true)"
cached_qt_svg_dir="$(sed -n 's/^Qt6Svg_DIR:PATH=//p' "$gui_cache" 2> /dev/null || true)"
if [ -n "$cached_qt_dir" ] && [ -d "$cached_qt_dir" ] && [ -n "$cached_qt_svg_dir" ] && [ -d "$cached_qt_svg_dir" ]; then
    ok "Qt 6 (as found by the previous build: $cached_qt_dir)"
elif have cmake && [ -n "${CXX:-}" ] && [ -n "$CMAKE_GENERATOR_NAME" ]; then
    mkdir -p "$probe_dir/qt"
    cat > "$probe_dir/qt/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(sprat_qt_probe CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg)
file(WRITE "${CMAKE_BINARY_DIR}/qt-version.txt" "${Qt6_VERSION}")
EOF
    if cmake -S "$probe_dir/qt" -B "$probe_dir/qt/build" -G "$CMAKE_GENERATOR_NAME" \
            > "$probe_dir/qt.log" 2>&1; then
        ok "Qt $(cat "$probe_dir/qt/build/qt-version.txt") (Core, Gui, Widgets, Concurrent, Network, Svg)"
    else
        fail "Qt 6 development files not found (needs Core, Gui, Widgets, Concurrent, Network, Svg)."
        add_missing qt6
    fi
else
    warn "Skipped the Qt 6 check until CMake, a compiler and a build tool are available."
fi

if [ -n "$missing" ]; then
    packages=""
    for dep in $missing; do
        packages="$packages $(packages_for "$dep")"
    done
    packages="$(echo $packages)"  # trim and collapse spaces
    printf '\n%sSome dependencies are missing.%s\n' "$C_BOLD$C_YELLOW" "$C_RESET"
    if [ "$OS" = "macos" ] && case "$missing" in *compiler*) true ;; *) false ;; esac; then
        # xcode-select --install refuses to run when the tools are already
        # installed, so outdated tools need an update or a reinstall instead.
        case "$(xcode-select -p 2> /dev/null)" in
            "")
                # Not installed: Git passed step 1 only because Homebrew provides it.
                info "Install the Xcode Command Line Tools (they include the C++ compiler):"
                printf '\n      %sxcode-select --install%s\n\n' "$C_BOLD" "$C_RESET"
                ;;
            *Xcode*.app/*)
                info "Your Xcode is too old. Update it from the App Store."
                hint "Older macOS versions may not offer an Xcode new enough for C++20."
                ;;
            *)
                info "Your Command Line Tools are too old. Update them in"
                info "System Settings > General > Software Update, or reinstall them with:"
                printf '\n      %ssudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install%s\n\n' \
                    "$C_BOLD" "$C_RESET"
                hint "Older macOS versions may not offer tools new enough for C++20."
                ;;
        esac
    fi
    if [ -n "$packages" ]; then
        info "Install them with:"
        printf '\n      %s%s%s\n\n' "$C_BOLD" "$(install_command_for "$packages")" "$C_RESET"
    elif [ "$PKG_MANAGER" = "unknown" ]; then
        info "Install them with your system's package manager:"
        info "  a C++20 compiler, CMake $MIN_CMAKE_VERSION+, make, pkg-config,"
        info "  libarchive (development files) and Qt 6 (base and svg development files)."
    fi
    if [ "$PKG_MANAGER" = "apt" ] && case "$missing" in *qt6*) true ;; *) false ;; esac; then
        hint "On Ubuntu 22.04 and older, use libqt6svg6-dev instead of qt6-svg-dev."
    fi
    info "Then run this script again."
    exit 1
fi

# --- Step 3: Folder -----------------------------------------------------------

step "Preparing $SPRAT_DIR"

if [ -e "$SPRAT_DIR" ] && [ ! -d "$SPRAT_DIR" ]; then
    die "$SPRAT_DIR exists but is not a folder." \
        "Rename or remove it, or run the script from another folder."
fi
if [ -d "$SPRAT_DIR" ]; then
    ok "Using the existing folder."
else
    mkdir -p "$SPRAT_DIR" || die "Couldn't create $SPRAT_DIR."
    ok "Created the folder."
fi
mkdir -p "$BIN_DIR" "$LOG_DIR" || die "Couldn't create folders inside $SPRAT_DIR."

# --- Step 4: Source code ------------------------------------------------------

step "Getting the source code"

# Clones the repository into $2, or updates it when it's already there.
fetch_repo() {
    local url="$1" dir="$2" name log_file
    name="$(basename "$dir")"
    log_file="$LOG_DIR/git-$name.log"
    : > "$log_file"

    if [ -d "$dir/.git" ]; then
        info "Updating $name..."
        if ! run_logged git -C "$dir" pull --ff-only; then
            fail "Couldn't update $name."
            die "git pull failed in $dir." \
                "Common causes: no internet connection, local changes, or a branch that diverged." \
                "Check with:  git -C \"$dir\" status" \
                "Log: $log_file"
        fi
        ok "$name is up to date ($(git -C "$dir" log -1 --format='%h %s' | cut -c1-60))."
    elif [ -e "$dir" ]; then
        die "$dir exists but is not a Git repository." \
            "Move or remove it so the script can clone $name there."
    else
        info "Cloning $name..."
        if ! run_logged git clone "$url" "$dir"; then
            rm -rf "$dir"
            fail "Couldn't clone $name."
            die "git clone failed for $url." \
                "Check your internet connection and try again." \
                "Log: $log_file"
        fi
        ok "Cloned $name."
    fi
}

fetch_repo "$CLI_REPO_URL" "$SPRAT_DIR/sprat-cli"
fetch_repo "$GUI_REPO_URL" "$SPRAT_DIR/sprat-gui"

# sprat-gui requires the exact sprat-cli version listed in its DEPENDENCIES file.
required_cli_version="$(tr -d '[:space:]' < "$SPRAT_DIR/sprat-gui/DEPENDENCIES" 2> /dev/null || true)"
checkout_cli_version="$(tr -d '[:space:]' < "$SPRAT_DIR/sprat-cli/VERSION" 2> /dev/null || true)"
cli_source_dir="$SPRAT_DIR/sprat-cli"
cli_build_dir="$WORK_DIR/sprat-cli-build"

has_cli_tag() {
    git -C "$SPRAT_DIR/sprat-cli" rev-parse -q --verify "refs/tags/$required_cli_version^{commit}" > /dev/null
}

if [ -n "$required_cli_version" ] && [ "$required_cli_version" != "$checkout_cli_version" ]; then
    info "sprat-gui needs sprat-cli $required_cli_version, but the latest sprat-cli is ${checkout_cli_version:-unknown}."
    if ! has_cli_tag; then
        git -C "$SPRAT_DIR/sprat-cli" fetch --tags --quiet >> "$LOG_DIR/git-sprat-cli.log" 2>&1 || true
    fi
    if has_cli_tag; then
        cli_source_dir="$WORK_DIR/sprat-cli-$required_cli_version-src"
        cli_build_dir="$WORK_DIR/sprat-cli-$required_cli_version-build"
        # A tag doesn't change, so a complete earlier extraction is reused; extracting
        # again would touch every file and force a full rebuild.
        if [ ! -f "$cli_source_dir/.sprat-extracted" ]; then
            rm -rf "$cli_source_dir"
            mkdir -p "$cli_source_dir"
            if ! git -C "$SPRAT_DIR/sprat-cli" archive "$required_cli_version" | tar -x -C "$cli_source_dir"; then
                die "Couldn't extract sprat-cli $required_cli_version from the repository."
            fi
            : > "$cli_source_dir/.sprat-extracted"
        fi
        ok "Building sprat-cli $required_cli_version so it matches sprat-gui."
    else
        warn "Tag $required_cli_version not found; building sprat-cli ${checkout_cli_version:-from the checkout} instead."
        hint "sprat-gui may report that the CLI version doesn't match until both projects catch up."
    fi
fi

# Remove sources and builds of sprat-cli versions sprat-gui no longer needs.
for old in "$WORK_DIR"/sprat-cli-v*; do
    if [ -d "$old" ] && [ "$old" != "$cli_source_dir" ] && [ "$old" != "$cli_build_dir" ]; then
        rm -rf "$old"
    fi
done

# --- Step 5: Build ------------------------------------------------------------

step "Building (this can take several minutes)"

if [ -n "${SPRAT_BUILD_JOBS:-}" ]; then
    jobs="$SPRAT_BUILD_JOBS"
else
    # One job per CPU, limited to one per ~1.5 GB of memory so large C++ files
    # don't run the machine out of memory.
    cpus="$(getconf _NPROCESSORS_ONLN 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 2)"
    if [ "$OS" = "macos" ]; then
        mem_mb=$(( $(sysctl -n hw.memsize 2> /dev/null || echo 4294967296) / 1048576 ))
    else
        # Kernels before 3.14 (and some containers) have no MemAvailable; assume 4 GB.
        mem_kb="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2> /dev/null || true)"
        mem_mb=$(( ${mem_kb:-4194304} / 1024 ))
    fi
    jobs=$(( mem_mb / 1536 ))
    [ "$jobs" -gt "$cpus" ] && jobs="$cpus"
    [ "$jobs" -lt 1 ] && jobs=1
fi
info "Using $jobs parallel jobs (set SPRAT_BUILD_JOBS to change)."

# Configures and builds a CMake project; stops the script on failure.
cmake_build() {
    local name="$1" source_dir="$2" build_dir="$3" log_file
    shift 3
    log_file="$LOG_DIR/build-$name.log"
    : > "$log_file"

    printf '\n  %sConfiguring %s...%s\n' "$C_BOLD" "$name" "$C_RESET"
    if ! run_logged cmake -S "$source_dir" -B "$build_dir" -G "$CMAKE_GENERATOR_NAME" \
            -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "$@"; then
        fail "Configuring $name failed."
        die "CMake couldn't configure $name." \
            "Scroll up for the first error, or see the log: $log_file" \
            "If the build folder is stale, delete it and try again:  rm -rf \"$build_dir\""
    fi

    printf '\n  %sCompiling %s...%s\n' "$C_BOLD" "$name" "$C_RESET"
    if ! run_logged cmake --build "$build_dir" --parallel "$jobs"; then
        fail "Compiling $name failed."
        die "The $name build failed." \
            "Scroll up for the first error, or see the log: $log_file" \
            "If it ran out of memory, try fewer jobs:  SPRAT_BUILD_JOBS=1 bash $0"
    fi
    ok "Built $name."
}

# A build folder configured with another generator can't be reused.
reset_if_generator_changed() {
    local build_dir="$1" cached
    if [ -f "$build_dir/CMakeCache.txt" ]; then
        cached="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$build_dir/CMakeCache.txt")"
        if [ "$cached" != "$CMAKE_GENERATOR_NAME" ]; then
            rm -rf "$build_dir"
        fi
    fi
}

gui_build_dir="$WORK_DIR/sprat-gui"
reset_if_generator_changed "$gui_build_dir"

if [ "$OS" = "macos" ]; then
    # The app bundle gets its own sprat-cli tools (SPRAT_BUNDLE_CLI), built from the
    # source chosen above instead of a second download; they're copied to bin/ below.
    cmake_build sprat-gui "$SPRAT_DIR/sprat-gui" "$gui_build_dir" \
        -DSPRAT_EMBEDDED_CLI=OFF -DSPRAT_BUNDLE_CLI=ON -DSPRAT_E2E_TESTS=OFF \
        -DFETCHCONTENT_SOURCE_DIR_SPRAT_CLI="$cli_source_dir"
    cli_build_dir="$gui_build_dir/sprat-gui.app/Contents/Resources/cli"
else
    reset_if_generator_changed "$cli_build_dir"
    cmake_build sprat-cli "$cli_source_dir" "$cli_build_dir" -DSPRAT_DOWNLOAD_STB=ON
    # sprat-gui runs the tools built above, so it doesn't build them again.
    cmake_build sprat-gui "$SPRAT_DIR/sprat-gui" "$gui_build_dir" \
        -DSPRAT_EMBEDDED_CLI=OFF -DSPRAT_E2E_TESTS=OFF
fi

# --- Step 6: Install into the folder --------------------------------------------

step "Copying the programs into $SPRAT_DIR"

# The build puts spratprofiles.cfg and transforms/ next to the tools, where they
# look for them; copy them together.
for tool in $CLI_TOOLS; do
    [ -x "$cli_build_dir/$tool" ] || die "The build finished but $tool is missing from $cli_build_dir."
    cp -f "$cli_build_dir/$tool" "$BIN_DIR/$tool" || die "Couldn't copy $tool into $BIN_DIR."
done
cp -f "$cli_build_dir/spratprofiles.cfg" "$BIN_DIR/spratprofiles.cfg" \
    && rm -rf "$BIN_DIR/transforms" \
    && cp -R "$cli_build_dir/transforms" "$BIN_DIR/transforms" \
    || die "Couldn't copy the sprat-cli profiles and transforms into $BIN_DIR."
ok "sprat-cli tools, profiles and transforms -> $BIN_DIR"

if [ -f "$cli_source_dir/man/sprat-cli.1" ]; then
    mkdir -p "$SPRAT_DIR/man/man1" && cp -f "$cli_source_dir/man/sprat-cli.1" "$SPRAT_DIR/man/man1/" \
        && ok "Manual page -> $SPRAT_DIR/man/man1/sprat-cli.1"
fi

if [ "$OS" = "macos" ]; then
    app_src="$gui_build_dir/sprat-gui.app"
    app_dest="$SPRAT_DIR/sprat-gui.app"
    [ -d "$app_src" ] || die "The build finished but sprat-gui.app is missing from $gui_build_dir."
    rm -rf "$app_dest"
    cp -R "$app_src" "$app_dest" || die "Couldn't copy sprat-gui.app into $SPRAT_DIR."
    ok "sprat-gui.app (with its own copy of the tools) -> $app_dest"
    gui_launch="open \"$app_dest\""
else
    [ -x "$gui_build_dir/sprat-gui" ] || die "The build finished but sprat-gui is missing from $gui_build_dir."
    cp -f "$gui_build_dir/sprat-gui" "$BIN_DIR/sprat-gui" || die "Couldn't copy sprat-gui into $BIN_DIR."
    chmod +x "$BIN_DIR/sprat-gui"
    ok "sprat-gui -> $BIN_DIR/sprat-gui"
    gui_launch="\"$BIN_DIR/sprat-gui\""
fi

# --- Step 7: Configure sprat-gui -------------------------------------------------

step "Configuring sprat-gui"

# Same location as Qt's QStandardPaths::ConfigLocation, which sprat-gui uses.
if [ "$OS" = "macos" ]; then
    config_file="$HOME/Library/Preferences/sprat/sprat.conf"
else
    config_file="${XDG_CONFIG_HOME:-$HOME/.config}/sprat/sprat.conf"
fi

# Quotes a value for a QSettings INI file.
ini_quote() {
    local v="$1"
    v="${v//\\/\\\\}"
    v="${v//\"/\\\"}"
    printf '"%s"' "$v"
}

# Sets key=value in [section] of an INI file, keeping everything else.
ini_set() {
    local file="$1" section="$2" key="$3" value="$4" tmp
    tmp="$file.tmp.$$"
    if [ ! -f "$file" ]; then
        printf '[%s]\n%s=%s\n' "$section" "$key" "$value" > "$file"
        return
    fi
    awk -v section="[$section]" -v key="$key" -v line="$key=$value" '
        function flush() { if (in_section && !done) { print line; done = 1 } }
        /^\[.*\][[:space:]]*$/ {
            flush()
            in_section = ($0 == section); seen = seen || in_section
            print; next
        }
        in_section && index($0, key "=") == 1 { flush(); next }
        { print }
        END {
            flush()
            if (!seen) { print ""; print section; print line }
        }
    ' "$file" > "$tmp" && mv "$tmp" "$file"
}

if pgrep -x sprat-gui > /dev/null 2>&1; then
    warn "sprat-gui is running; close it, or it may overwrite these settings when it quits."
fi

mkdir -p "$(dirname "$config_file")" || die "Couldn't create $(dirname "$config_file")."
if [ -f "$config_file" ] && [ ! -f "$config_file.bak" ]; then
    cp "$config_file" "$config_file.bak"
    info "Backed up your settings to $config_file.bak"
fi
# base_dir is where sprat-gui looks first for the CLI tools. It checks their
# version itself when it starts.
ini_set "$config_file" cli base_dir "$(ini_quote "$BIN_DIR")" \
    || die "Couldn't update $config_file."
ok "sprat-gui will use the CLI tools in $BIN_DIR"
info "Settings file: $config_file"

# Quick check that the tools start and find their files.
if reported="$("$BIN_DIR/spratlayout" --version 2> /dev/null | head -n 1)"; then
    ok "spratlayout runs: $reported"
else
    warn "spratlayout didn't report its version; the GUI may not find the tools."
fi

# --- Done ---------------------------------------------------------------------

printf '\n%s%s All done!%s\n\n' "$C_BOLD$C_GREEN" "$MARK_OK" "$C_RESET"
printf '  Start sprat-gui:\n      %s\n\n' "$gui_launch"
printf '  Use the command-line tools from anywhere by adding this line to your shell profile:\n'
printf '      export PATH="%s:$PATH"\n\n' "$BIN_DIR"
printf '  To update later, run this script again from %s.\n' "$(dirname "$SPRAT_DIR")"
printf '  Build logs: %s\n\n' "$LOG_DIR"
