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
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"

if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "ASCEND_HOME_PATH is not set; source CANN 9.1+ set_env.sh first." >&2
    exit 2
fi

# The CCU dataplane only works when HCCL builds the communicator on the CCU
# scheduling path. Without this HcclGetHcclBuffer returns empty and the sample
# fails at resource setup.
export HCCL_OP_EXPANSION_MODE="${HCCL_OP_EXPANSION_MODE:-CCU_SCHED}"

# Redirect CANN process logs (plog) into this sample's ./log instead of the
# default /root/ascend/log. Structure under it is still debug/plog/plog-*.log.
export ASCEND_PROCESS_LOG_PATH="${ASCEND_PROCESS_LOG_PATH:-${SCRIPT_DIR}/log}"
mkdir -p "${ASCEND_PROCESS_LOG_PATH}"

jobs=4
if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc)"
fi

# BUILD_ONLY=1: configure and compile only, skip the exec step.
# EXEC_ONLY=1: run the exec step only, skip configure and compile.
if [[ "${EXEC_ONLY:-0}" != "1" ]]; then
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
        -DASCEND_CANN_PACKAGE_PATH="${ASCEND_HOME_PATH}" \
        -DCMAKE_ASC_ARCHITECTURES=dav-3510
    cmake --build "${BUILD_DIR}" -j"${jobs}"

    if [[ "${BUILD_ONLY:-0}" == "1" ]]; then
        exit 0
    fi
fi

exec "${BUILD_DIR}/ccu_all_to_all"
