#!/usr/bin/env bash

set -euo pipefail

readonly MAX_RANK_NUM=16

if [[ $# -gt 1 || ( $# -eq 1 && ( ! $1 =~ ^[0-9]+$ || $1 -lt 2 || $1 -gt ${MAX_RANK_NUM} ) ) ]]; then
    echo "Usage: $0 [rank_num: 2-${MAX_RANK_NUM}]" >&2
    exit 1
fi

readonly RANK_NUM=${1:-2}
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build/examples/hcomm_batch_write"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

exec "${BUILD_DIR}/hcomm_batch_write" "${RANK_NUM}"
