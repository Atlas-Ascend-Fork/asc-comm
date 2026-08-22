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

if [[ $# -gt 1 || ( $# -eq 1 && ( ! $1 =~ ^[0-9]+$ || $1 -lt 2 || $1 -gt ${MAX_RANK_NUM} ) ) ]]; then
    echo "Usage: $0 [rank_num: 2-${MAX_RANK_NUM}]" >&2
    exit 1
fi

readonly RANK_NUM=${1:-2}
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build/examples/hcomm_batch_write"

if [[ -z ${ASCEND_HOME_PATH:-} ]]; then
    echo "ASCEND_HOME_PATH is not set. Source the CANN environment before running this sample." >&2
    exit 1
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build "${BUILD_DIR}" -j"$(nproc)"

exec "${BUILD_DIR}/hcomm_batch_write" "${RANK_NUM}"
