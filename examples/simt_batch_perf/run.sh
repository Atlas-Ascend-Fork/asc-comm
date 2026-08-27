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
  --warmup N          Warmup operation count, default: 96
  --payload-bytes N   Write/Read/Notify payload, default: 64
  --batch-size N      Operations per batch (1..1024), default: 8
  --group-lanes LIST  GroupBatch lane counts, default: 2,4,8,16,32
  --devices N         Device count, 2 or 8; default: 2
  --mode MODE         compare, immediate, batch, or group; default: compare
  --log FILE          Output log file
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then usage; exit 0; fi
api="${1:-all}"
if [[ $# -gt 0 ]]; then shift; fi
iterations=1024
warmup=96
payload_bytes=64
batch_size=8
reference_batch_size=128
large_batch_size=1024
group_lanes="2,4,8,16,32"
devices=2
mode=compare
log_file=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --payload-bytes) payload_bytes="$2"; shift 2 ;;
        --batch-size) batch_size="$2"; shift 2 ;;
        --group-lanes) group_lanes="$2"; shift 2 ;;
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
[[ "$batch_size" =~ ^[0-9]+$ && "$batch_size" -ge 1 && "$batch_size" -le 1024 ]] || {
    echo "batch size must be 1..1024" >&2; exit 1;
}
[[ "$devices" == "2" || "$devices" == "8" ]] || { echo "devices must be 2 or 8" >&2; exit 1; }
[[ "$mode" == "compare" || "$mode" == "immediate" || "$mode" == "batch" || "$mode" == "group" ]] || {
    echo "invalid mode: $mode" >&2; exit 1;
}
IFS=',' read -r -a lane_counts <<< "$group_lanes"
[[ ${#lane_counts[@]} -gt 0 ]] || { echo "group lane list must not be empty" >&2; exit 1; }
if [[ "$mode" == "group" || "$mode" == "compare" ]]; then
    group_batch_size="$batch_size"
    [[ "$mode" == "group" ]] || group_batch_size="$reference_batch_size"
    ((iterations % group_batch_size == 0)) || {
        echo "iterations must be divisible by group batch size ${group_batch_size}" >&2; exit 1;
    }
    for lanes in "${lane_counts[@]}"; do
        [[ "$lanes" =~ ^[0-9]+$ ]] && ((lanes >= 2 && lanes <= 32)) || {
            echo "group lanes must be comma-separated values in 2..32" >&2; exit 1;
        }
        ((group_batch_size % lanes == 0 && iterations % lanes == 0 && warmup % lanes == 0)) || {
            echo "iterations/warmup must be divisible by every group lane count" >&2; exit 1;
        }
    done
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
echo "TEST_INFO | Suite=simt_batch_perf | CodeVersion=${code_version} | Devices=${devices} | Pairs=$((devices / 2)) | Iterations=${iterations} | Warmup=${warmup} | PayloadBytes=${payload_bytes} | PrimaryBatchSize=${batch_size} | ReferenceBatchSize=${reference_batch_size} | LargeBatchSize=${large_batch_size} | GroupLanes=${group_lanes}"
apis=(write read notify write_value faa cas)
[[ "$api" == "all" ]] || apis=("$api")
active_dir=""
active_pids=()
rank_status=()
cleanup() {
    if [[ ${#active_pids[@]} -ne 0 ]]; then kill "${active_pids[@]}" 2>/dev/null || true; fi
    if [[ -n "$active_dir" ]]; then rm -rf -- "$active_dir"; fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

run_case() {
    local case_api="$1" case_mode="$2" case_batch_size="$3" case_lanes="${4:-1}"
    local work_dir root_info_file status rank pair_status perf_line
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_batch_perf.XXXXXX")"
    active_dir="$work_dir"
    root_info_file="${work_dir}/root_info.bin"
    export SIMT_BATCH_PERF_SYNC_DIR="$work_dir"
    active_pids=()
    for ((rank = 0; rank < devices; ++rank)); do
        "$binary" "$rank" "$devices" "$root_info_file" "$case_api" "$iterations" "$warmup" \
            "$payload_bytes" "$case_batch_size" "$case_mode" "$case_lanes" \
            >"${work_dir}/rank_${rank}.log" 2>&1 &
        active_pids+=("$!")
    done
    status=0
    rank_status=()
    for ((rank = 0; rank < devices; ++rank)); do
        if wait "${active_pids[$rank]}"; then rank_status[$rank]=0; else rank_status[$rank]=1; status=1; fi
    done
    active_pids=()
    for ((rank = 0; rank < devices; rank += 2)); do
        pair_status=PASS
        if ((rank_status[rank] != 0 || rank_status[rank + 1] != 0)); then pair_status=FAIL; fi
        perf_line="$(grep -m1 '^PERF_DATA |' "${work_dir}/rank_${rank}.log" || true)"
        if [[ -n "$perf_line" ]]; then
            echo "${perf_line/PERF_DATA/RESULT} | Status=${pair_status}"
        else
            echo "RESULT | API=${case_api} | Status=FAIL"
        fi
        if [[ "$pair_status" == FAIL ]]; then
            cat "${work_dir}/rank_${rank}.log" "${work_dir}/rank_$((rank + 1)).log"
        fi
    done
    rm -rf -- "$work_dir"
    active_dir=""
    return "$status"
}

status=0
for current_api in "${apis[@]}"; do
    if [[ "$mode" == "compare" ]]; then
        if ! run_case "$current_api" immediate "$batch_size"; then status=1; fi
        if ! run_case "$current_api" batch "$batch_size"; then status=1; fi
        if [[ "$batch_size" != "$reference_batch_size" ]] &&
            ! run_case "$current_api" batch "$reference_batch_size"; then status=1; fi
        if [[ "$batch_size" != "$large_batch_size" ]] &&
            ! run_case "$current_api" batch "$large_batch_size"; then status=1; fi
        for lanes in "${lane_counts[@]}"; do
            if ! run_case "$current_api" group "$reference_batch_size" "$lanes"; then status=1; fi
        done
    elif [[ "$mode" == "group" ]]; then
        for lanes in "${lane_counts[@]}"; do
            if ! run_case "$current_api" group "$batch_size" "$lanes"; then status=1; fi
        done
    elif ! run_case "$current_api" "$mode" "$batch_size"; then
        status=1
    fi
done
echo "Log file: $log_file"
exit "$status"
