#!/bin/bash
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

script_path=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=../scripts/benchmark_environment.sh
source "${script_path}/../scripts/benchmark_environment.sh"
build_dir="${MC2_BENCH_BUILD_DIR:-${script_path}/build_out}"
build_dir="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "${build_dir}")"
package_target="${1:-package}"
cann_path="$(hccl_test_cann_root /usr/local/Ascend/cann)"
cmake_command="${MC2_BENCH_CMAKE:-cmake}"

# An out-of-tree build is used by the launcher. Never clear a source directory
# (or any of its parents) when a malformed override is supplied.
if [[ "${build_dir}" != /* || "${build_dir}" == / ||
      "${build_dir}" == "${script_path}" || "${script_path}" == "${build_dir}"/* ||
      ( "${build_dir}" == "${script_path}"/* && "${build_dir}" != "${script_path}/build_out" ) ]]; then
    echo "[ERROR] build directory must be an absolute, disposable build directory" >&2
    exit 1
fi

if [ ! -d "${cann_path}" ]; then
    echo "[ERROR] CANN package path does not exist: ${cann_path}" >&2
    echo "        Source the CANN environment, or set ASCEND_HOME_PATH / ASCEND_CANN_PACKAGE_PATH before running build.sh." >&2
    exit 1
fi

if [[ -n "${MC2_BENCH_BUILD_JOBS:-}" ]]; then
    jobs="${MC2_BENCH_BUILD_JOBS}"
elif command -v nproc >/dev/null 2>&1; then
    jobs=$(nproc)
else
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)
fi

if [[ -f "${cann_path}/set_env.sh" ]]; then
    cann_environment="${cann_path}/set_env.sh"
elif [[ -f "$(dirname "${cann_path}")/set_env.sh" ]]; then
    cann_environment="$(dirname "${cann_path}")/set_env.sh"
else
    echo "[ERROR] CANN set_env.sh not found under selected SDK root or its parent" >&2
    exit 1
fi
# SDK setup scripts may inspect unset variables. Source the selected SDK in
# this build shell; a previous deployment probe cannot transfer its environment.
readonly mc2_bench_saved_source="${script_path}"
readonly mc2_bench_saved_build="${build_dir}"
readonly mc2_bench_saved_cann="${cann_path}"
readonly mc2_bench_saved_cmake="${cmake_command}"
readonly mc2_bench_saved_target="${package_target}"
readonly mc2_bench_saved_jobs="${jobs}"
set +eu
set --
source "${cann_environment}" </dev/null
cann_environment_status=$?
set -euo pipefail
if [[ "${cann_environment_status}" -ne 0 ]]; then
    echo "[ERROR] Failed to source selected CANN environment" >&2
    exit "${cann_environment_status}"
fi
# Environment scripts can use generic shell variables such as script_path.
script_path="${mc2_bench_saved_source}"
build_dir="${mc2_bench_saved_build}"
cann_path="${mc2_bench_saved_cann}"
cmake_command="${mc2_bench_saved_cmake}"
package_target="${mc2_bench_saved_target}"
jobs="${mc2_bench_saved_jobs}"
export ASCEND_HOME_PATH="${cann_path}"
export ASCEND_CANN_PACKAGE_PATH="${cann_path}"
hccl_test_use_cann_environment "${cann_path}"
cmake_args=(-S "${script_path}" -B "${build_dir}" --preset=default
    "-DASCEND_CANN_PACKAGE_PATH=${cann_path}"
    "-DCMAKE_INSTALL_PREFIX=${build_dir}"
    "-DMC2_BENCH_COMM_ONLY=${MC2_BENCH_COMM_ONLY:-OFF}")
if [[ -n "${MC2_BENCH_MAKESELF:-}" ]]; then
    cmake_args+=("-DMC2_BENCH_MAKESELF=${MC2_BENCH_MAKESELF}")
fi
if [[ -n "${MC2_BENCH_ASCENDC_UTIL_DIR:-}" ]]; then
    cmake_args+=("-DMC2_BENCH_ASCENDC_UTIL_DIR=${MC2_BENCH_ASCENDC_UTIL_DIR}")
fi
"${cmake_command}" -E remove_directory "${build_dir}"
"${cmake_command}" "${cmake_args[@]}"
"${cmake_command}" --build "${build_dir}" --target binary -j"${jobs}"
"${cmake_command}" --build "${build_dir}" --target "${package_target}" -j"${jobs}"

echo "[INFO] custom operator build finished: ${build_dir}"
