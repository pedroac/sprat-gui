#!/usr/bin/env bash

set -euo pipefail

echo "Configuring..."
cmake .

echo "Building..."
cmake --build . --parallel

echo "Testing..."
ctest --output-on-failure
