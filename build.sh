#!/usr/bin/env bash

set -euo pipefail

echo "Updating DEPENDENCIES..."
TAG=$(git ls-remote --tags --sort="v:refname" https://github.com/pedroac/sprat-cli.git 2>/dev/null | awk '{print $2}' | sed 's|refs/tags/||' | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | tail -n 1)

if [ -n "$TAG" ]; then
    echo "$TAG" > DEPENDENCIES
    echo "Dependencies updated to version: $TAG"
else
    echo "Could not fetch remote tags. Using existing DEPENDENCIES file."
fi

echo "Configuring..."
mkdir -p build
cmake -B build -S .

echo "Building..."
cmake --build build --parallel 2

echo "Testing..."
ctest --test-dir build --output-on-failure
