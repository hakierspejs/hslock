# Builds hslock.uf2 without installing the toolchain on the host.
#
# Usage (from repo root, submodules already checked out):
#   docker build -t hslock-build .
#   docker run --rm -v "$(pwd)":/src hslock-build
#
# The container only brings the toolchain + Pico SDK; your working tree is
# bind-mounted at /src and built in place, so hslock.uf2 lands directly in
# the repo root - no copy-out step needed.

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    build-essential \
    git \
    python3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Pinned Pico SDK checkout, matching docs/BUILD.md's manual clone (no fixed
# tag there - pinned here for reproducible container builds).
ARG PICO_SDK_TAG=2.1.1
RUN git clone -b $PICO_SDK_TAG --depth 1 https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && cd /opt/pico-sdk && git submodule update --init --recursive --depth 1

ENV PICO_SDK_PATH=/opt/pico-sdk

WORKDIR /src
ENTRYPOINT ["./build.sh"]
