# Build environment for the arm64 package: Debian 13, matching the device.
FROM debian:trixie
# libsystemd for arm64 is what the service talks to the Bluetooth daemon with.
RUN dpkg --add-architecture arm64 \
    && apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
       crossbuild-essential-arm64 build-essential cmake ninja-build file dpkg-dev ca-certificates curl \
       libsystemd-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
