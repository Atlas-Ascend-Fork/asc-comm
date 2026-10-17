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

readonly MAX_AIV_CORE_NUM=56
readonly DEFAULT_RANK_NUM=8
readonly DEFAULT_IPPORT=127.0.0.1:8899
readonly DEFAULT_FIRST_RANK=0
readonly DEFAULT_FIRST_DEVICE=0
readonly MAX_RANK_NUM=128

if [[ $# -gt 6 ]]; then
    echo "Usage: $0 <core_num> [rank_num] [ip:port] [first_rank] [local_ranks] [first_device]" >&2
    exit 1
fi

coreNum=${1:-1}
rankNum=${2:-${DEFAULT_RANK_NUM}}
ipPort=${3:-${DEFAULT_IPPORT}}
firstRank=${4:-${DEFAULT_FIRST_RANK}}
localRanks=${5:-}
firstDevice=${6:-${DEFAULT_FIRST_DEVICE}}
# Unset local_ranks lets this host run every remaining rank from first_rank, so single-host runs need no more
# arguments than the core count and the rank count.
if [[ -z ${localRanks} ]]; then
    localRanks=$((rankNum - firstRank))
fi
readonly CORE_NUM=${coreNum}
readonly RANK_NUM=${rankNum}
readonly IPPORT=${ipPort}
readonly FIRST_RANK=${firstRank}
readonly LOCAL_RANKS=${localRanks}
readonly FIRST_DEVICE=${firstDevice}

if [[ ! ${CORE_NUM} =~ ^[0-9]+$ || ${CORE_NUM} -lt 1 || ${CORE_NUM} -gt ${MAX_AIV_CORE_NUM} ||
    ! ${RANK_NUM} =~ ^[0-9]+$ || ${RANK_NUM} -lt 2 || ${RANK_NUM} -gt ${MAX_RANK_NUM} ||
    ! ${FIRST_RANK} =~ ^[0-9]+$ || ! ${LOCAL_RANKS} =~ ^[0-9]+$ || ${LOCAL_RANKS} -lt 1 ||
    ! ${FIRST_DEVICE} =~ ^[0-9]+$ ]]; then
    echo "Usage: $0 <core_num: 1-${MAX_AIV_CORE_NUM}> [rank_num: 2-${MAX_RANK_NUM}] [ip:port] [first_rank] [local_ranks] [first_device]" >&2
    exit 1
fi
if (( FIRST_RANK + LOCAL_RANKS > RANK_NUM )); then
    echo "first_rank + local_ranks must not exceed rank_num" >&2
    exit 1
fi
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build/rdv/rdv_hcomm"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DRDV_AIV_CORE_NUM="${CORE_NUM}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

pids=()
cleanup()
{
    echo "[INFO] terminating local ranks" >&2
    if [[ ${#pids[@]} -gt 0 ]]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    exit 1
}
trap cleanup SIGINT SIGTERM SIGHUP

for localIndex in $(seq 0 $((LOCAL_RANKS - 1))); do
    rank=$((FIRST_RANK + localIndex))
    device=$((FIRST_DEVICE + localIndex))
    "${BUILD_DIR}/rdv_hcomm" "${RANK_NUM}" "${rank}" "tcp://${IPPORT}" "${device}" &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    if ! wait "${pid}"; then
        status=1
    fi
done
exit "${status}"
