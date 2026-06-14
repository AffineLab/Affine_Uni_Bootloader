FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        gcc-arm-none-eabi \
        git \
        libnewlib-arm-none-eabi \
        ninja-build \
        openssl \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["bash", "tools/ci.sh"]

