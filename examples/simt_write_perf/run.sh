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
  run.sh <nranks> <write|write_value> [options]

Options:
  --iterations N      Timed WQE count, default: 1024
  --warmup N          Warmup WQE count, default: 100
  --payload-bytes N   WriteNbi payload bytes, default: 4096; minimum 2.
                      write_value always carries 8B inline and ignores this.
  --commit-mode MODE  immediate (one doorbell per WQE) or last (defer all but
                      the last); default: immediate
  --sweep             Run a payload sweep: 8 16 64 256 1024 4096 16384 65536.
                      Only meaningful for write; ignores --payload-bytes.
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
commit_mode=immediate
sweep=0
log_file=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --payload-bytes) payload_bytes="$2"; shift 2 ;;
        --commit-mode) commit_mode="$2"; shift 2 ;;
        --sweep) sweep=1; shift ;;
        --log) log_file="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

[[ "$api" == "write" || "$api" == "write_value" ]] || {
    echo "api must be write or write_value" >&2; exit 1;
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
# A committed WriteNbi splits its payload across two SGEs and an SGE length may not be 0,
# so a single byte cannot be split.
[[ "$payload_bytes" =~ ^[0-9]+$ && "$payload_bytes" -ge 2 ]] || {
    echo "--payload-bytes must be an integer greater than or equal to 2" >&2; exit 1;
}
[[ "$commit_mode" == "immediate" || "$commit_mode" == "last" ]] || {
    echo "--commit-mode must be immediate or last" >&2; exit 1;
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_write_perf"
[[ -x "$binary" ]] || { echo "build the example first: bash ${script_dir}/build.sh" >&2; exit 1; }

mkdir -p "${script_dir}/results"
if [[ -z "$log_file" ]]; then
    suffix=$([[ "$sweep" -eq 1 ]] && echo "sweep" || echo "p${payload_bytes}")
    log_file="${script_dir}/results/${api}_${commit_mode}_${suffix}_$(date +%Y%m%d_%H%M%S).log"
fi
mkdir -p "$(dirname "$log_file")"

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_write_perf.XXXXXX")"
pids=()
cleanup() {
    if [[ ${#pids[@]} -ne 0 ]]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    rm -rf -- "$work_dir"
}
trap cleanup EXIT INT TERM

if [[ "$sweep" -eq 1 ]]; then
    payloads=(8 16 64 256 1024 4096 16384 65536)
else
    payloads=("$payload_bytes")
fi

status=0
: >"${work_dir}/combined.log"
for payload in "${payloads[@]}"; do
    # A fresh sync dir per payload: stale markers from the previous run would let the
    # barrier pass immediately. The root info file lives in the same directory for the
    # same reason -- rank 0 must publish fresh root info for every communicator.
    run_dir="${work_dir}/p${payload}"
    mkdir -p "$run_dir"
    export SIMT_WRITE_PERF_SYNC_DIR="$run_dir"
    root_info_file="${run_dir}/root_info.bin"

    pids=()
    for ((rank = 0; rank < nranks; ++rank)); do
        "$binary" "$rank" "$nranks" "$root_info_file" "$api" \
            "$iterations" "$warmup" "$payload" "$commit_mode" \
            >"${run_dir}/rank_${rank}.log" 2>&1 &
        pids+=("$!")
    done
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            status=1
        fi
    done
    pids=()

    for ((rank = 0; rank < nranks; ++rank)); do
        cat "${run_dir}/rank_${rank}.log" >>"${work_dir}/combined.log"
    done
done

tee "$log_file" <"${work_dir}/combined.log"

echo "日志文件：$log_file"
exit "$status"
