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
# Build one current SIMT batch specialization. Historical modes live in diagnostics/.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
batch=256
clean=false
optimization=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --batch-size|--matrix-batch) batch="${2:?batch size required}"; shift 2 ;;
        --backend) [[ "${2:-}" == simt ]] || { echo "Historical backends: diagnostics/build.sh" >&2; exit 1; }; shift 2 ;;
        --clean) clean=true; shift ;;
        -O0) optimization=-O0; shift ;;
        -h|--help) echo "Usage: build.sh [--batch-size 1|8|32|64|128|256] [--clean] [-O0]"; exit 0 ;;
        *) echo "Unknown option: $1; historical experiments: diagnostics/build.sh" >&2; exit 1 ;;
    esac
done
[[ "$batch" =~ ^(1|8|32|64|128|256)$ ]] || { echo "Invalid batch size: $batch" >&2; exit 1; }
: "${ASCEND_HOME_PATH:?Source the CANN set_env.sh first}"
profile="simt-matrix-bs${batch}"
# Reset cached flags when returning to the toolchain default.
cmake_options=(-U CMAKE_ASC_FLAGS)
[[ -z "$optimization" ]] || cmake_options=(-DCMAKE_ASC_FLAGS="$optimization")
build_dir="${script_dir}/build/${profile}"
[[ "$clean" == false ]] || rm -rf -- "$build_dir"
mkdir -p "$build_dir"
rm -f -- "${build_dir}/multi_jetty_batch_perf"
started_at=$SECONDS
echo "BUILD_CONFIG | Profile=${profile} | Directory=${build_dir}"
cmake -S "$script_dir" -B "$build_dir" -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DMULTI_JETTY_MATRIX_BATCH="$batch" "${cmake_options[@]}"
timeout --signal=TERM --kill-after=10s 5m cmake --build "$build_dir" --verbose --parallel "${BUILD_JOBS:-2}" 2>&1 | tee "${build_dir}/scope-build.log"

echo "BUILD_COMPLETE | Profile=${profile} | Seconds=$((SECONDS - started_at))"
echo "Run: bash run.sh --batch-size ${batch}"
