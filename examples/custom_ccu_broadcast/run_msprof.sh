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
LOG_DIR="${LOG_DIR:-${SCRIPT_DIR}/log}"
MSPROF_OUTPUT_DIR="${MSPROF_OUTPUT_DIR:-${SCRIPT_DIR}/msprof_out}"
CCU_INSTR_DUMP_DIR="${CCU_INSTR_DUMP_DIR:-${SCRIPT_DIR}/ccu_instr_dump}"
DUMP_CCU_INSTR="${DUMP_CCU_INSTR:-1}"
MSPROF_TASK_TIME="${MSPROF_TASK_TIME:-off}"
MSPROF_RUNTIME_API="${MSPROF_RUNTIME_API:-off}"
MSPROF_ASCENDCL="${MSPROF_ASCENDCL:-off}"
MSPROF_HCCL="${MSPROF_HCCL:-off}"
MSPROF_SYS_PROFILING="${MSPROF_SYS_PROFILING:-on}"
MSPROF_SYS_DEVICES="${MSPROF_SYS_DEVICES:-${DEVICES:-all}}"
MSPROF_SYS_INTERCONNECTION="${MSPROF_SYS_INTERCONNECTION:-on}"
MSPROF_SYS_INTERCONNECTION_FREQ="${MSPROF_SYS_INTERCONNECTION_FREQ:-50}"

if ! command -v msprof >/dev/null 2>&1; then
    echo "msprof not found; source CANN set_env.sh first." >&2
    exit 2
fi

export HCCL_OP_EXPANSION_MODE="${HCCL_OP_EXPANSION_MODE:-CCU_SCHED}"
export ASCEND_PROCESS_LOG_PATH="${ASCEND_PROCESS_LOG_PATH:-${LOG_DIR}}"
mkdir -p "${ASCEND_PROCESS_LOG_PATH}"

BUILD_ONLY=1 BUILD_DIR="${BUILD_DIR}" bash "${SCRIPT_DIR}/run.sh"

if [[ "${DUMP_CCU_INSTR}" == "1" ]]; then
    export HCOMM_CCU_INSTR_DUMP=1
    export HCOMM_CCU_INSTR_DUMP_PATH="${CCU_INSTR_DUMP_DIR}"
    export HCOMM_CCU_INSTR_DUMP_DIR="${CCU_INSTR_DUMP_DIR}"
    mkdir -p "${CCU_INSTR_DUMP_DIR}"
fi

mkdir -p "${MSPROF_OUTPUT_DIR}"

msprof_args=(--output="${MSPROF_OUTPUT_DIR}")

[[ "${MSPROF_TASK_TIME}" != "off" ]] && msprof_args+=(--task-time="${MSPROF_TASK_TIME}")
[[ "${MSPROF_RUNTIME_API}" != "off" ]] && msprof_args+=(--runtime-api="${MSPROF_RUNTIME_API}")
[[ "${MSPROF_ASCENDCL}" != "off" ]] && msprof_args+=(--ascendcl="${MSPROF_ASCENDCL}")
[[ "${MSPROF_HCCL}" == "on" ]] && msprof_args+=(--hccl=on)
[[ "${MSPROF_SYS_PROFILING}" == "on" ]] && msprof_args+=(--sys-profiling=on)
[[ "${MSPROF_SYS_PROFILING}" == "on" ]] && msprof_args+=(--sys-devices="${MSPROF_SYS_DEVICES}")
[[ "${MSPROF_SYS_INTERCONNECTION}" == "on" ]] && msprof_args+=(--sys-interconnection-profiling=on)
[[ "${MSPROF_SYS_INTERCONNECTION}" == "on" ]] && msprof_args+=(--sys-interconnection-freq="${MSPROF_SYS_INTERCONNECTION_FREQ}")

exec msprof \
    "${msprof_args[@]}" \
    "${BUILD_DIR}/custom_broadcast_ccu"
