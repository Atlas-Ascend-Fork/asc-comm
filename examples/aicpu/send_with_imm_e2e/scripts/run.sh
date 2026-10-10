#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SKIP_BUILD=false

usage() {
    cat <<'EOF'
Usage: run.sh [--skip-build] [environment variables]

Environment variables:
  SERVER_TCP_PORT  TCP control port, default 49152
  CLIENT_HOST      Server address, default 127.0.0.1
  DEVICE_ID        NPU device ID, default 0
  PAYLOAD_BYTES    Payload size in bytes, default 64
  IMM_DATA         32-bit immediate data, default 305419896
  SEND_COUNT       Number of messages, default 1
  RECV_SLOTS       Pre-posted receive slots, default 8
  BUILD_JOBS       Build job count, default nproc
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if [[ "${1:-}" == "--skip-build" ]]; then
    SKIP_BUILD=true
    shift
fi
if [[ $# -gt 0 ]]; then
    echo "Unknown argument: $1" >&2
    usage
    exit 2
fi

if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "ASCEND_HOME_PATH is not set. Source the CANN set_env.sh script first." >&2
    exit 2
fi

if [[ "${SKIP_BUILD}" == false ]]; then
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
    cmake --build "${BUILD_DIR}" --parallel "${BUILD_JOBS:-$(nproc)}"
fi

KERNEL_JSON="${BUILD_DIR}/send_with_imm_aicpu.json"
if [[ ! -f "${KERNEL_JSON}" ]]; then
    echo "Kernel metadata was not found: ${KERNEL_JSON}" >&2
    exit 2
fi

SERVER_TCP_PORT="${SERVER_TCP_PORT:-49152}"
CLIENT_HOST="${CLIENT_HOST:-127.0.0.1}"
DEVICE_ID="${DEVICE_ID:-0}"
PAYLOAD_BYTES="${PAYLOAD_BYTES:-64}"
IMM_DATA="${IMM_DATA:-305419896}"
SEND_COUNT="${SEND_COUNT:-1}"
RECV_SLOTS="${RECV_SLOTS:-8}"

server_pid=""
cleanup() {
    if [[ -n "${server_pid}" ]]; then
        kill -TERM "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

"${BUILD_DIR}/send_with_imm_server" \
    --port "${SERVER_TCP_PORT}" \
    --len "${PAYLOAD_BYTES}" \
    --imm "${IMM_DATA}" \
    --count "${SEND_COUNT}" \
    --recv-slots "${RECV_SLOTS}" &
server_pid=$!

client_status=0
"${BUILD_DIR}/send_with_imm_client" \
    --host "${CLIENT_HOST}" \
    --port "${SERVER_TCP_PORT}" \
    --device "${DEVICE_ID}" \
    --len "${PAYLOAD_BYTES}" \
    --imm "${IMM_DATA}" \
    --count "${SEND_COUNT}" \
    --kernel-json "${KERNEL_JSON}" || client_status=$?
server_status=0
wait "${server_pid}" || server_status=$?
server_pid=""
if [[ "${client_status}" -ne 0 || "${server_status}" -ne 0 ]]; then
    exit 1
fi

