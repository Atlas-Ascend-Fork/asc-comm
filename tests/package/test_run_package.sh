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

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "${SCRIPT_DIR}/../.." && pwd)
RUN_FILE=${1:-}

[[ -n "${RUN_FILE}" ]] || { echo "Usage: $0 <asc-comm.run>" >&2; exit 1; }
RUN_FILE=$(readlink -f -- "${RUN_FILE}")
[[ -x "${RUN_FILE}" ]] || { echo "Run package is missing or not executable: ${RUN_FILE}" >&2; exit 1; }

TEST_ROOT=$(mktemp -d)
trap 'rm -rf -- "${TEST_ROOT}"' EXIT
EXTRACT_DIR="${TEST_ROOT}/extracted"
FAKE_CANN="${TEST_ROOT}/cann"
BASELINE_DIR="${TEST_ROOT}/baseline"
ERROR_LOG="${TEST_ROOT}/expected-error.log"

expect_failure()
{
    if "$@" > "${ERROR_LOG}" 2>&1; then
        echo "Command unexpectedly succeeded: $*" >&2
        exit 1
    fi
}

"${RUN_FILE}" --noexec --extract="${EXTRACT_DIR}" >/dev/null
REQUIRED_VERSION=$(awk -F= '$1 == "RequiredAscDevkitVersion" {print $2; exit}' "${EXTRACT_DIR}/version.info")
[[ -n "${REQUIRED_VERSION}" ]] || { echo "Failed to read package version metadata" >&2; exit 1; }
PUBLIC_HEADER_COUNT=$(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' | wc -l)
DETAIL_HEADER_COUNT=$(find "${PROJECT_ROOT}/src/aicore/hcomm/detail" -type f -name '*.h' | wc -l)
EXPECTED_FILE_COUNT=$((PUBLIC_HEADER_COUNT + DETAIL_HEADER_COUNT * 2))
[[ "$(wc -l < "${EXTRACT_DIR}/manifest.sha256")" -eq "${EXPECTED_FILE_COUNT}" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/impl/adv_api/hcomm" ]]

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/include/aicore/hcomm/"}
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/${relative_path}"
done < <(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' -print0 | sort -z)

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/src/aicore/hcomm/detail/"}
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/detail/${relative_path}"
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/impl/adv_api/detail/hcomm/${relative_path}"
done < <(find "${PROJECT_ROOT}/src/aicore/hcomm/detail" -type f -name '*.h' -print0 | sort -z)

HCOMM_DETAIL_INCLUDE_COUNT=0
while IFS= read -r include_path; do
    case "${include_path}" in
        hcomm/detail/*.h)
            [[ -f "${EXTRACT_DIR}/payload/asc/include/adv_api/${include_path}" ]]
            HCOMM_DETAIL_INCLUDE_COUNT=$((HCOMM_DETAIL_INCLUDE_COUNT + 1))
            ;;
    esac
done < <(awk -F '"' '/^[[:space:]]*#[[:space:]]*include[[:space:]]*"/ {print $2}' \
    "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/hcomm.h")
[[ ${HCOMM_DETAIL_INCLUDE_COUNT} -gt 0 ]]

mkdir -p \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/detail/common" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common" \
    "${FAKE_CANN}/share/info/asc-devkit" \
    "${BASELINE_DIR}"
printf 'Version=%s\n' "${REQUIRED_VERSION}" > "${FAKE_CANN}/share/info/asc-devkit/version.info"

printf 'public baseline\n' > "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
printf 'public detail baseline\n' > "${FAKE_CANN}/asc/include/adv_api/hcomm/detail/common/hcomm_base.h"
printf 'impl detail baseline\n' > "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common/hcomm_base.h"
chmod 540 \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h" \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/detail/common/hcomm_base.h" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common/hcomm_base.h"
cp -a "${FAKE_CANN}/asc" "${BASELINE_DIR}/asc"

expect_failure "${RUN_FILE}" --install --install-path="${FAKE_CANN}"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

"${RUN_FILE}" --full --install-path="${FAKE_CANN}"

while read -r _ path; do
    cmp "${EXTRACT_DIR}/payload/${path}" "${FAKE_CANN}/${path}"
    [[ "$(stat -c %a -- "${FAKE_CANN}/${path}")" == "550" ]]
done < "${EXTRACT_DIR}/manifest.sha256"
[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/active.manifest.sha256" ]]

# Protect local changes made directly in an already patched CANN tree.
chmod 750 "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
printf 'external change\n' > "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
expect_failure "${RUN_FILE}" --uninstall --install-path="${FAKE_CANN}"
grep -q '^external change$' "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
[[ -d "${FAKE_CANN}/var/asc-comm-dev-patch" ]]
expect_failure "${RUN_FILE}" --full --install-path="${FAKE_CANN}"
grep -q '^external change$' "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"

"${RUN_FILE}" --full --install-path="${FAKE_CANN}" --force

# Reinstalling must retain the original baseline rather than backing up patched files.
"${RUN_FILE}" --full --install-path="${FAKE_CANN}"
"${RUN_FILE}" --uninstall --install-path="${FAKE_CANN}"

cmp "${BASELINE_DIR}/asc/include/adv_api/hcomm/hcomm.h" \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
cmp "${BASELINE_DIR}/asc/include/adv_api/hcomm/detail/common/hcomm_base.h" \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/detail/common/hcomm_base.h"
cmp "${BASELINE_DIR}/asc/impl/adv_api/detail/hcomm/common/hcomm_base.h" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common/hcomm_base.h"
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h")" == "540" ]]

while read -r _ path; do
    case "${path}" in
        asc/include/adv_api/hcomm/hcomm.h|\
        asc/include/adv_api/hcomm/detail/common/hcomm_base.h|\
        asc/impl/adv_api/detail/hcomm/common/hcomm_base.h)
            ;;
        *)
            [[ ! -e "${FAKE_CANN}/${path}" ]]
            ;;
    esac
done < "${EXTRACT_DIR}/manifest.sha256"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

# A compatibility failure must not touch the CANN tree.
printf 'Version=0.0.0\n' > "${FAKE_CANN}/share/info/asc-devkit/version.info"
expect_failure "${RUN_FILE}" --full --install-path="${FAKE_CANN}"
cmp "${BASELINE_DIR}/asc/include/adv_api/hcomm/hcomm.h" \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

echo "asc-comm run package install/reinstall/uninstall test passed"
