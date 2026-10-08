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

if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "ASCEND_HOME_PATH is not set. Run: source /usr/local/Ascend/cann/set_env.sh" >&2
    exit 1
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build"
large_group=OFF
clean=0
for option in "$@"; do
    case "$option" in
        --quick) large_group=OFF ;;
        --full) large_group=ON ;;
        --clean) clean=1 ;;
        *) echo "Usage: build.sh [--quick|--full] [--clean]" >&2; exit 1 ;;
    esac
done
if [[ "$clean" == "1" ]]; then
    rm -rf -- "$build_dir"
fi
mkdir -p "$build_dir"
compile_log="${build_dir}/build-output.log"
if cmake -S "$script_dir" -B "$build_dir" -DCMAKE_ASC_ARCHITECTURES=dav-3510 \
    -DSIMT_BATCH_ENABLE_LARGE_GROUP="$large_group" >"$compile_log" 2>&1 &&
    cmake --build "$build_dir" --verbose --parallel "${BUILD_JOBS:-$(nproc)}" >>"$compile_log" 2>&1; then
    :
else
    code=$?
    tail -n 40 "$compile_log" >&2
    echo "BUILD_ERROR | Exit=${code} | Log=${compile_log}" >&2
    exit "$code"
fi
printf '%s\n' "$large_group" >"${build_dir}/large_group_enabled"
