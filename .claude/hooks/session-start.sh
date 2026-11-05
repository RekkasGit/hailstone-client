#!/bin/bash
# Cloud sessions: the tools the build needs beyond the image's (CMake, Ninja, GCC and Clang are there). CMake's
# configure step fails without a GLSL compiler (cmake/Shaders.cmake), so install glslang, then configure build/ once
# so the dependencies CMake fetches are in place before the first build.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

if ! command -v glslangValidator >/dev/null 2>&1 && ! command -v glslc >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  if ! apt-get install -y glslang-tools >/dev/null 2>&1; then
    apt-get update >/dev/null
    apt-get install -y glslang-tools >/dev/null
  fi
fi

cd "$CLAUDE_PROJECT_DIR"
if [ ! -f build/build.ninja ]; then
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
fi
