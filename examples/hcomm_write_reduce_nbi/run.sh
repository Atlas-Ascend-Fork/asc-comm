#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

DEFAULT_BYTES=4096
DEFAULT_IP_PORT="tcp://127.0.0.1:8899"

if (($# == 0)); then
    DATA_TYPES=(INT8 INT16 INT32 UINT32 FP16 FP32 BFP16)
    REDUCE_OPS=(MAX MIN SUM)
    BYTES="${DEFAULT_BYTES}"
    IP_PORT="${DEFAULT_IP_PORT}"
elif (($# >= 2 && $# <= 4)); then
    DATA_TYPES=("$1")
    REDUCE_OPS=("$2")
    BYTES="${3:-${DEFAULT_BYTES}}"
    IP_PORT="${4:-${DEFAULT_IP_PORT}}"
else
    echo "Usage: $0 [data_type reduce_op [bytes [ipport]]]" >&2
    exit 1
fi

if [[ ! "${BYTES}" =~ ^[0-9]+$ ]] || ((BYTES == 0)); then
    echo "bytes must be a positive integer." >&2
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

run_case()
{
    local data_type="$1"
    local reduce_op="$2"
    local element_size

    case "${data_type}" in
        INT8) element_size=1 ;;
        INT16|FP16|BFP16) element_size=2 ;;
        INT32|UINT32|FP32) element_size=4 ;;
        *)
            echo "data_type must be one of: INT8 INT16 INT32 UINT32 FP16 FP32 BFP16." >&2
            return 1
            ;;
    esac

    case "${reduce_op}" in
        MAX|MIN|SUM) ;;
        *)
            echo "reduce_op must be one of: MAX MIN SUM." >&2
            return 1
            ;;
    esac

    if ((BYTES % element_size != 0)); then
        echo "bytes must be aligned to ${data_type} element size ${element_size}." >&2
        return 1
    fi

    echo "Running ${data_type} + ${reduce_op}, bytes=${BYTES}."
    for rank in 0 1; do
        local device="${rank}"
        "${BUILD_DIR}/demo" \
            2 \
            "${rank}" \
            "${device}" \
            "${BYTES}" \
            "${data_type}" \
            "${reduce_op}" \
            "${IP_PORT}" &
        PIDS+=("$!")
    done

    local remaining=${#PIDS[@]}
    while ((remaining > 0)); do
        if wait -n; then
            remaining=$((remaining - 1))
        else
            local status=$?
            echo "${data_type} + ${reduce_op} failed with status ${status}." >&2
            return "${status}"
        fi
    done
    PIDS=()
    echo "${data_type} + ${reduce_op} success."
}

for data_type in "${DATA_TYPES[@]}"; do
    for reduce_op in "${REDUCE_OPS[@]}"; do
        run_case "${data_type}" "${reduce_op}"
    done
done

echo "All requested datatype and reduce-op cases passed."
