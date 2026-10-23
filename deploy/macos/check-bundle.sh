#!/usr/bin/env bash
# Checks that a macOS app bundle runs on every Mac sprat-gui supports. Every Mach-O file
# must have arm64 and x86_64 slices, a minimum macOS no newer than the given version, and
# link only system libraries or libraries inside the bundle (not Homebrew's, for example).
# The bundled sprat-cli tools must be present.
#
# Usage: deploy/macos/check-bundle.sh <app bundle> [highest allowed minimum macOS, default 11.0]
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <app bundle> [highest allowed minimum macOS]" >&2
    exit 64
fi

bundle="${1%/}"
max_minos="${2:-11.0}"
required_archs=(arm64 x86_64)
bundled_tools=(spratlayout spratpack spratconvert spratframes spratunpack)
failures=0

fail() {
    echo "[check-bundle] FAIL: $*" >&2
    failures=$((failures + 1))
}

# Succeeds when version $1 <= version $2 (dotted, up to three parts).
version_le() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        split(a, x, "."); split(b, y, ".")
        for (i = 1; i <= 3; i++) {
            if ((x[i] + 0) < (y[i] + 0)) exit 0
            if ((x[i] + 0) > (y[i] + 0)) exit 1
        }
        exit 0
    }'
}

for tool in "${bundled_tools[@]}"; do
    [ -x "$bundle/Contents/Resources/cli/$tool" ] || fail "Contents/Resources/cli/$tool is missing"
done

machO_count=0
# One file(1) run for the whole bundle; it prints "<path>|<description>" per file.
while IFS= read -r file; do
    machO_count=$((machO_count + 1))
    relative="${file#"$bundle"/}"
    archs=$(lipo -archs "$file")

    for arch in "${required_archs[@]}"; do
        case " $archs " in
            *" $arch "*) ;;
            *) fail "$relative has no $arch slice (has: $archs)"; continue ;;
        esac

        minos=$(otool -arch "$arch" -l "$file" | awk '
            /LC_BUILD_VERSION/      { build = 1 }
            build && $1 == "minos"  { print $2; exit }
            /LC_VERSION_MIN_MACOSX/ { legacy = 1 }
            legacy && $1 == "version" { print $2; exit }')
        if [ -z "$minos" ]; then
            fail "$relative ($arch) has no minimum macOS version"
        elif ! version_le "$minos" "$max_minos"; then
            fail "$relative ($arch) needs macOS $minos (must be $max_minos or older)"
        fi

        while read -r library; do
            case "$library" in
                /System/*|/usr/lib/*|@rpath/*|@executable_path/*|@loader_path/*) ;;
                *) fail "$relative ($arch) links $library, which is outside the bundle and the system" ;;
            esac
        done < <(otool -arch "$arch" -L "$file" | tail -n +2 | awk '{ print $1 }')
    done
done < <(find "$bundle" -type f -exec file -F '|' {} + | awk -F '|' '/Mach-O/ { print $1 }')

if [ "$machO_count" -eq 0 ]; then
    fail "no Mach-O files found in $bundle"
fi

if [ "$failures" -ne 0 ]; then
    echo "[check-bundle] $failures problem(s) in $bundle" >&2
    exit 1
fi
echo "[check-bundle] OK: $machO_count Mach-O files, all ${required_archs[*]}, macOS $max_minos or older, no outside libraries"
