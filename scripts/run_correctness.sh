#!/usr/bin/env bash
# Run correctness suite (unit + golden + integration). Optional TSan job.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${HOUND_BUILD_DIR:-$ROOT/build}"
BUILD_TSAN="${HOUND_TSAN_BUILD_DIR:-$ROOT/build-tsan}"
RUN_TSAN="${HOUND_RUN_TSAN:-1}"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}" \
  -DHOUND_BUILD_TESTS=ON -DHOUND_ENABLE_TSAN=OFF
cmake --build "$BUILD" -j"$(nproc)"
ctest --test-dir "$BUILD" --output-on-failure

if [[ "$RUN_TSAN" == "1" ]]; then
  if [[ ! -e /usr/lib64/libtsan.so.2 && ! -e /usr/lib64/libtsan.so.2.0.0 ]]; then
    echo "WARN: libtsan runtime missing — skipping TSan job."
    echo "      Install with: sudo dnf install libtsan   # Fedora/RHEL"
    echo "                   sudo apt install libgcc-*  # or libtsan0 on Debian"
  else
    cmake -S "$ROOT" -B "$BUILD_TSAN" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DHOUND_BUILD_TESTS=ON -DHOUND_ENABLE_TSAN=ON -DHOUND_BUILD_BENCH=OFF
    cmake --build "$BUILD_TSAN" -j"$(nproc)"
    ctest --test-dir "$BUILD_TSAN" --output-on-failure
  fi
fi

echo "correctness OK"
