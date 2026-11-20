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
tool_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v cmake >/dev/null 2>&1; then
    echo 'CMake >=3.16 is required.' >&2
    exit 1
fi
cmake -S "$tool_dir" -B "$tool_dir/build" "$@"
cmake --build "$tool_dir/build" --parallel
if grep -Eiq '^BUILD_TESTING:BOOL=(ON|TRUE|YES|Y|1)$' "$tool_dir/build/CMakeCache.txt"; then
    cmake -E chdir "$tool_dir/build" ctest --output-on-failure
fi
