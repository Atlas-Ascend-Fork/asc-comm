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
  run.sh <nranks> <notify|faa|cas> [options]

Options:
  --iterations N      Timed WQE count, default: 1024
  --warmup N          Warmup WQE count, default: 100
  --payload-bytes N   Notify payload bytes, default: 4096; FAA/CAS always use 8B
  --lanes N           SIMT lanes: 1, 32, or 1024; default: 1
  --commit-mode MODE  immediate or last; default: immediate
  --log FILE          Output log file
EOF
}

if [[ $# -lt 2 ]]; then
    usage
    exit 1
fi

nranks="$1"
api="$2"
shift 2
iterations=1024
warmup=100
payload_bytes=4096
lanes=1
commit_mode=immediate
log_file=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --payload-bytes) payload_bytes="$2"; shift 2 ;;
        --lanes) lanes="$2"; shift 2 ;;
        --commit-mode) commit_mode="$2"; shift 2 ;;
        --log) log_file="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

[[ "$api" == "notify" || "$api" == "faa" || "$api" == "cas" ]] || {
    echo "api must be notify, faa, or cas" >&2; exit 1;
}
[[ "$nranks" =~ ^[0-9]+$ && "$nranks" -ge 2 ]] || {
    echo "nranks must be an integer greater than or equal to 2" >&2; exit 1;
}
[[ "$iterations" =~ ^[0-9]+$ && "$iterations" -gt 0 ]] || {
    echo "--iterations must be a positive integer" >&2; exit 1;
}
[[ "$warmup" =~ ^[0-9]+$ ]] || {
    echo "--warmup must be a non-negative integer" >&2; exit 1;
}
[[ "$payload_bytes" =~ ^[0-9]+$ && "$payload_bytes" -gt 0 ]] || {
    echo "--payload-bytes must be a positive integer" >&2; exit 1;
}
[[ "$lanes" == "1" || "$lanes" == "32" || "$lanes" == "1024" ]] || {
    echo "--lanes must be 1, 32, or 1024" >&2; exit 1;
}
[[ "$commit_mode" == "immediate" || "$commit_mode" == "last" ]] || {
    echo "--commit-mode must be immediate or last" >&2; exit 1;
}
[[ "$commit_mode" == "last" || "$lanes" == "1" ]] || {
    echo "--lanes 32/1024 requires --commit-mode last" >&2; exit 1;
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_notify_atomic_perf"
[[ -x "$binary" ]] || { echo "build the example first: bash ${script_dir}/build.sh" >&2; exit 1; }

# Rank 0 writes HCCL root info to a temp file; the other ranks read it before calling
# HcclCommInitRootInfo. A fresh directory per run avoids reusing stale root info.
root_info_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_notify_atomic_perf_root_info.XXXXXX")"
root_info_file="${root_info_dir}/root_info.bin"

mkdir -p "${script_dir}/results"
if [[ -z "$log_file" ]]; then
    log_file="${script_dir}/results/${api}_t${lanes}_${commit_mode}_$(date +%Y%m%d_%H%M%S).log"
fi
mkdir -p "$(dirname "$log_file")"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_notify_atomic_perf.XXXXXX")"
pids=()
cleanup() {
    if [[ ${#pids[@]} -ne 0 ]]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    rm -rf -- "$work_dir" "$root_info_dir"
}
trap cleanup EXIT INT TERM
export SIMT_NOTIFY_ATOMIC_SYNC_DIR="$work_dir"

for ((rank = 0; rank < nranks; ++rank)); do
    "$binary" "$rank" "$nranks" "$root_info_file" "$api" \
        "$iterations" "$warmup" "$payload_bytes" "$lanes" "$commit_mode" \
        >"${work_dir}/rank_${rank}.log" 2>&1 &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        status=1
    fi
done
pids=()

for ((rank = 0; rank < nranks; ++rank)); do
    cat "${work_dir}/rank_${rank}.log"
done | tee "$log_file"

if [[ "$status" -eq 0 ]]; then
    echo "STATUS | API=$api | Path=SIMT-t$lanes | CommitMode=$commit_mode | PASS"
else
    echo "STATUS | API=$api | Path=SIMT-t$lanes | CommitMode=$commit_mode | FAIL"
fi
echo "日志文件：$log_file"
exit "$status"
