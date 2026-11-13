#!/bin/bash
# Configures (once) and builds a Linux Release inside the Linux image, with ccache (compose.yaml).
#   linux_build.sh <build dir> [cmake configure args ...]
# JOBS is the parallel jobs (default 8; tools/docker/build.sh sets it to the container's CPU limit).
set -euo pipefail
build=$1
shift
if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache "$@"
elif [ $# -gt 0 ]; then
    # An existing build folder still takes the options given now; generator and compilers stay as configured.
    cmake -B "$build" "$@"
fi
cmake --build "$build" -j "${JOBS:-8}"
ccache -s | sed -n '1,6p'
