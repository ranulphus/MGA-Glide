# Development and CI image for MGA-Glide: 86Box build dependencies,
# the Loop A harness tools, and host test compilers. CI installs the same
# list (packages.txt) directly on its runner.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
COPY packages.txt /tmp/packages.txt
RUN apt-get update && xargs -a /tmp/packages.txt apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*
