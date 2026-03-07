#!/usr/bin/env bash

set -euo pipefail

echo "Updating DEPENDENCIES..."
TAG=$(curl -fsSL https://api.github.com/repos/pedroac/sprat-cli/releases/latest 2>/dev/null | sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)

if [ -n "$TAG" ]; then
    echo "$TAG" > DEPENDENCIES
    echo "Dependencies updated to latest release tag: $TAG"
else
    echo "Could not fetch latest release tag. Using existing DEPENDENCIES file."
fi

echo "Configuring..."
mkdir -p build
cmake -B build -S .

echo "Building..."
cmake --build build --parallel 2

echo "Testing..."
ctest --test-dir build --output-on-failure
