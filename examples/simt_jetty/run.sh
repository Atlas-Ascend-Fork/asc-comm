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
Usage: run.sh <nranks> [all|thread|warp|warp-pad|group|dispatch|mixed] [options]

Options:
  --iterations N  Number of correctness iterations, default: 1
  --warmup N      Warmup iterations for --timing, default: 0
  --timing        Run all iterations in one kernel and report device clock cycles
EOF
}

if [ "$#" -lt 1 ]; then
    usage
    exit 1
fi

nranks="$1"
shift
requested_mode="all"
if [ "$#" -gt 0 ] && [[ "$1" != --* ]]; then
    requested_mode="$1"
    shift
fi
iterations=1
warmup=0
timing=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --iterations)
            [ "$#" -ge 2 ] || { echo "--iterations requires a value" >&2; exit 1; }
            iterations="$2"
            shift 2
            ;;
        --warmup)
            [ "$#" -ge 2 ] || { echo "--warmup requires a value" >&2; exit 1; }
            warmup="$2"
            shift 2
            ;;
        --timing) timing=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi
if ! [[ "$iterations" =~ ^[0-9]+$ ]] || [ "$iterations" -lt 1 ]; then
    echo "--iterations must be a positive integer"
    exit 1
fi
if ! [[ "$warmup" =~ ^[0-9]+$ ]]; then
    echo "--warmup must be a non-negative integer"
    exit 1
fi
if [ "$timing" -eq 0 ] && [ "$warmup" -ne 0 ]; then
    echo "--warmup requires --timing"
    exit 1
fi
if [ "$timing" -eq 1 ] && [ "$requested_mode" = "mixed" ]; then
    echo "mixed mode does not support device timing"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_jetty"

if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    echo "build it first: bash ${script_dir}/build.sh"
    exit 1
fi

pids=()
active_dirs=()
cleanup() {
    if [ ${#pids[@]} -ne 0 ]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    if [ ${#active_dirs[@]} -ne 0 ]; then
        rm -rf "${active_dirs[@]}"
    fi
}
trap cleanup EXIT
trap 'exit 1' SIGINT SIGTERM

mode_id() {
    case "$1" in
        thread) echo 0 ;;
        warp-pad) echo 1 ;;
        group) echo 2 ;;
        dispatch) echo 3 ;;
        warp) echo 4 ;;
        mixed) echo 5 ;;
        *) return 1 ;;
    esac
}

run_mode() {
    local mode_name="$1"
    local id
    id="$(mode_id "$mode_name")"
    local root_info_dir
    local sync_dir
    root_info_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_jetty_root_info.XXXXXX")"
    sync_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_jetty_sync.XXXXXX")"
    active_dirs=("$root_info_dir" "$sync_dir")
    local root_info_file="${root_info_dir}/root_info.bin"

    echo "[Running] mode=${mode_name} iterations=${iterations} warmup=${warmup} timing=${timing}"
    pids=()
    for ((rank = 0; rank < nranks; ++rank)); do
        "$binary" "$rank" "$nranks" "$root_info_file" "$sync_dir" "$id" "$iterations" "$warmup" "$timing" &
        pids+=("$!")
    done

    local mode_status=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            mode_status=1
        fi
    done
    pids=()
    rm -rf "$root_info_dir" "$sync_dir"
    active_dirs=()
    return "$mode_status"
}

status=0
if [ "$requested_mode" = "all" ]; then
    modes=(thread warp warp-pad group dispatch)
    if [ "$timing" -eq 0 ]; then
        modes+=(mixed)
    fi
else
    if ! mode_id "$requested_mode" >/dev/null; then
        usage
        exit 1
    fi
    modes=("$requested_mode")
fi

for mode in "${modes[@]}"; do
    if ! run_mode "$mode"; then
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "RESULT | Status=PASS"
else
    echo "RESULT | Status=FAIL"
fi
exit "$status"
