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

MODES="write_single | write_batch_last | write_value_single | write_value_batch_last
        notify | faa | cas | single (default) | batch_last | notify_immediate_repeat"

usage() {
    echo "Usage: $0 <nranks> [mode]"
    echo "  mode: ${MODES}"
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 1
fi

nranks="$1"
mode="${2:-single}"

case "$mode" in
    write_single | write_batch_last | write_value_single | write_value_batch_last) ;;
    notify | faa | cas | single | batch_last | notify_immediate_repeat) ;;
    *)
        echo "Unknown mode: $mode"
        usage
        exit 1
        ;;
esac

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_urma"

if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    exit 1
fi

# Rank 0 writes HCCL root info to a temp file; the other ranks read it before calling
# HcclCommInitRootInfo. A fresh directory per run avoids reusing stale root info.
root_info_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_urma_root_info.XXXXXX")"
root_info_file="${root_info_dir}/root_info.bin"

# A fresh directory per run, so markers left behind by an earlier run cannot make a
# barrier pass immediately. Passed to the binary as an argument rather than exported: an
# environment variable is easy to lose across a wrapper or scheduler, and losing it silently
# sent every rank to a shared default directory.
sync_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_urma_sync.XXXXXX")"
trap 'rm -rf "$sync_dir" "$root_info_dir"' EXIT

pids=()
cleanup() {
    echo -e "\n[Terminating] Caught Ctrl+C, killing background processes..."
    if [ ${#pids[@]} -ne 0 ]; then
        kill "${pids[@]}" 2>/dev/null
    fi
    exit 1
}
trap cleanup SIGINT SIGTERM

for ((rank = 0; rank < nranks; ++rank)); do
    "$binary" "$rank" "$nranks" "$root_info_file" "$sync_dir" "$mode" &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    # Without tolerating a non-zero wait, errexit would abort the loop and leave the
    # remaining ranks unreaped.
    if ! wait "$pid"; then
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "RESULT | Mode=$mode | Status=PASS"
else
    echo "RESULT | Mode=$mode | Status=FAIL"
fi
exit "$status"
