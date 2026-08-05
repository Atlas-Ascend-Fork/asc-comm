#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

RANK_SIZE="${1:-3}"
BYTES="${2:-4096}"
IP_PORT="${3:-tcp://127.0.0.1:8899}"
RANK_START="${4:-0}"
LOCAL_RANK_SIZE="${5:-${RANK_SIZE}}"

if [[ ! "${RANK_SIZE}" =~ ^[0-9]+$ ]] || ((RANK_SIZE < 2)); then
    echo "rank_size must be an integer greater than or equal to 2." >&2
    exit 1
fi
if [[ ! "${BYTES}" =~ ^[0-9]+$ ]] || ((BYTES == 0)); then
    echo "bytes must be a positive integer." >&2
    exit 1
fi
if [[ ! "${RANK_START}" =~ ^[0-9]+$ ]] || [[ ! "${LOCAL_RANK_SIZE}" =~ ^[0-9]+$ ]] || \
    ((LOCAL_RANK_SIZE == 0 || RANK_START + LOCAL_RANK_SIZE > RANK_SIZE)); then
    echo "local rank range must satisfy 0 <= rank_start and rank_start + local_rank_size <= rank_size." >&2
    exit 1
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build "${BUILD_DIR}" -j

PIDS=()
cleanup()
{
    local exit_code=$?
    trap - EXIT INT TERM
    for pid in "${PIDS[@]}"; do
        kill "${pid}" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do
        wait "${pid}" 2>/dev/null || true
    done
    exit "${exit_code}"
}
trap cleanup EXIT INT TERM

for ((local_rank = 0; local_rank < LOCAL_RANK_SIZE; ++local_rank)); do
    rank=$((RANK_START + local_rank))
    device="${local_rank}"
    "${BUILD_DIR}/demo" \
        "${RANK_SIZE}" \
        "${rank}" \
        "${device}" \
        "${BYTES}" \
        "${IP_PORT}" &
    PIDS+=("$!")
done

remaining=${#PIDS[@]}
while ((remaining > 0)); do
    if wait -n; then
        remaining=$((remaining - 1))
    else
        status=$?
        echo "A rank process failed with status ${status}." >&2
        exit "${status}"
    fi
done
PIDS=()

echo "ranks ${RANK_START}-$((RANK_START + LOCAL_RANK_SIZE - 1)) run success."
