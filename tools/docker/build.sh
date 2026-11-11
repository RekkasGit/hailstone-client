#!/bin/bash
# Builds, tests and installs the client for one platform, or all, in containers (compose.yaml, docs/ci.md
# "Builds in containers").
#   tools/docker/build.sh linux-x64|linux-arm64|windows-x64|macos-arm64|all [--no-test]
# Each platform's install lands in dist/<platform>/hailstone, ready for tools/make_update.py --dist.
# JOBS sets the parallel jobs. windows-x64 downloads MSVC on its first run and needs MSVC_ACCEPT_LICENSE=1.
# macos-arm64 starts the macOS workflow on GitHub for HEAD (pushed) and unpacks its artifact; it needs gh.
# A server plugin built in (docs/plugins.md): PLUGIN_DIR=<its clone> PLUGIN_NAME=<folder under plugins/>.
set -euo pipefail
cd "$(dirname "$0")/../.."
what=${1:?usage: tools/docker/build.sh linux-x64|linux-arm64|windows-x64|macos-arm64|all [--no-test]}
test=1; [ "${2:-}" = --no-test ] && test=0
export HOST_UID=$(id -u) HOST_GID=$(id -g)
mkdir -p .docker/ccache/linux-x64 .docker/ccache/linux-arm64 .docker/msvc .docker/msvc-home .docker/no-plugin dist

# Compose v2 (docker compose) or v1 (docker-compose); BuildKit so linux-arm64's image can be built for its platform.
if docker compose version >/dev/null 2>&1; then dc() { docker compose "$@"; }; else dc() { docker-compose "$@"; }; fi
export DOCKER_BUILDKIT=1 COMPOSE_DOCKER_CLI_BUILD=1

install_tree() { # <platform> <build dir> <service>
    rm -rf "dist/$1"
    dc run --rm --no-deps "$3" cmake --install "$2" --prefix "dist/$1/hailstone"
    # The default Windows install also writes zlib's bin/, include/ and lib/ (docs/updater.md step 2).
    rm -rf "dist/$1/hailstone/bin" "dist/$1/hailstone/include" "dist/$1/hailstone/lib"
    echo "installed dist/$1/hailstone"
}

linux() { # <platform>
    if [ "$1" = linux-arm64 ] && [ "$(uname -m)" != aarch64 ] && [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
        echo "registering QEMU for arm64 (until the next reboot)"
        docker run --privileged --rm tonistiigi/binfmt --install arm64
    fi
    dc build "$1"
    dc run --rm "$1"
    if [ $test = 1 ]; then dc run --rm --no-deps -e EQ_GAME_DIR= "$1" "build-$1/hailstone_tests"; fi
    install_tree "$1" "build-$1" "$1"
}

windows() {
    dc build linux-x64
    dc --profile setup build msvc-setup
    if [ ! -x .docker/msvc/bin/x64/cl ]; then
        if [ "${MSVC_ACCEPT_LICENSE:-}" != 1 ]; then
            echo "MSVC is downloaded once: set MSVC_ACCEPT_LICENSE=1 to accept Microsoft's license"; exit 1
        fi
        dc --profile setup run --rm msvc-setup
    fi
    dc run --rm windows-x64
    if [ $test = 1 ]; then dc run --rm --no-deps -e EQ_GAME_DIR= windows-x64 wine build-windows-x64/hailstone_tests.exe; fi
    install_tree windows-x64 build-windows-x64 windows-x64
}

macos() {
    command -v gh >/dev/null || { echo "macos-arm64 needs the GitHub CLI (gh)"; exit 1; }
    sha=$(git rev-parse HEAD)
    if [ -z "$(git branch -r --contains "$sha")" ]; then echo "push $sha first: the workflow builds a pushed commit"; exit 1; fi
    gh workflow run macos.yml -f ref="$sha"
    sleep 10
    run=$(gh run list --workflow macos.yml --limit 1 --json databaseId -q '.[0].databaseId')
    gh run watch "$run" --exit-status
    rm -rf dist/macos-arm64 && mkdir -p dist/macos-arm64
    gh run download "$run" -n hailstone-macos-arm64 -D dist/macos-arm64
    tar -C dist/macos-arm64 -xf dist/macos-arm64/hailstone-macos-arm64.tar
    rm dist/macos-arm64/hailstone-macos-arm64.tar
    echo "installed dist/macos-arm64/hailstone"
}

case $what in
    linux-x64|linux-arm64) linux "$what" ;;
    windows-x64) windows ;;
    macos-arm64) macos ;;
    all) linux linux-x64; windows; linux linux-arm64; macos ;;
    *) echo "unknown platform $what"; exit 1 ;;
esac
