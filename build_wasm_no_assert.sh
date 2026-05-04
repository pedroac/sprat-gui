#!/bin/bash
set -e

# Build WASM without Asyncify assertions
cd "$(dirname "$0")"

# Source Qt WASM environment
source /opt/qt6-wasm/qtwasm_env.sh

# Setup cache
EM_CACHE_DIR="$(pwd)/build_wasm/emsdk_cache"
mkdir -p "${EM_CACHE_DIR}" "${EM_CACHE_DIR}/symbol_lists"
export EM_CACHE="${EM_CACHE_DIR}"
export PKG_CONFIG_PATH=/opt/qt6-wasm/emsdk/upstream/emscripten/system/lib/pkgconfig:/opt/qt6-wasm/emsdk/upstream/emscripten/cache/sysroot/lib/pkgconfig:${PKG_CONFIG_PATH:-}

# Create stub EGL library (required for WASM)
mkdir -p build_wasm/wasm_deps
emcc wasm/egl_stub.c -c -o build_wasm/wasm_deps/egl_stub.o
emar rcs build_wasm/wasm_deps/libegl.a build_wasm/wasm_deps/egl_stub.o

# Configure with assertion disabling flags
emcmake cmake -B build_wasm -S . \
  -DCMAKE_PREFIX_PATH=/opt/qt6-wasm \
  -DCMAKE_FIND_ROOT_PATH=/opt/qt6-wasm \
  -DCMAKE_MODULE_PATH=/opt/qt6-wasm/lib/cmake/Qt6 \
  -DSPRAT_WASM_DEBUG=ON \
  -DSPRAT_WASM_JSPI=OFF \
  -DEGL_INCLUDE_DIR=/opt/qt6-wasm/emsdk/upstream/emscripten/system/include \
  -DEGL_LIBRARY="$(pwd)/build_wasm/wasm_deps/libegl.a" \
  -DCMAKE_CXX_FLAGS="-DNDEBUG -DEMSCRIPTEN_DISABLE_ASSERTS=1" \
  -DCMAKE_C_FLAGS="-DNDEBUG -DEMSCRIPTEN_DISABLE_ASSERTS=1"

# Build
cmake --build build_wasm

echo "✓ WASM build complete (Asyncify assertions disabled)"
