# Development and CI image for MGA-Glide: 86Box build dependencies,
# the Loop A harness tools, and host test compilers.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential gcc-multilib g++-multilib cmake ninja-build pkg-config git ca-certificates \
      curl xz-utils bzip2 unzip zip file python3 \
      libsdl2-dev libfreetype-dev libpng-dev libsndfile1-dev libserialport-dev libevdev-dev \
      libxkbcommon-dev libxkbcommon-x11-dev libslirp-dev libopenal-dev zlib1g-dev \
      mtools xvfb xauth xdotool dosfstools libfl2 \
    && rm -rf /var/lib/apt/lists/*
