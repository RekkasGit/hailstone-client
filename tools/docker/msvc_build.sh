#!/bin/bash
# Configures (once) and builds with MSVC under Wine, inside hailstone-msvc-build (docs/ci.md "MSVC under Wine").
#   msvc_build.sh <source dir> <build dir> [cmake configure args ...]
# MSVC_DIR is msvc-wine's install (bin/x64/cl ...); JOBS the parallel jobs (default 20).
set -euo pipefail
src=$1 build=$2
shift 2
export PATH="${MSVC_DIR:?set MSVC_DIR to the msvc-wine install}/bin/x64:$PATH" WINEDEBUG=-all
# One wine server for the whole build, rather than one started and stopped per compile.
wineserver -p 2>/dev/null || true
wine wineboot >/dev/null 2>&1 || true
# wineboot can stop before it makes the profile's Temp folder (seen when its 32-bit rundll32 is missing), and cl then
# fails with D8037 "cannot create temporary il file": make the folder Wine's TEMP names.
temp=$(wine cmd /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')
if [ -n "$temp" ]; then mkdir -p "$(winepath -u "$temp" 2>/dev/null)" || true; fi
if [ ! -f "$build/CMakeCache.txt" ]; then
    CC=cl CXX=cl cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows "$@"
fi
cmake --build "$build" -j "${JOBS:-20}"
