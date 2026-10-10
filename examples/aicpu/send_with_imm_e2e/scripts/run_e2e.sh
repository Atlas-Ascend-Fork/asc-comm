#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}/../../.."
BUILD_DIR="${ROOT_DIR}/build_send_with_imm_e2e"

if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "ASCEND_HOME_PATH is not set" >&2
    exit 2
fi

# Assume the newest hcomm run package has been installed into CANN before this test.
cmake -S "${ROOT_DIR}/examples/aicpu/send_with_imm_e2e" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

KERNEL_JSON="${BUILD_DIR}/send_with_imm_aicpu.json"
SERVER_TCP_PORT="${SERVER_TCP_PORT:-49152}"
CLIENT_HOST="${CLIENT_HOST:-127.0.0.1}"
DEVICE_ID="${DEVICE_ID:-0}"
PAYLOAD_BYTES="${PAYLOAD_BYTES:-64}"
IMM_DATA="${IMM_DATA:-305419896}"
SEND_COUNT="${SEND_COUNT:-1}"
RECV_SLOTS="${RECV_SLOTS:-8}"

"${BUILD_DIR}/send_with_imm_server" \
    --port "${SERVER_TCP_PORT}" \
    --len "${PAYLOAD_BYTES}" \
    --imm "${IMM_DATA}" \
    --count "${SEND_COUNT}" \
    --recv-slots "${RECV_SLOTS}" &
server_pid=$!
trap 'kill -TERM "${server_pid}" 2>/dev/null || true' EXIT

"${BUILD_DIR}/send_with_imm_client" \
    --host "${CLIENT_HOST}" \
    --port "${SERVER_TCP_PORT}" \
    --device "${DEVICE_ID}" \
    --len "${PAYLOAD_BYTES}" \
    --imm "${IMM_DATA}" \
    --count "${SEND_COUNT}" \
    --kernel-json "${KERNEL_JSON}"

wait "${server_pid}"
