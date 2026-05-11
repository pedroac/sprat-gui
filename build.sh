#!/usr/bin/env bash

set -euo pipefail

# Ensure we're in the script directory
cd "$(dirname "$0")"

# Update DEPENDENCIES if possible (don't fail offline)
echo "Updating DEPENDENCIES..."
TAG=$(curl -fsSL https://api.github.com/repos/pedroac/sprat-cli/releases/latest 2>/dev/null \
    | sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' \
    | head -n 1 || true)

if [ -n "$TAG" ]; then
    echo "$TAG" > DEPENDENCIES
    echo "Dependencies updated to latest release tag: $TAG"
else
    echo "Could not fetch latest release tag. Using existing DEPENDENCIES file."
fi

BUILD_TYPE="${1:-host}"

if [ "$BUILD_TYPE" == "wasm" ]; then
    echo "Configuring for WASM..."
    # Use the Qt-provided Emscripten toolchain to match the Qt WASM build.
    # This avoids ABI/symbol mismatches with libqwasm.a.
    set +e
    source /opt/qt6-wasm/qtwasm_env.sh
    set -e
    EM_CACHE_DIR="$(pwd)/build_wasm/emsdk_cache"
    mkdir -p "${EM_CACHE_DIR}" "${EM_CACHE_DIR}/symbol_lists"
    export EM_CACHE="${EM_CACHE_DIR}"
    export PKG_CONFIG_PATH=/opt/qt6-wasm/emsdk/upstream/emscripten/system/lib/pkgconfig:/opt/qt6-wasm/emsdk/upstream/emscripten/cache/sysroot/lib/pkgconfig:${PKG_CONFIG_PATH:-}
    mkdir -p build_wasm
    mkdir -p build_wasm/wasm_deps
    emcc wasm/egl_stub.c -c -o build_wasm/wasm_deps/egl_stub.o
    emar rcs build_wasm/wasm_deps/libegl.a build_wasm/wasm_deps/egl_stub.o
    EGL_STUB_LIB="$(pwd)/build_wasm/wasm_deps/libegl.a"
    JSPI_FLAG="${SPRAT_WASM_JSPI:-OFF}"
    emcmake cmake -B build_wasm -S . \
        -DCMAKE_PREFIX_PATH=/opt/qt6-wasm \
        -DCMAKE_FIND_ROOT_PATH=/opt/qt6-wasm \
        -DCMAKE_MODULE_PATH=/opt/qt6-wasm/lib/cmake/Qt6 \
        -DSPRAT_WASM_DEBUG=ON \
        -DSPRAT_WASM_JSPI="${JSPI_FLAG}" \
        -DEGL_INCLUDE_DIR=/opt/qt6-wasm/emsdk/upstream/emscripten/system/include \
        -DEGL_LIBRARY=${EGL_STUB_LIB} \
        -DCMAKE_CXX_FLAGS="-DNDEBUG -DEMSCRIPTEN_DISABLE_ASSERTS=1" \
        -DCMAKE_C_FLAGS="-DNDEBUG -DEMSCRIPTEN_DISABLE_ASSERTS=1"
    
    echo "Building for WASM..."
    export EMFLAGS="-sASSERTIONS=0"
    cmake --build build_wasm --parallel 2

    echo "Compressing WASM build..."
    cd build_wasm
    rm -f sprat-gui-wasm.zip
    zip -j sprat-gui-wasm.zip index.html qtloader.js qtlogo.svg sprat-gui.data sprat-gui.js sprat-gui.wasm
    cd ..

    echo "WASM build complete in build_wasm/"
else
    echo "Configuring for host..."
    mkdir -p build
    cmake -B build -S .

    echo "Building for host..."
    cmake --build build --parallel 2

    echo "Testing host build..."
    ctest --test-dir build --output-on-failure
fi
