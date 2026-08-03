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

# build.sh must propagate packaging failures to CI and other callers.
expect_failure env ASCEND_HOME_PATH="${TEST_ROOT}/missing-cann" \
    bash "${PROJECT_ROOT}/build.sh" --pkg

"${RUN_FILE}" --noexec --extract="${EXTRACT_DIR}" >/dev/null
REQUIRED_VERSION=$(awk -F= '$1 == "RequiredAscDevkitVersion" {print $2; exit}' "${EXTRACT_DIR}/version.info")
[[ -n "${REQUIRED_VERSION}" ]] || { echo "Failed to read package version metadata" >&2; exit 1; }
PACKAGE_GIT_DIRTY=$(awk -F= '$1 == "GitDirty" {print $2; exit}' "${EXTRACT_DIR}/version.info")
EXPECTED_GIT_DIRTY=true
if [[ -z "$(git -C "${PROJECT_ROOT}" status --porcelain --untracked-files=all -- \
    build.sh include/aicore/hcomm src/aicore/hcomm scripts/package)" ]]; then
    EXPECTED_GIT_DIRTY=false
fi
[[ "${PACKAGE_GIT_DIRTY}" == "${EXPECTED_GIT_DIRTY}" ]]

# Reject module-directory links that would redirect package writes outside CANN_ROOT.
EXTERNAL_LINK_CANN="${TEST_ROOT}/external-link-cann"
EXTERNAL_HEADER_DIR="${TEST_ROOT}/outside-cann/hcomm"
mkdir -p \
    "${EXTERNAL_LINK_CANN}/asc/include/adv_api" \
    "${EXTERNAL_LINK_CANN}/asc/impl/adv_api/detail" \
    "${EXTERNAL_LINK_CANN}/share/info/asc-devkit" \
    "${EXTERNAL_HEADER_DIR}"
ln -s "${EXTERNAL_HEADER_DIR}" "${EXTERNAL_LINK_CANN}/asc/include/adv_api/hcomm"
printf 'Version=%s\n' "${REQUIRED_VERSION}" > \
    "${EXTERNAL_LINK_CANN}/share/info/asc-devkit/version.info"
expect_failure "${RUN_FILE}" --full --install-path="${EXTERNAL_LINK_CANN}"
grep -q 'target path resolves outside CANN root' "${ERROR_LOG}"
[[ ! -e "${EXTERNAL_HEADER_DIR}/hcomm.h" ]]
[[ ! -e "${EXTERNAL_LINK_CANN}/var/asc-comm-dev-patch" ]]

# Reject an invalid pre-existing backup target without deleting it during rollback.
INVALID_STATE_CANN="${TEST_ROOT}/invalid-state-cann"
INVALID_STATE_PATH="${INVALID_STATE_CANN}/var/asc-comm-dev-patch"
mkdir -p \
    "${INVALID_STATE_CANN}/asc/include/adv_api" \
    "${INVALID_STATE_CANN}/asc/impl/adv_api/detail" \
    "${INVALID_STATE_CANN}/share/info/asc-devkit" \
    "${INVALID_STATE_CANN}/var"
printf 'Version=%s\n' "${REQUIRED_VERSION}" > \
    "${INVALID_STATE_CANN}/share/info/asc-devkit/version.info"
printf 'pre-existing marker\n' > "${INVALID_STATE_PATH}"
expect_failure "${RUN_FILE}" --full --install-path="${INVALID_STATE_CANN}"
grep -q 'backup state path is invalid' "${ERROR_LOG}"
grep -qx 'pre-existing marker' "${INVALID_STATE_PATH}"
[[ ! -e "${INVALID_STATE_CANN}/asc/include/adv_api/hcomm/hcomm.h" ]]

# Preserve support for links whose resolved destination remains inside CANN_ROOT.
INTERNAL_LINK_CANN="${TEST_ROOT}/internal-link-cann"
INTERNAL_HEADER_DIR="${INTERNAL_LINK_CANN}/shared/hcomm"
mkdir -p \
    "${INTERNAL_LINK_CANN}/asc/include/adv_api" \
    "${INTERNAL_LINK_CANN}/asc/impl/adv_api/detail" \
    "${INTERNAL_LINK_CANN}/share/info/asc-devkit" \
    "${INTERNAL_HEADER_DIR}"
ln -s "${INTERNAL_HEADER_DIR}" "${INTERNAL_LINK_CANN}/asc/include/adv_api/hcomm"
printf 'Version=%s\n' "${REQUIRED_VERSION}" > \
    "${INTERNAL_LINK_CANN}/share/info/asc-devkit/version.info"
"${RUN_FILE}" --full --install-path="${INTERNAL_LINK_CANN}"
cmp "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/hcomm.h" \
    "${INTERNAL_HEADER_DIR}/hcomm.h"
