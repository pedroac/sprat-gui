#!/usr/bin/env bash

set -u

if cmake .; then
    if make; then
        ctest --output-on-failure
    else
        echo "make failed"
        exit 1
    fi
else
    echo "cmake failed"
    exit 1
fi
