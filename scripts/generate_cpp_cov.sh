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

set -e

if [[ $# -ne 4 ]]; then
    echo "Usage: $0 BUILD_DIR COV_FILE OUT_DIR CANN_PATH"
    exit 1
fi

BUILD_DIR="$1"
COV_FILE="$2"
OUT_DIR="$3"
CANN_PATH="$4"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v lcov >/dev/null 2>&1; then
    echo "lcov is required to generate coverage data, please install it first."
    exit 1
fi

if ! command -v genhtml >/dev/null 2>&1; then
    echo "genhtml is required to generate coverage html report, please install lcov package first."
    exit 1
fi

mkdir -p "$(dirname "${COV_FILE}")" "${OUT_DIR}"

LCOV_MAJOR=$(lcov --version 2>/dev/null | grep -oE '[0-9]+' | head -n 1)
EXTRA_ARGS=""
REMOVE_ARGS=""
if [[ -n "${LCOV_MAJOR}" && "${LCOV_MAJOR}" -ge 2 ]]; then
    EXTRA_ARGS="--ignore-errors mismatch --ignore-errors source"
    REMOVE_ARGS="--ignore-errors unused"
fi

lcov -c -d "${BUILD_DIR}" -o "${COV_FILE}" ${EXTRA_ARGS}
# Project headers are staged under this build directory and must remain in the report.
lcov -r "${COV_FILE}" \
    "${CANN_PATH}/*" \
    "/home/jenkins/opensource/*" \
    "${BUILD_DIR}/ut-hcomm/_deps/*" \
    "${ROOT_DIR}/build_out/*" \
    "${ROOT_DIR}/output/*" \
    "${ROOT_DIR}/tests/*" \
    "/usr/include/*" \
    "/usr/local/include/*" \
    -o "${COV_FILE}" ${REMOVE_ARGS}
genhtml "${COV_FILE}" -o "${OUT_DIR}"

echo "coverage report generated at ${OUT_DIR}/index.html"
