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

devices=2
if [[ $# -gt 0 ]]; then
    [[ $# -eq 2 && "$1" == "--devices" ]] || { echo "Usage: $0 [--devices 2|8]" >&2; exit 1; }
    devices="$2"
fi
[[ "$devices" == "2" || "$devices" == "8" ]] || { echo "--devices must be 2 or 8" >&2; exit 1; }

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_batch"
[[ -x "$binary" ]] || { echo "build first: bash ${script_dir}/build.sh" >&2; exit 1; }

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_batch.XXXXXX")"
root_info_file="${work_dir}/root_info.bin"
pids=()
cleanup() {
    if [[ ${#pids[@]} -ne 0 ]]; then kill "${pids[@]}" 2>/dev/null || true; fi
    rm -rf -- "$work_dir"
}
trap cleanup EXIT INT TERM
export SIMT_BATCH_SYNC_DIR="$work_dir"

echo "TEST_INFO | Suite=simt_batch | Devices=${devices} | Pairs=$((devices / 2))"
for ((rank = 0; rank < devices; ++rank)); do
    timeout --signal=TERM "${SIMT_BATCH_TIMEOUT_SECONDS:-60}s" stdbuf -oL -eL \
        "$binary" "$rank" "$devices" "$root_info_file" \
        | sed -u "s/^/[process rank ${rank}] /" &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then status=1; fi
done
pids=()
echo "SUITE | Name=simt_batch | Status=$([[ "$status" -eq 0 ]] && echo PASS || echo FAIL)"
exit "$status"
