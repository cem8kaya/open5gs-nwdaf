# P0-4: Reproducible Build Path (Docker)
# Uses Ubuntu 22.04 LTS (Jammy) for stable and pinned environment.

FROM ubuntu:22.04 AS builder

# Prevent tzdata prompts
ENV DEBIAN_FRONTEND=noninteractive

# Pin all dependencies via apt-get
# We install exactly what is needed for Open5GS NWDAF C++ component
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential=12.9ubuntu3 \
    cmake=3.22.1-1ubuntu1.22.04.2 \
    pkg-config=0.29.2-1ubuntu3 \
    libssl-dev=3.0.2-0ubuntu1.18 \
    libsystemd-dev=249.11-0ubuntu3.12 \
    libsqlite3-dev=3.37.2-2ubuntu0.3 \
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
