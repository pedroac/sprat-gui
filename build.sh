#!/usr/bin/env bash

set -euo pipefail

echo "Updating DEPENDENCIES..."
TAG=$(git ls-remote --tags --sort="v:refname" https://github.com/pedroac/sprat-cli.git 2>/dev/null | tail -n 1 | awk -F/ '{print $3}')

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
