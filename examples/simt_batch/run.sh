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
    echo "Usage: $0 [basic|full|single-last|matrix]"
}

if [[ $# -gt 1 ]]; then
    usage
    exit 1
fi

mode="${1:-full}"
case "$mode" in
    basic | full | single-last | matrix | matrix-1x1 | matrix-2x1 | matrix-8x1 | matrix-2x2 | matrix-8x2) ;;
    *)
        usage
        exit 1
        ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_batch"
[[ -x "$binary" ]] || { echo "build the example first: bash ${script_dir}/build.sh" >&2; exit 1; }
if [[ "$mode" == "matrix" ]]; then
    matrix_status=0
    for matrix_case in matrix-1x1 matrix-2x1 matrix-8x1 matrix-2x2 matrix-8x2; do
        bash "${script_dir}/run.sh" "$matrix_case" || matrix_status=1
    done
    exit "$matrix_status"
fi
code_version="$(git -C "${script_dir}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [[ "$mode" == "basic" ]]; then
    suite_name="simt_batch_basic"
    echo "TEST_INFO | Suite=${suite_name} | CodeVersion=${code_version} | BatchSize=2 | BatchCount=1 | PayloadBytes=64"
elif [[ "$mode" == "single-last" ]]; then
    suite_name="simt_single_last_control"
    echo "TEST_INFO | Suite=${suite_name} | CodeVersion=${code_version} | DeferredWrites=1 | ImmediateWrites=1 | PayloadBytes=64"
elif [[ "$mode" == matrix-* ]]; then
    matrix_shape="${mode#matrix-}"
    suite_name="simt_batch_${mode}"
    echo "TEST_INFO | Suite=${suite_name} | CodeVersion=${code_version} | BatchSize=${matrix_shape%x*} | BatchCount=${matrix_shape#*x} | PayloadBytes=64"
else
    suite_name="simt_batch"
    echo "TEST_INFO | Suite=${suite_name} | CodeVersion=${code_version} | BatchSize=8 | BatchCount=2 | PayloadBytes=64"
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/simt_batch.XXXXXX")"
root_info_file="${work_dir}/root_info.bin"
pids=()
cleanup() {
    if [[ ${#pids[@]} -ne 0 ]]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    rm -rf -- "$work_dir"
}
trap cleanup EXIT INT TERM
export SIMT_BATCH_SYNC_DIR="$work_dir"

timeout_seconds="${SIMT_BATCH_TIMEOUT_SECONDS:-60}"
for rank in 0 1; do
    timeout --signal=TERM "${timeout_seconds}s" stdbuf -oL -eL \
        "$binary" "$rank" 2 "$root_info_file" "$mode" 2>&1 \
        | sed -u "s/^/[process rank ${rank}] /" \
        | tee "${work_dir}/rank_${rank}.log" &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        status=1
    fi
done
pids=()

echo "SUITE | Name=${suite_name} | CodeVersion=${code_version} | Status=$([[ "$status" -eq 0 ]] && echo PASS || echo FAIL)"
exit "$status"
