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
# Build one current SIMT batch specialization.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
batch=256
clean=false
optimization=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --batch-size) batch="${2:?batch size required}"; shift 2 ;;
        --clean) clean=true; shift ;;
        -O0) optimization=-O0; shift ;;
        -h|--help) echo "Usage: build.sh [--batch-size 1|8|32|64|128|256] [--clean] [-O0]"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
[[ "$batch" =~ ^(1|8|32|64|128|256)$ ]] || { echo "Invalid batch size: $batch" >&2; exit 1; }
: "${ASCEND_HOME_PATH:?Source the CANN set_env.sh first}"
profile="simt-matrix-bs${batch}"
build_dir="${script_dir}/build/${profile}"
[[ "$clean" == false ]] || rm -rf -- "$build_dir"
mkdir -p "$build_dir"
rm -f -- "${build_dir}/build-ready.sha256"
started_at=$SECONDS
# Keep successful compiler output in the build directory; show the useful tail on failure.
compile_log="${build_dir}/build-output.log"
cmake_options=(-U CMAKE_ASC_FLAGS)
[[ -z "$optimization" ]] || cmake_options=(-DCMAKE_ASC_FLAGS="$optimization")
if cmake -S "$script_dir" -B "$build_dir" -DCMAKE_ASC_ARCHITECTURES=dav-3510 \
    -DMULTI_JETTY_MATRIX_BATCH="$batch" "${cmake_options[@]}" >"$compile_log" 2>&1 &&
    timeout --signal=TERM --kill-after=10s 5m cmake --build "$build_dir" --verbose \
        --parallel "${BUILD_JOBS:-2}" >>"$compile_log" 2>&1; then
    :
else
    code=$?
    tail -n 40 "$compile_log" >&2
    echo "BUILD_ERROR | Exit=${code} | Log=${compile_log}" >&2
    exit "$code"
fi

# Verify the selected VF. Register usage is recorded, not limited across different geometries.
object="${build_dir}/CMakeFiles/multi_jetty_batch_perf.dir/simt_kernel.cpp.o"
mapfile -t reports < <(grep '^\[BISHENG\] SIMT VF Function properties for ' "${build_dir}/build-output.log" || true)
if [[ "$optimization" == -O0 ]]; then
    : # O0 may not emit a VF resource report.
elif [[ ${#reports[@]} == 0 ]]; then
    # Reuse an incremental resource report only for the identical object.
    [[ -f "${build_dir}/build-resource.txt" && -f "${build_dir}/build-resource-object.sha256" ]] &&
        sha256sum --check --status "${build_dir}/build-resource-object.sha256" || {
        echo "BUILD_ERROR | missing resource report for current object" >&2; exit 1;
    }
else
    capacity=$((batch * 8))
    ((capacity >= 32)) || capacity=32
    ((capacity <= 1024)) || capacity=1024
    pattern='Stack size: ([0-9]+) bytes, Used register number: ([0-9]+)'
    [[ ${#reports[@]} == 1 && "${reports[0]}" == *"SimtParallelMultiJettyVfILj${batch}ELj${capacity}E"* &&
       "${reports[0]}" =~ $pattern ]] || {
        echo "BUILD_ERROR | unexpected VF resource report" >&2; exit 1;
    }
    stack="${BASH_REMATCH[1]}"
    registers="${BASH_REMATCH[2]}"
    [[ "$stack" -le 1152 ]] || { echo "BUILD_ERROR | StackBytes=${stack} exceeds 1152" >&2; exit 1; }
    echo "BUILD_RESOURCE | VF=${batch},${capacity} | StackBytes=${stack} | Registers=${registers} | RegisterPolicy=record" > "${build_dir}/build-resource.txt"
    sha256sum "$object" > "${build_dir}/build-resource-object.sha256"
fi
binary="${build_dir}/multi_jetty_batch_perf"
expected="BUILD_CONFIG | Backend=simt | Layout=matrix-v4 | CompletionVersion=1 | MatrixBatch=${batch}"
[[ "$("$binary" --build-info)" == "$expected" ]] || { echo "BUILD_ERROR | stale binary configuration" >&2; exit 1; }
sha256sum "$binary" > "${build_dir}/build-ready.sha256"
echo "BUILD_COMPLETE | Profile=${profile} | Seconds=$((SECONDS - started_at))" > "${build_dir}/build-complete.txt"
