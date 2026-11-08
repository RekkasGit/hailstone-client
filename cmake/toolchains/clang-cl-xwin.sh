#!/usr/bin/env bash
# clang-cl for cmake/toolchains/clang-cl-xwin.cmake: the target and the xwin SDK's headers first, on every compile
# (then xwin-include/, for a header CEF names in another case than the SDK).
# Three warnings go last, just before the "--" CMake puts ahead of the source file, so they win over the warning
# flags a target sets: CEF's wrapper library (tools/hailstone-web) builds with /WX, and clang-cl reports what MSVC
# does not (/MP it ignores, missing field initializers, an undefined variable template).
set -euo pipefail
xwin="${HAILSTONE_XWIN_DIR:-$HOME/.xwin}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pre=(--target=x86_64-pc-windows-msvc
     "/imsvc$xwin/crt/include" "/imsvc$xwin/sdk/include/ucrt" "/imsvc$xwin/sdk/include/um"
     "/imsvc$xwin/sdk/include/shared" "/imsvc$xwin/sdk/include/winrt" "/imsvc$here/xwin-include")
post=(-Wno-unused-command-line-argument -Wno-missing-field-initializers -Wno-undefined-var-template)
args=()
placed=0
for a in "$@"; do
    if [[ "$a" == "--" && $placed == 0 ]]; then
        args+=("${post[@]}")
        placed=1
    fi
    args+=("$a")
done
if [[ $placed == 0 ]]; then args+=("${post[@]}"); fi
exec clang-cl "${pre[@]}" "${args[@]}"
