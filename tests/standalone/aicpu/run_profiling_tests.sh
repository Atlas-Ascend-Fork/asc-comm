#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
set -euo pipefail
# Host protocol checks only; does not substitute for AICPU / profiler validation.
root=$(cd "$(dirname "$0")/../../.." && pwd)
: "${RUNTIME_SOURCE:?Set RUNTIME_SOURCE to runtime source containing public profiling headers}"
: "${SECUREC_INCLUDE:?Set SECUREC_INCLUDE to the directory containing securec.h}"
build=$(mktemp -d "${TMPDIR:-/tmp}/asccomm-prof-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT
includes=(-I"$root/src/aicpu/asccomm/diagnostics" -I"$root/include/aicpu_ts/asccomm" -I"$root/pkg_inc/hcomm"
 -I"$RUNTIME_SOURCE/pkg_inc/profiling" -I"$RUNTIME_SOURCE/pkg_inc/aicpu_sched"
 -I"$RUNTIME_SOURCE/include/dfx/base" -I"$RUNTIME_SOURCE/pkg_inc/aicpu"
 -I"$RUNTIME_SOURCE/pkg_inc/base" -I"$RUNTIME_SOURCE/include/experiment/slog" -I"$SECUREC_INCLUDE")
libs=(-pthread)
if [[ $(uname -s) == Linux ]]; then libs=(-ldl -pthread); fi
for name in profiling_control profiling_session; do
 "${CXX:-c++}" -std=c++14 -Wall -Wextra -Werror "${includes[@]}" \
   "$root/tests/standalone/aicpu/${name}_test.cc" "${libs[@]}" -o "$build/$name"
 "$build/$name"
 printf '%s: PASS\n' "$name"
done
