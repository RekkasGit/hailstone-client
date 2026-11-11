#!/bin/bash
# Downloads MSVC 2022 and the Windows SDK into .docker/msvc and installs msvc-wine's wrappers (compose.yaml
# msvc-setup). tools/docker/build.sh runs it once, after MSVC_ACCEPT_LICENSE=1 accepts Microsoft's license.
set -euo pipefail
d=/src/.docker
if [ -x "$d/msvc/bin/x64/cl" ]; then echo "MSVC is already installed in .docker/msvc"; exit 0; fi
[ -d "$d/msvc-wine-src/.git" ] || git clone --depth 1 https://github.com/mstorsjo/msvc-wine "$d/msvc-wine-src"
python3 "$d/msvc-wine-src/vsdownload.py" --major 17 --accept-license --architecture x64 \
    --with-asan no --with-atl no --dest "$d/msvc"
"$d/msvc-wine-src/install.sh" "$d/msvc"