"${RUN_FILE}" --uninstall --install-path="${INTERNAL_LINK_CANN}"
[[ ! -e "${INTERNAL_HEADER_DIR}/hcomm.h" ]]
[[ -L "${INTERNAL_LINK_CANN}/asc/include/adv_api/hcomm" ]]

HCOMM_PUBLIC_HEADER_COUNT=$(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' | wc -l)
HCOMM_IMPL_HEADER_COUNT=$(find "${PROJECT_ROOT}/src/aicore/hcomm" -type f -name '*.h' | wc -l)
EXPECTED_FILE_COUNT=$((HCOMM_PUBLIC_HEADER_COUNT + HCOMM_IMPL_HEADER_COUNT))
[[ "$(wc -l < "${EXTRACT_DIR}/manifest.sha256")" -eq "${EXPECTED_FILE_COUNT}" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/include/adv_api/ain" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/impl/adv_api/hcomm" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/impl/adv_api/detail/ain" ]]

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/include/aicore/hcomm/"}
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/${relative_path}"
done < <(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' -print0 | sort -z)

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/src/aicore/hcomm/"}
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/impl/adv_api/detail/hcomm/${relative_path}"
done < <(find "${PROJECT_ROOT}/src/aicore/hcomm" -type f -name '*.h' -print0 | sort -z)

RELATIVE_DETAIL_INCLUDE_COUNT=0
while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/include/aicore/hcomm/"}
    packaged_header="${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/${relative_path}"
    while IFS= read -r include_path; do
        case "${include_path}" in
            ../../../impl/adv_api/detail/*)
                [[ -f "$(dirname -- "${packaged_header}")/${include_path}" ]]
                RELATIVE_DETAIL_INCLUDE_COUNT=$((RELATIVE_DETAIL_INCLUDE_COUNT + 1))
                ;;
        esac
    done < <(awk -F '"' '/^[[:space:]]*#[[:space:]]*include[[:space:]]*"/ {print $2}' "${packaged_header}")
done < <(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' -print0 | sort -z)
[[ ${RELATIVE_DETAIL_INCLUDE_COUNT} -gt 0 ]]

mkdir -p \
    "${FAKE_CANN}/asc/include/adv_api/hcomm" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged" \
    "${FAKE_CANN}/share/info/asc-devkit" \
    "${BASELINE_DIR}"
printf 'Version=%s\n' "${REQUIRED_VERSION}" > "${FAKE_CANN}/share/info/asc-devkit/version.info"

while read -r _ path; do
    mkdir -p "$(dirname -- "${FAKE_CANN}/${path}")"
    printf 'baseline: %s\n' "${path}" > "${FAKE_CANN}/${path}"
    chmod 540 "${FAKE_CANN}/${path}"
done < "${EXTRACT_DIR}/manifest.sha256"
chmod 751 "${FAKE_CANN}/asc/include/adv_api/hcomm"
chmod 711 "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm"
chmod 700 "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common"
chmod 701 "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged"
cp -a "${FAKE_CANN}/asc" "${BASELINE_DIR}/asc"

expect_failure "${RUN_FILE}" --install --install-path="${FAKE_CANN}"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

"${RUN_FILE}" --full --install-path="${FAKE_CANN}"

while read -r _ path; do
    cmp "${EXTRACT_DIR}/payload/${path}" "${FAKE_CANN}/${path}"
    [[ "$(stat -c %a -- "${FAKE_CANN}/${path}")" == "550" ]]
done < "${EXTRACT_DIR}/manifest.sha256"
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/adv_api/hcomm")" == "750" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm")" == "750" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common")" == "750" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged")" == "701" ]]
[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/active.manifest.sha256" ]]
grep -qx 'FormatVersion=2' "${FAKE_CANN}/var/asc-comm-dev-patch/state.info"
[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/baseline/directories.tsv" ]]

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

while read -r _ path; do
    cmp "${BASELINE_DIR}/${path}" "${FAKE_CANN}/${path}"
    [[ "$(stat -c %a -- "${FAKE_CANN}/${path}")" == "540" ]]
done < "${EXTRACT_DIR}/manifest.sha256"
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/adv_api/hcomm")" == "751" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm")" == "711" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common")" == "700" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged")" == "701" ]]
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

# A compatibility failure must not touch the CANN tree.
printf 'Version=0.0.0\n' > "${FAKE_CANN}/share/info/asc-devkit/version.info"
expect_failure "${RUN_FILE}" --full --install-path="${FAKE_CANN}"
cmp "${BASELINE_DIR}/asc/include/adv_api/hcomm/hcomm.h" \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

echo "asc-comm run package install/reinstall/uninstall test passed"
