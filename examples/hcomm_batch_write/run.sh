#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software: you can redistribute it and/or modify it under the terms and conditions of
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
readonly BINARY="${SCRIPT_DIR}/build/hcomm_batch_write"

if [[ ! -x "${BINARY}" ]]; then
    echo "binary does not exist or is not executable: ${BINARY}" >&2
    echo "build the sample first:" >&2
    echo "  mkdir -p ${SCRIPT_DIR}/build && cd ${SCRIPT_DIR}/build" >&2
    echo "  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 .. && make -j" >&2
    exit 1
fi

exec "${BINARY}" "${RANK_NUM}"
