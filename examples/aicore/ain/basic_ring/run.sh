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

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <nranks>"
    exit 1
fi

nranks="$1"
timeout_seconds=300

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/basic_ring_demo"

if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    exit 1
fi

root_info_dir="$(mktemp -d "${TMPDIR:-/tmp}/ain_basic_ring_root_info.XXXXXX")"
root_info_file="${root_info_dir}/root_info.bin"

pids=()

cleanup() {
    status=$?
    trap - EXIT INT TERM

    if [ "$status" -ne 0 ] && [ "${#pids[@]}" -gt 0 ]; then
        kill "${pids[@]}" 2>/dev/null || true
        wait "${pids[@]}" 2>/dev/null || true
    fi

    if [ -n "${root_info_dir:-}" ]; then
        rm -rf "$root_info_dir"
    fi
    exit "$status"
}

trap cleanup EXIT INT TERM

for ((rank = 0; rank < nranks; ++rank)); do
    timeout --foreground --kill-after=5s "${timeout_seconds}s" \
        "$binary" "$rank" "$nranks" "$root_info_file" &
    pids+=("$!")
done

remaining="${#pids[@]}"
while [ "$remaining" -gt 0 ]; do
    if wait -n; then
        remaining=$((remaining - 1))
    else
        status=$?
        echo "a rank process failed with status ${status}"
        exit "$status"
    fi
done
