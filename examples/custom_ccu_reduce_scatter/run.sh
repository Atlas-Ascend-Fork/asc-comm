#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ "${ASCEND_HOME_PATH:-}" = "" ]; then
    echo "ASCEND_HOME_PATH is not set. Please set environment before running this script." >&2
    exit 1
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

exec "${BUILD_DIR}/custom_reduce_scatter_ccu"
