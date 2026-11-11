# MSVC under Wine (msvc-wine), for the same compiler as CI's windows-msvc leg (docs/ci.md "MSVC under Wine").
# Built on the Linux image: docker build -t hailstone-linux-build:24.04 -f tools/docker/linux.Dockerfile tools/docker
# winbind is there because CMake's first test compiles write separate PDB files, which need it under Wine.
# wine32 too: without it wineboot stops at syswow64\rundll32.exe before it writes the user's environment, so Wine has no
# TEMP and cl fails with D8037 "cannot create temporary il file".
# tools/docker/build.sh builds the Linux image first, under the tag above.
ARG BASE=hailstone-linux-build:24.04
FROM ${BASE}
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    wine64 wine wine32:i386 msitools procps winbind && rm -rf /var/lib/apt/lists/*
