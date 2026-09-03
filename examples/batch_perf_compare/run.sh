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
Usage: run.sh [simd|multi-jetty|all] [options]
Options:
  --api API            write, read, notify, or all; default: all
  --batch-sizes LIST   Batch sizes; default: 1,8,32,64,128,256
  --jetty-counts LIST  Jetty counts; default: 1,2,4,8
  --iterations N       Timed operation count; default: 1024
  --warmup N           Requested warmup count; default: 96 (rounded to a full batch)
  --payload-bytes N    Write/Read/Notify payload; default: 64
  --devices N          Device count, 2 or 8; default: 2
  --log FILE           Output log file
EOF
}

path="${1:-all}"
if [[ $# -gt 0 ]]; then shift; fi
api=all
batch_sizes="1,8,32,64,128,256"
jetty_counts="1,2,4,8"
iterations=1024
warmup=96
payload_bytes=64
devices=2
log_file=""
case_timeout_seconds="${BATCH_PERF_CASE_TIMEOUT_SECONDS:-120}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --api) api="$2"; shift 2 ;;
        --batch-sizes) batch_sizes="$2"; shift 2 ;;
        --jetty-counts) jetty_counts="$2"; shift 2 ;;
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --payload-bytes) payload_bytes="$2"; shift 2 ;;
        --devices) devices="$2"; shift 2 ;;
        --log) log_file="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done
[[ "$path" == "simd" || "$path" == "multi-jetty" || "$path" == "all" ]] || { usage; exit 1; }
[[ "$api" == "write" || "$api" == "read" || "$api" == "notify" || "$api" == "all" ]] || {
    echo "invalid API: $api" >&2; exit 1;
}
[[ "$devices" == "2" || "$devices" == "8" ]] || { echo "devices must be 2 or 8" >&2; exit 1; }
[[ "$iterations" =~ ^[0-9]+$ && "$iterations" -gt 0 ]] || { echo "iterations must be positive" >&2; exit 1; }
[[ "$warmup" =~ ^[0-9]+$ ]] || { echo "warmup must be non-negative" >&2; exit 1; }
[[ "$payload_bytes" =~ ^[0-9]+$ && "$payload_bytes" -ge 2 ]] || { echo "payload must be at least 2" >&2; exit 1; }

IFS=',' read -r -a simd_batches <<< "$batch_sizes"
for batch in "${simd_batches[@]}"; do
    [[ "$batch" == "1" || "$batch" == "8" || "$batch" == "32" || "$batch" == "64" ||
       "$batch" == "128" || "$batch" == "256" ]] && ((iterations % batch == 0)) || {
        echo "Batch sizes must be selected from 1,8,32,64,128,256 and divide iterations" >&2; exit 1;
    }
done
IFS=',' read -r -a jettys <<< "$jetty_counts"
for count in "${jettys[@]}"; do
    [[ "$count" == "1" || "$count" == "2" || "$count" == "4" || "$count" == "8" ]] || {
        echo "Jetty counts must be selected from 1,2,4,8" >&2; exit 1;
    }
done
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/batch_perf_compare"
[[ -x "$binary" ]] || { echo "build first: bash ${script_dir}/build.sh" >&2; exit 1; }
mkdir -p "${script_dir}/results"
if [[ -z "$log_file" ]]; then
    log_file="${script_dir}/results/${path}_$(date +%Y%m%d_%H%M%S).log"
fi
mkdir -p "$(dirname "$log_file")"
exec > >(tee "$log_file") 2>&1
echo "TEST_INFO | Suite=batch_perf_compare | Path=${path} | Devices=${devices} | Iterations=${iterations} | Warmup=${warmup} | PayloadBytes=${payload_bytes} | SimdBatchSizes=${batch_sizes} | JettyCounts=${jetty_counts}"

apis=(write read notify)
[[ "$api" == "all" ]] || apis=("$api")
active_dir=""
active_pids=()
cleanup() {
    if [[ ${#active_pids[@]} -ne 0 ]]; then kill "${active_pids[@]}" 2>/dev/null || true; fi
    if [[ -n "$active_dir" ]]; then rm -rf -- "$active_dir"; fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

run_case() {
    local case_path="$1" case_api="$2" batch="$3" jetty_count="$4"
    local case_warmup work_dir root_file rank status pair_status perf_line
    case_warmup=$(( (warmup + batch - 1) / batch * batch ))
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/batch_perf_compare.XXXXXX")"
    active_dir="$work_dir"
    root_file="${work_dir}/root_info.bin"
    export BATCH_PERF_COMPARE_SYNC_DIR="$work_dir"
    echo "CASE_START | Path=${case_path} | API=${case_api} | BatchSize=${batch} | Jettys=${jetty_count}"
    active_pids=()
    for ((rank = 0; rank < devices; ++rank)); do
        timeout --signal=TERM "${case_timeout_seconds}s" \
            "$binary" "$rank" "$devices" "$root_file" "$case_path" "$case_api" "$iterations" "$case_warmup" \
            "$payload_bytes" "$batch" "$jetty_count" >"${work_dir}/rank_${rank}.log" 2>&1 &
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
        ((rank_status[rank] == 0 && rank_status[rank + 1] == 0)) || pair_status=FAIL
        perf_line="$(grep -m1 '^PERF_DATA |' "${work_dir}/rank_${rank}.log" || true)"
        if [[ -n "$perf_line" ]]; then
            echo "${perf_line/PERF_DATA/RESULT} | Status=${pair_status}"
        else
            echo "RESULT | Path=${case_path} | API=${case_api} | Status=FAIL"
        fi
        if [[ "$pair_status" == "FAIL" ]]; then
            cat "${work_dir}/rank_${rank}.log" "${work_dir}/rank_$((rank + 1)).log"
        fi
    done
    rm -rf -- "$work_dir"
    active_dir=""
    return "$status"
}

overall=0
if [[ "$path" == "simd" || "$path" == "all" ]]; then
    for current_api in "${apis[@]}"; do
        for batch in "${simd_batches[@]}"; do
            run_case simd "$current_api" "$batch" 1 || overall=1
        done
    done
fi
if [[ "$path" == "multi-jetty" || "$path" == "all" ]]; then
    for current_api in "${apis[@]}"; do
        for batch in "${simd_batches[@]}"; do
            for count in "${jettys[@]}"; do
                run_case multi-jetty "$current_api" "$batch" "$count" || overall=1
            done
        done
    done
fi
echo "Log file: $log_file"
exit "$overall"
