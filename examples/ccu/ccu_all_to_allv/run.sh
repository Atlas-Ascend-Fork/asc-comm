#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ -d "${BUILD_DIR}" ]; then
    rm -rf "${BUILD_DIR}"
fi

if [ "${ASCEND_HOME_PATH:-}" = "" ]; then
    echo "ASCEND_HOME_PATH is not set. Please set environment before running this script." >&2
    exit 1
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

exec "${BUILD_DIR}/ccu_all_to_allv"
