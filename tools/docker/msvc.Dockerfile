# MSVC under Wine (msvc-wine), for the same compiler as CI's windows-msvc leg (docs/ci.md "MSVC under Wine").
# Built on the Linux image: docker build -t hailstone-linux-build:24.04 -f tools/docker/linux.Dockerfile tools/docker
# winbind is there because CMake's first test compiles write separate PDB files, which need it under Wine.
# compose.yaml passes the Linux service's image as BASE; by hand it is the tag above.
ARG BASE=hailstone-linux-build:24.04
FROM ${BASE}
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    wine64 wine msitools procps winbind && rm -rf /var/lib/apt/lists/*
