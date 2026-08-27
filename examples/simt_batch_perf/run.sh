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

usage() {
    cat <<'EOF'
Usage: run.sh [write|read|notify|write_value|faa|cas|all] [options]
Options:
  --iterations N      Timed operation count, default: 1024
  --warmup N          Warmup operation count, default: 100
  --payload-bytes N   Write/Read/Notify payload, default: 64
  --batch-size N      Operations per explicit batch (1..32), default: 8
  --devices N         Device count, 2 or 8; default: 2
  --mode MODE         compare, immediate, batch, or group; default: compare
  --log FILE          Output log file
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then usage; exit 0; fi
api="${1:-all}"
if [[ $# -gt 0 ]]; then shift; fi
iterations=1024
warmup=100
payload_bytes=64
batch_size=8
devices=2
mode=compare
log_file=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --payload-bytes) payload_bytes="$2"; shift 2 ;;
        --batch-size) batch_size="$2"; shift 2 ;;
        --devices) devices="$2"; shift 2 ;;
        --mode) mode="$2"; shift 2 ;;
        --log) log_file="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

valid_api() {
    [[ "$1" == "write" || "$1" == "read" || "$1" == "notify" || "$1" == "write_value" ||
        "$1" == "faa" || "$1" == "cas" ]]
}
[[ "$api" == "all" ]] || valid_api "$api" || { echo "invalid API: $api" >&2; exit 1; }
[[ "$iterations" =~ ^[0-9]+$ && "$iterations" -gt 0 ]] || { echo "iterations must be positive" >&2; exit 1; }
[[ "$warmup" =~ ^[0-9]+$ ]] || { echo "warmup must be non-negative" >&2; exit 1; }
[[ "$payload_bytes" =~ ^[0-9]+$ && "$payload_bytes" -ge 2 ]] || { echo "payload must be at least 2" >&2; exit 1; }
[[ "$batch_size" =~ ^[0-9]+$ && "$batch_size" -ge 1 && "$batch_size" -le 32 ]] || {
    echo "batch size must be 1..32" >&2; exit 1;
}
[[ "$devices" == "2" || "$devices" == "8" ]] || { echo "devices must be 2 or 8" >&2; exit 1; }
[[ "$mode" == "compare" || "$mode" == "immediate" || "$mode" == "batch" || "$mode" == "group" ]] || {
    echo "invalid mode: $mode" >&2; exit 1;
}
if [[ "$mode" == "group" ]] && ((batch_size <= 1 || iterations % batch_size != 0 || warmup % batch_size != 0)); then
    echo "group mode requires batch size > 1 and iterations/warmup divisible by batch size" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_batch_perf"
[[ -x "$binary" ]] || { echo "build first: bash ${script_dir}/build.sh" >&2; exit 1; }
code_version="$(git -C "${script_dir}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
mkdir -p "${script_dir}/results"
if [[ -z "$log_file" ]]; then
    log_file="${script_dir}/results/${api}_$(date +%Y%m%d_%H%M%S).log"
fi
mkdir -p "$(dirname "$log_file")"
exec > >(tee "$log_file") 2>&1
echo "TEST_INFO | Suite=simt_batch_perf | CodeVersion=${code_version} | Devices=${devices} | Pairs=$((devices / 2)) | Iterations=${iterations} | Warmup=${warmup} | PayloadBytes=${payload_bytes} | BatchSize=${batch_size}"
apis=(write read notify write_value faa cas)
[[ "$api" == "all" ]] || apis=("$api")
modes=(immediate batch)
[[ "$mode" == "compare" ]] || modes=("$mode")
active_dir=""
active_pids=()
cleanup() {
    if [[ ${#active_pids[@]} -ne 0 ]]; then kill "${active_pids[@]}" 2>/dev/null || true; fi
    if [[ -n "$active_dir" ]]; then rm -rf -- "$active_dir"; fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

run_case() {
    local case_api="$1" case_mode="$2" work_dir root_info_file status pid rank
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_batch_perf.XXXXXX")"
    active_dir="$work_dir"
    root_info_file="${work_dir}/root_info.bin"
    export SIMT_BATCH_PERF_SYNC_DIR="$work_dir"
    active_pids=()
    for ((rank = 0; rank < devices; ++rank)); do
        "$binary" "$rank" "$devices" "$root_info_file" "$case_api" "$iterations" "$warmup" \
            "$payload_bytes" "$batch_size" "$case_mode" >"${work_dir}/rank_${rank}.log" 2>&1 &
        active_pids+=("$!")
    done
    status=0
    for pid in "${active_pids[@]}"; do
        if ! wait "$pid"; then status=1; fi
    done
    active_pids=()
    for ((rank = 0; rank < devices; ++rank)); do cat "${work_dir}/rank_${rank}.log"; done
    local mode_name=Immediate
    [[ "$case_mode" == batch ]] && mode_name=ExplicitBatch
    [[ "$case_mode" == group ]] && mode_name=GroupBatch
    echo "CASE | API=${case_api} | Mode=${mode_name} | CodeVersion=${code_version} | Devices=${devices} | Pairs=$((devices / 2)) | Status=$([[ "$status" -eq 0 ]] && echo PASS || echo FAIL)"
    rm -rf -- "$work_dir"
    active_dir=""
    return "$status"
}

status=0
for current_api in "${apis[@]}"; do
    for current_mode in "${modes[@]}"; do
        if ! run_case "$current_api" "$current_mode"; then status=1; fi
    done
done
echo "Log file: $log_file"
exit "$status"
