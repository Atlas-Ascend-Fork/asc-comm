#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

set -o pipefail

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <test-command> [args...]" >&2
    exit 2
fi

output_file=$(mktemp)
trap 'rm -f "${output_file}"' EXIT

"$@" 2>&1 | tee "${output_file}"
test_status=${PIPESTATUS[0]}

if [[ ${test_status} -ne 0 ]]; then
    exit "${test_status}"
fi

# Keep the output check as a fallback for runners that incorrectly return 0
# after printing a GTest failure summary.
if grep -Eq '\[[[:space:]]*FAILED[[:space:]]*\]|FAILED TESTS' "${output_file}"; then
    exit 1
fi

exit 0
