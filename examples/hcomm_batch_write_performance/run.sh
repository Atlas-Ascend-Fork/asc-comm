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

readonly MAX_RANK_NUM=16
readonly MAX_OPERATIONS_PER_BATCH=512
readonly MAX_BATCH_COUNT=1000000
readonly RANK_NUM=${1:-2}
readonly OPERATIONS_PER_BATCH=${2:-500}
readonly WARMUP_BATCHES=${3:-100}
readonly MEASURE_BATCHES=${4:-1}

if [[ $# -gt 4 ||
      ! ${RANK_NUM} =~ ^[0-9]+$ || ${RANK_NUM} -lt 2 || ${RANK_NUM} -gt ${MAX_RANK_NUM} ||
      ! ${OPERATIONS_PER_BATCH} =~ ^[0-9]+$ || ${OPERATIONS_PER_BATCH} -lt 1 ||
      ${OPERATIONS_PER_BATCH} -gt ${MAX_OPERATIONS_PER_BATCH} ||
      ! ${WARMUP_BATCHES} =~ ^[0-9]+$ || ${WARMUP_BATCHES} -gt ${MAX_BATCH_COUNT} ||
      ! ${MEASURE_BATCHES} =~ ^[0-9]+$ || ${MEASURE_BATCHES} -lt 1 ||
      ${MEASURE_BATCHES} -gt ${MAX_BATCH_COUNT} ]]; then
    echo "Usage: $0 [rank_num: 2-${MAX_RANK_NUM}] [operations_per_batch: 1-${MAX_OPERATIONS_PER_BATCH}] "\
         "[warmup_batches: 0-${MAX_BATCH_COUNT}] [measure_batches: 1-${MAX_BATCH_COUNT}]" >&2
    exit 1
fi

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build/examples/hcomm_batch_write_performance"

if [[ -z ${ASCEND_HOME_PATH:-} ]]; then
    echo "ASCEND_HOME_PATH is not set. Source the CANN environment before running this sample." >&2
    exit 1
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build "${BUILD_DIR}" -j"$(nproc)"

exec "${BUILD_DIR}/hcomm_batch_write_performance" \
    "${RANK_NUM}" "${OPERATIONS_PER_BATCH}" "${WARMUP_BATCHES}" "${MEASURE_BATCHES}"
