#!/usr/bin/env bash

set -euo pipefail

# === Configuration ===
readonly BUILD_DIR="build"
readonly NUM_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# === Functions ===
log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

# === Main Script ===
main() {
    log_info "Starting build process..."

    # Create build directory
    if [[ ! -d "$BUILD_DIR" ]]; then
        mkdir -p "$BUILD_DIR"
        log_info "Created build directory: $BUILD_DIR"
    fi

    # Configure with CMake
    log_info "Configuring with CMake..."
    if ! cmake -S . -B "$BUILD_DIR"; then
        log_error "CMake configuration failed"
        exit 1
    fi

    # Build with make
    log_info "Building with make ($NUM_JOBS jobs)..."
    if ! make -C "$BUILD_DIR" -j "$NUM_JOBS"; then
        log_error "Build failed"
        exit 1
    fi

    # Run tests
    log_info "Running tests..."
    if ! ctest -C "$BUILD_DIR" --output-on-failure; then
        log_error "Tests failed"
        exit 1
    fi

    log_info "Build completed successfully!"
    log_info "Binary location: $BUILD_DIR/sprat-gui"
}

# === Entry Point ===
main "$@"
