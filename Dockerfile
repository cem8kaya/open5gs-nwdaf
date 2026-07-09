# P0-4: Reproducible Build Path (Docker)
# Uses Ubuntu 22.04 LTS (Jammy) for stable and pinned environment.

FROM ubuntu:22.04 AS builder

# Prevent tzdata prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies for the Open5GS NWDAF C++ component.
# The base image tag (ubuntu:22.04) is the reproducibility anchor; exact apt
# version pins are intentionally avoided because Ubuntu removes superseded
# point versions from the archive, which breaks the build over time.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    libsystemd-dev \
    libsqlite3-dev \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Step 1: Pre-fetch dependencies to allow offline build
# FetchContent works during CMake configure time.
COPY CMakeLists.txt ./
# Copy minimal directories to allow CMake to configure and fetch
RUN mkdir -p src include tests config && \
    touch src/main.cpp

RUN cmake -S . -B build

# Step 2: Copy actual source code
COPY . /src

# Step 3: Build the project fully offline
# Network is not required here because FetchContent already downloaded deps
RUN cmake -S . -B build && \
    cmake --build build -j$(nproc)

FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libsystemd0 \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/open5gs-nwdafd /usr/local/bin/open5gs-nwdafd
COPY config/nwdaf.yaml /etc/open5gs/nwdaf.yaml

# Provide an entrypoint
CMD ["/usr/local/bin/open5gs-nwdafd", "-c", "/etc/open5gs/nwdaf.yaml"]
