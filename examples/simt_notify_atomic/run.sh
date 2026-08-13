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

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "Usage: $0 <nranks> <rankTableFilePath> [mode]"
    echo "  mode: notify | faa | cas | single (default) | batch_last | multi_lane | notify_immediate_repeat | warp_immediate"
    exit 1
fi

nranks="$1"
rank_table_file="$2"
mode="${3:-single}"

case "$mode" in
    notify | faa | cas | single | batch_last | multi_lane | notify_immediate_repeat | warp_immediate) ;;
    *)
        echo "Unknown mode: $mode"
        echo "  mode: notify | faa | cas | single (default) | batch_last | multi_lane | notify_immediate_repeat | warp_immediate"
        exit 1
        ;;
esac

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi

if [ ! -f "$rank_table_file" ]; then
    echo "rank table file does not exist: $rank_table_file"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_notify_atomic"

if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    exit 1
fi

# A fresh directory per run, so markers left behind by an earlier run cannot make a
# barrier pass immediately.
sync_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_notify_atomic_sync.XXXXXX")"
export SIMT_NOTIFY_ATOMIC_SYNC_DIR="$sync_dir"
trap 'rm -rf "$sync_dir"' EXIT

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
    "$binary" "$rank" "$nranks" "$rank_table_file" "$mode" &
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
