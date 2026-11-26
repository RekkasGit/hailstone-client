#!/bin/bash
# The nightly run (docs/ci.md "Nightly run"): what a normal run of hailstone_tests leaves out, on origin/main.
#   1. The sanitizer build (HAILSTONE_SANITIZE) and every hailstone_tests case, with the game folder and the recorded
#      vectors set, so the tests that otherwise pass without checking anything check something.
#   2. The libFuzzer targets (HAILSTONE_LIBFUZZER, Clang), each for --fuzz-seconds, side by side, on corpora kept
#      between nights. A crash's input is kept for the morning.
#   3. A summary, printed and, with a webhook set, posted to Discord.
#
# Usage: tools/nightly.sh [--fuzz-seconds N] [--jobs N] [--no-fuzz] [--no-post] [--ref REF]
#   --fuzz-seconds  seconds per fuzz target (default 1800; the targets run at once, one core each)
#   --jobs          build and test jobs (default 20)
#   --ref           what to test (default origin/main, fetched first)
#
# Settings, in ${XDG_CONFIG_HOME:-~/.config}/hailstone/nightly.env (sourced; never in the repository):
#   EQ_GAME_DIR=...             the game folder the asset tests read
#   HAILSTONE_VECTORS_DIR=...   the recorded vectors (*.jsonl)
#   HAILSTONE_NIGHTLY_WEBHOOK=  a Discord webhook URL for the summary (optional)
#
# Everything it writes is under ${XDG_CACHE_HOME:-~/.cache}/hailstone-nightly: the checkout it tests (a detached
# worktree of this repository), its build folders, the fuzz corpora and crash inputs, and a log folder per night.
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && git rev-parse --show-toplevel)
fuzz_seconds=1800
jobs=20
fuzz=1
post=1
ref=origin/main
while [ $# -gt 0 ]; do
  case "$1" in
    --fuzz-seconds) fuzz_seconds=$2; shift 2 ;;
    --jobs) jobs=$2; shift 2 ;;
    --no-fuzz) fuzz=0; shift ;;
    --no-post) post=0; shift ;;
    --ref) ref=$2; shift 2 ;;
    *) echo "nightly: unknown option $1" >&2; exit 2 ;;
  esac
done

config=${XDG_CONFIG_HOME:-$HOME/.config}/hailstone/nightly.env
# shellcheck disable=SC1090
[ -f "$config" ] && . "$config"

cache=${XDG_CACHE_HOME:-$HOME/.cache}/hailstone-nightly
checkout=$cache/checkout
night=$(date +%Y%m%d-%H%M%S)
logs=$cache/logs/$night
mkdir -p "$logs" "$cache/corpus" "$cache/crashes"

exec 9>"$cache/lock"
if ! flock -n 9; then
  echo "nightly: another run holds $cache/lock" >&2
  exit 1
fi

summary=()
failed=0
note() { summary+=("$1"); echo "nightly: $1"; }
fail() { note "FAIL $1"; failed=1; }

# The checkout: a detached worktree at the ref, cleaned of everything but its build folders.
git -C "$repo" fetch -q origin || note "fetch failed; testing the last fetched $ref"
if [ ! -e "$checkout/.git" ]; then
  git -C "$repo" worktree add -q --detach "$checkout" "$ref" || { echo "nightly: cannot make $checkout" >&2; exit 1; }
fi
git -C "$checkout" checkout -q --detach -f "$ref" || { echo "nightly: cannot check out $ref" >&2; exit 1; }
git -C "$checkout" clean -q -ffdx -e /build-asan -e /build-fuzz -e /plugins/nms -e /plugins/projecttriune
# The server plugins' clones join the build when the main checkout has them (AGENTS.md, server plugins).
for p in nms projecttriune; do
  [ -d "$repo/plugins/$p" ] && [ ! -e "$checkout/plugins/$p" ] && ln -s "$repo/plugins/$p" "$checkout/plugins/$p"
