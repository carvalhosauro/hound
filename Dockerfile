# syntax=docker/dockerfile:1
# Multi-stage image: build Release hound, run thin runtime.
# DX: docs/DX.md (D0.1)
#
# Build speed: Ninja + BuildKit cache mount on /src/build (keeps FetchContent
# deps across rebuilds). Prefer native amd64/arm runners over QEMU multi-arch.

FROM docker.io/library/debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tools ./tools
COPY examples ./examples

# Cache the entire build tree (FetchContent + object files). Copy the binary
# out — cache mounts are not persisted in the image layer.
RUN --mount=type=cache,target=/src/build,sharing=locked \
    cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DHOUND_BUILD_TESTS=OFF \
      -DHOUND_BUILD_BENCH=OFF \
    && cmake --build build -j"$(nproc)" --target hound \
    && cp /src/build/hound /src/hound

FROM docker.io/library/debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --uid 10001 hound

LABEL org.opencontainers.image.title="hound" \
      org.opencontainers.image.description="Lightweight C++ fuzzy autocomplete sidecar" \
      org.opencontainers.image.licenses="MIT"

WORKDIR /app
COPY --from=build /src/hound /app/hound
COPY --from=build /src/examples/sample.csv /app/examples/sample.csv

USER hound
EXPOSE 8080
# Bind all interfaces so published ports work; sample CSV for first-search DX.
ENTRYPOINT ["/app/hound"]
CMD ["--host", "0.0.0.0", "--port", "8080", "--load", "/app/examples/sample.csv"]
