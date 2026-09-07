#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. Please make sure you use this file only in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set -euo pipefail

usage() {
    echo "Usage: $0 <nranks>"
}

if [ "$#" -ne 1 ]; then
    usage
    exit 1
fi

nranks="$1"
if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "nranks must be an integer greater than or equal to 2"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_jetty"

if [ ! -x "$binary" ]; then
    echo "binary does not exist or is not executable: $binary"
    echo "build it first: bash ${script_dir}/build.sh"
    exit 1
fi

root_info_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_jetty_root_info.XXXXXX")"
sync_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_jetty_sync.XXXXXX")"
root_info_file="${root_info_dir}/root_info.bin"
trap 'rm -rf "$root_info_dir" "$sync_dir"' EXIT

pids=()
cleanup() {
    echo -e "\n[Terminating] Caught Ctrl+C, killing background processes..."
    if [ ${#pids[@]} -ne 0 ]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    exit 1
}
trap cleanup SIGINT SIGTERM

for ((rank = 0; rank < nranks; ++rank)); do
    "$binary" "$rank" "$nranks" "$root_info_file" "$sync_dir" &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "RESULT | Status=PASS"
else
    echo "RESULT | Status=FAIL"
fi
exit "$status"