done
commit=$(git -C "$checkout" rev-parse --short HEAD)
note "testing $ref at $commit"
[ -n "${EQ_GAME_DIR:-}" ] || note "EQ_GAME_DIR unset: the game folder tests check nothing"
[ -n "${HAILSTONE_VECTORS_DIR:-}" ] || note "HAILSTONE_VECTORS_DIR unset: the vector tests check nothing"

cd "$checkout" || exit 1

# 1. The sanitizer build and the tests.
if { [ -f build-asan/CMakeCache.txt ] ||
     cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHAILSTONE_SANITIZE=ON; } \
     > "$logs/asan-configure.log" 2>&1 &&
   nice cmake --build build-asan -j "$jobs" > "$logs/asan-build.log" 2>&1; then
  if nice ctest --test-dir build-asan -j "$jobs" --output-on-failure > "$logs/asan-tests.log" 2>&1; then
    note "sanitizer tests: $(grep -o '[0-9]*% tests passed.*' "$logs/asan-tests.log" | tail -1)"
  else
    fail "sanitizer tests: $(grep -o '[0-9]*% tests passed.*' "$logs/asan-tests.log" | tail -1) (asan-tests.log)"
  fi
else
  fail "sanitizer build (asan-configure.log, asan-build.log)"
fi

# 2. The fuzz targets, side by side.
if [ "$fuzz" = 1 ]; then
  if { [ -f build-fuzz/CMakeCache.txt ] ||
       cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHAILSTONE_LIBFUZZER=ON \
         -DHAILSTONE_BUILD_TESTS=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++; } \
       > "$logs/fuzz-configure.log" 2>&1; then
    targets=$(ninja -C build-fuzz -t targets all 2>/dev/null | grep -o '^fuzz_[a-z_]*:' | tr -d : | sort -u)
    # shellcheck disable=SC2086
    if nice cmake --build build-fuzz -j "$jobs" --target $targets > "$logs/fuzz-build.log" 2>&1; then
      pids=()
      for t in $targets; do
        mkdir -p "$cache/corpus/$t" "$cache/crashes/$t"
        nice build-fuzz/"$t" "$cache/corpus/$t" -max_total_time="$fuzz_seconds" -print_final_stats=1 \
          -artifact_prefix="$cache/crashes/$t/$night-" > "$logs/$t.log" 2>&1 &
        pids+=("$!:$t")
      done
      for entry in "${pids[@]}"; do
        pid=${entry%%:*}; t=${entry#*:}
        if wait "$pid"; then
          note "$t: $(grep -o 'stat::number_of_executed_units: *[0-9]*' "$logs/$t.log" | grep -o '[0-9]*$') runs, no crash"
        else
          fail "$t: crashed; its input is kept in crashes/$t ($t.log)"
        fi
      done
    else
      fail "fuzz build (fuzz-build.log)"
    fi
  else
    fail "fuzz configure (fuzz-configure.log)"
  fi
fi

# 3. The summary.
status="passed"
[ "$failed" = 1 ] && status="FAILED"
# The posted text names no local path (it would carry the user's name); the log folder is added locally only.
text="Hailstone nightly $status on $commit"$'\n'"$(printf -- '- %s\n' "${summary[@]}")"
printf '%s\n' "$text" > "$logs/summary.txt"
printf '%s\nLogs: %s\n' "$text" "$logs"
if [ "$post" = 1 ] && [ -n "${HAILSTONE_NIGHTLY_WEBHOOK:-}" ]; then
  python3 -c 'import json,sys; print(json.dumps({"content": sys.stdin.read()[:1900]}))' < "$logs/summary.txt" |
    curl -fsS -H 'Content-Type: application/json' --data-binary @- "$HAILSTONE_NIGHTLY_WEBHOOK" > /dev/null ||
    echo "nightly: posting the summary failed" >&2
fi
exit "$failed"
