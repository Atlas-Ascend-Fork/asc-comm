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

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 <nranks> [mode]"
    echo "  mode: single (default) | batch_last | multi_lane"
    exit 1
fi

nranks="$1"
mode="${2:-single}"

case "$mode" in
    single | batch_last | multi_lane) ;;
    *)
        echo "Unknown mode: $mode"
        echo "  mode: single (default) | batch_last | multi_lane"
        exit 1
        ;;
esac

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_write"

if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    exit 1
fi

# Rank 0 writes HCCL root info to a temp file; the other ranks read it before calling
# HcclCommInitRootInfo. A fresh directory per run avoids reusing stale root info.
root_info_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_write_root_info.XXXXXX")"
root_info_file="${root_info_dir}/root_info.bin"

# A fresh directory per run, so markers left behind by an earlier run cannot make a
# barrier pass immediately.
sync_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_write_sync.XXXXXX")"
export SIMT_WRITE_SYNC_DIR="$sync_dir"
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
echo "Starting $nranks processes..."

for ((rank = 0; rank < nranks; ++rank)); do
    "$binary" "$rank" "$nranks" "$root_info_file" "$mode" &
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

exit "$status"
