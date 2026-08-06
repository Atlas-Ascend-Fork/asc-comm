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
Usage:
  run.sh <nranks> <rank_table.json> notify [options]

Options:
  --loop-count N             Timed WQE count, default: 1024
  --warmup N                 Warmup WQE count, default: 100
  --threads 1                Only the validated single-lane path is enabled
  --exponent-range MIN MAX   Notify payload range 2^MIN..2^MAX B, default: 3 17
  --simt-only                Accepted for compatibility; this stage is SIMT-only
  --log FILE                 Output log path
EOF
}

if [ "$#" -lt 3 ]; then
    usage
    exit 1
fi

nranks="$1"
rank_table_file="$2"
mode="$3"
shift 3

loop_count=1024
warmup=100
threads="1"
min_exp=3
max_exp=17
log_file=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --loop-count)
            loop_count="$2"
            shift 2
            ;;
        --warmup)
            warmup="$2"
            shift 2
            ;;
        --threads)
            threads="$2"
            shift 2
            ;;
        --exponent-range)
            min_exp="$2"
            max_exp="$3"
            shift 3
            ;;
        --log)
            log_file="$2"
            shift 2
            ;;
        --simt-only)
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi
if [ "$mode" != "notify" ]; then
    echo "this minimal benchmark currently supports only notify"
    exit 1
fi
if ! [[ "$loop_count" =~ ^[0-9]+$ ]] || [ "$loop_count" -lt 1 ]; then
    echo "--loop-count must be a positive integer"
    exit 1
fi
if ! [[ "$warmup" =~ ^[0-9]+$ ]]; then
    echo "--warmup must be a non-negative integer"
    exit 1
fi
if ! [[ "$min_exp" =~ ^[0-9]+$ ]] || ! [[ "$max_exp" =~ ^[0-9]+$ ]] || \
    [ "$min_exp" -gt "$max_exp" ] || [ "$max_exp" -gt 30 ]; then
    echo "exponents must satisfy 0 <= MIN <= MAX <= 30"
    exit 1
fi
if [ ! -f "$rank_table_file" ]; then
    echo "rank table file does not exist: $rank_table_file"
    exit 1
fi

IFS=',' read -r -a lane_values <<< "$threads"
if [ "${#lane_values[@]}" -eq 0 ]; then
    echo "--threads must contain at least one lane count"
    exit 1
fi
if [ "${#lane_values[@]}" -ne 1 ] || [ "${lane_values[0]}" != "1" ]; then
    echo "this minimal benchmark requires --threads 1"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_notify_atomic_perf"
if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    exit 1
fi

mkdir -p "${script_dir}/results"
if [ -z "$log_file" ]; then
    log_file="${script_dir}/results/${mode}_$(date +%Y%m%d_%H%M%S).log"
fi
mkdir -p "$(dirname "$log_file")"
exec > >(tee "$log_file") 2>&1

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_notify_atomic_perf.XXXXXX")"
pids=()
cleanup() {
    if [ "${#pids[@]}" -ne 0 ]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    rm -rf -- "$work_dir"
}
trap cleanup EXIT SIGINT SIGTERM

run_case() {
    local path="$1"
    local operation="$2"
    local bytes="$3"
    local lanes="$4"
    local case_dir="${work_dir}/${path}_${operation}_${bytes}_t${lanes}"
    mkdir -p "$case_dir"
    export SIMT_NOTIFY_ATOMIC_SYNC_DIR="$case_dir"

    pids=()
    for ((rank = 0; rank < nranks; ++rank)); do
        "$binary" "$rank" "$nranks" "$rank_table_file" "$path" "$operation" \
            "$loop_count" "$warmup" "$bytes" "$lanes" \
            >"${case_dir}/rank_${rank}.log" 2>&1 &
        pids+=("$!")
    done

    local status=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            status=1
        fi
    done
    pids=()

    cat "${case_dir}/rank_0.log"
    if [ "$status" -ne 0 ]; then
        for ((rank = 1; rank < nranks; ++rank)); do
            cat "${case_dir}/rank_${rank}.log"
        done
    fi
    return "$status"
}

echo "计时范围：仅统计WQE填写和敲DB；预热Drain及最终Drain/PollCq不计入耗时。"
echo "WQE数量：$loop_count；预热次数：$warmup；SIMT lane数：$threads"

for ((exp = min_exp; exp <= max_exp; ++exp)); do
    bytes=$((1 << exp))
    run_case simt notify "$bytes" 1
    echo
done

echo "日志文件：$log_file"
echo "绘图命令：python3 ${script_dir}/plot.py $log_file"
