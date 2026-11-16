# Build environment for the arm64 package: Debian 13, matching the device.
FROM debian:trixie
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
       crossbuild-essential-arm64 build-essential cmake ninja-build file dpkg-dev ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
