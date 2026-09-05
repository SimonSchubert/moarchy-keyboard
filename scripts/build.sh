#!/bin/bash
# Configure and build. Runs inside docker/Dockerfile.build, or on any machine
# with Qt6, layer-shell-qt and wayland-scanner.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "$BUILD_DIR" --parallel
echo
echo "==> built $BUILD_DIR/moarchy-keyboard"
