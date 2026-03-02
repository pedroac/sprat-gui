#!/usr/bin/env bash

set -euo pipefail

echo "Configuring..."
cmake .

echo "Building..."
cmake --build . --parallel 2

echo "Testing..."
ctest --output-on-failure
