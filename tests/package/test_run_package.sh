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
chmod 755 "${TEST_ROOT}"
EXTRACT_DIR="${TEST_ROOT}/extracted"
FAKE_CANN="${TEST_ROOT}/cann"
BASELINE_DIR="${TEST_ROOT}/baseline"
ERROR_LOG="${TEST_ROOT}/expected-error.log"
TEST_HOME="${TEST_ROOT}/home"
LOG_DIR="${TEST_HOME}/var/log/ascend_seclog"

if [[ $(id -u) -eq 0 ]]; then
    EXPECTED_FILE_MODE=555
    EXPECTED_HCOMM_DIR_MODE=755
    EXPECTED_COMM_API_DIR_MODE=555
else
    EXPECTED_FILE_MODE=550
    EXPECTED_HCOMM_DIR_MODE=750
    EXPECTED_COMM_API_DIR_MODE=550
fi

run_package()
{
    env HOME="${TEST_HOME}" "${RUN_FILE}" "$@"
}

expect_failure()
{
    if "$@" > "${ERROR_LOG}" 2>&1; then
        echo "Command unexpectedly succeeded: $*" >&2
        exit 1
    fi
}

read_required_version()
{
    local package=$1
    awk -F= -v key="required_package_${package}_version" \
        '$1 == key {gsub(/"/, "", $2); print $2; exit}' "${EXTRACT_DIR}/version.info"
}

write_dependency_versions()
{
    local cann_root=$1

    mkdir -p \
        "${cann_root}/share/info/asc-devkit" \
        "${cann_root}/share/info/hcomm" \
        "${cann_root}/share/info/runtime"
    printf 'Version=%s\n' "${REQUIRED_ASC_DEVKIT_VERSION}" > \
        "${cann_root}/share/info/asc-devkit/version.info"
    printf 'Version=%s\n' "${REQUIRED_HCOMM_VERSION}" > \
        "${cann_root}/share/info/hcomm/version.info"
    printf 'Version=%s\n' "${REQUIRED_RUNTIME_VERSION}" > \
        "${cann_root}/share/info/runtime/version.info"
}

remove_baseline_entry()
{
    local manifest=$1
    local path=$2

    chmod 640 "${manifest}"
    awk -F '\t' -v path="${path}" '$2 != path' "${manifest}" > "${manifest}.tmp"
    mv "${manifest}.tmp" "${manifest}"
    chmod 440 "${manifest}"
}

# build.sh must propagate packaging failures to CI and other callers.
expect_failure env ASCEND_HOME_PATH="${TEST_ROOT}/missing-cann" \
    bash "${PROJECT_ROOT}/build.sh" --pkg
expect_failure bash "${PROJECT_ROOT}/scripts/package/build_package.sh" \
    --cann-path="${TEST_ROOT}/missing-cann"
bash "${PROJECT_ROOT}/scripts/package/build_package.sh" --help > "${ERROR_LOG}"
grep -q -- '--cann_path=<PATH>' "${ERROR_LOG}"

"${RUN_FILE}" --noexec --extract="${EXTRACT_DIR}" >/dev/null
REQUIRED_ASC_DEVKIT_VERSION=$(read_required_version "asc-devkit")
REQUIRED_HCOMM_VERSION=$(read_required_version "hcomm")
REQUIRED_RUNTIME_VERSION=$(read_required_version "runtime")
[[ -n "${REQUIRED_ASC_DEVKIT_VERSION}" && \
    -n "${REQUIRED_HCOMM_VERSION}" && \
    -n "${REQUIRED_RUNTIME_VERSION}" ]] || \
    { echo "Failed to read package dependency metadata" >&2; exit 1; }
PACKAGE_GIT_DIRTY=$(awk -F= '$1 == "GitDirty" {print $2; exit}' "${EXTRACT_DIR}/version.info")
EXPECTED_GIT_DIRTY=true
if [[ -z "$(git -C "${PROJECT_ROOT}" status --porcelain --untracked-files=all -- \
    build.sh include src scripts/package version.cmake)" ]]; then
    EXPECTED_GIT_DIRTY=false
fi
[[ "${PACKAGE_GIT_DIRTY}" == "${EXPECTED_GIT_DIRTY}" ]]
EXPECTED_PACKAGE_VERSION=$(sed -nE \
    's/^[[:space:]]*set\(ASCCOMM_VERSION[[:space:]]+"([^"]+)"\)[[:space:]]*$/\1/p' \
    "${PROJECT_ROOT}/version.cmake")
[[ "$(awk -F= '$1 == "PackageVersion" {print $2; exit}' "${EXTRACT_DIR}/version.info")" == \
    "${EXPECTED_PACKAGE_VERSION}" ]]
[[ "$(awk -F= '$1 == "Version" {print $2; exit}' "${EXTRACT_DIR}/version.info")" == \
    "${EXPECTED_PACKAGE_VERSION}" ]]

# Reject module-directory links that would redirect package writes outside CANN_ROOT.
EXTERNAL_LINK_CANN="${TEST_ROOT}/external-link-cann"
EXTERNAL_HEADER_DIR="${TEST_ROOT}/outside-cann/hcomm"
mkdir -p \
    "${EXTERNAL_LINK_CANN}/asc/include/adv_api" \
    "${EXTERNAL_LINK_CANN}/asc/impl/adv_api/detail" \
    "${EXTERNAL_HEADER_DIR}"
ln -s "${EXTERNAL_HEADER_DIR}" "${EXTERNAL_LINK_CANN}/asc/include/adv_api/hcomm"
write_dependency_versions "${EXTERNAL_LINK_CANN}"
expect_failure run_package --full --install-path="${EXTERNAL_LINK_CANN}"
grep -q 'target path resolves outside CANN root' "${ERROR_LOG}"
[[ ! -e "${EXTERNAL_HEADER_DIR}/hcomm.h" ]]
[[ ! -e "${EXTERNAL_LINK_CANN}/var/asc-comm-dev-patch" ]]

# Reject an invalid pre-existing backup target without deleting it during rollback.
INVALID_STATE_CANN="${TEST_ROOT}/invalid-state-cann"
INVALID_STATE_PATH="${INVALID_STATE_CANN}/var/asc-comm-dev-patch"
mkdir -p \
    "${INVALID_STATE_CANN}/asc/include/adv_api" \
    "${INVALID_STATE_CANN}/asc/impl/adv_api/detail" \
    "${INVALID_STATE_CANN}/var"
write_dependency_versions "${INVALID_STATE_CANN}"
printf 'pre-existing marker\n' > "${INVALID_STATE_PATH}"
expect_failure run_package --full --install-path="${INVALID_STATE_CANN}"
grep -q 'backup state path is invalid' "${ERROR_LOG}"
grep -qx 'pre-existing marker' "${INVALID_STATE_PATH}"
[[ ! -e "${INVALID_STATE_CANN}/asc/include/adv_api/hcomm/hcomm.h" ]]

# Preserve support for links whose resolved destination remains inside CANN_ROOT.
INTERNAL_LINK_CANN="${TEST_ROOT}/internal-link-cann"
INTERNAL_HEADER_DIR="${INTERNAL_LINK_CANN}/shared/hcomm"
mkdir -p \
    "${INTERNAL_LINK_CANN}/asc/include/adv_api" \
    "${INTERNAL_LINK_CANN}/asc/impl/adv_api/detail" \
    "${INTERNAL_HEADER_DIR}"
ln -s "${INTERNAL_HEADER_DIR}" "${INTERNAL_LINK_CANN}/asc/include/adv_api/hcomm"
write_dependency_versions "${INTERNAL_LINK_CANN}"
run_package --full --install-path="${INTERNAL_LINK_CANN}"
cmp "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/hcomm.h" \
    "${INTERNAL_HEADER_DIR}/hcomm.h"
run_package --uninstall --install-path="${INTERNAL_LINK_CANN}"
[[ ! -e "${INTERNAL_HEADER_DIR}/hcomm.h" ]]
[[ -L "${INTERNAL_LINK_CANN}/asc/include/adv_api/hcomm" ]]

HCOMM_PUBLIC_HEADER_COUNT=$(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' | wc -l)
HCOMM_IMPL_HEADER_COUNT=$(find "${PROJECT_ROOT}/src/aicore/hcomm" -type f -name '*.h' | wc -l)
COMM_API_PUBLIC_HEADER_COUNT=$(find "${PROJECT_ROOT}/include" -type f -name '*.h' \
    ! -path "${PROJECT_ROOT}/include/aicore/hcomm/*" | wc -l)
COMM_API_IMPL_HEADER_COUNT=$(find "${PROJECT_ROOT}/src" -type f -name '*.h' \
    ! -path "${PROJECT_ROOT}/src/aicore/hcomm/*" | wc -l)
EXPECTED_FILE_COUNT=$((HCOMM_PUBLIC_HEADER_COUNT + HCOMM_IMPL_HEADER_COUNT + \
    COMM_API_PUBLIC_HEADER_COUNT + COMM_API_IMPL_HEADER_COUNT))
[[ "$(wc -l < "${EXTRACT_DIR}/manifest.sha256")" -eq "${EXPECTED_FILE_COUNT}" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/include/adv_api/ain" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/impl/adv_api/hcomm" ]]
[[ ! -e "${EXTRACT_DIR}/payload/asc/impl/adv_api/detail/ain" ]]
[[ -f "${EXTRACT_DIR}/payload/asc/include/comm_api/ccu/ccu_host_launch.h" ]]
[[ ! -e "${EXTRACT_DIR}/payload/aarch64-linux" ]]
[[ ! -e "${EXTRACT_DIR}/payload/x86_64-linux" ]]
diff -u <(printf '%s\t%s\n' \
    'asc/include/comm_api/aicore/hcomm' '../../adv_api/hcomm' \
    'asc/impl/comm_api/aicore/hcomm' '../../adv_api/detail/hcomm') \
    "${EXTRACT_DIR}/links.tsv"

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/include/aicore/hcomm/"}
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/include/adv_api/hcomm/${relative_path}"
done < <(find "${PROJECT_ROOT}/include/aicore/hcomm" -type f -name '*.h' -print0 | sort -z)

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/src/aicore/hcomm/"}
    cmp "${source_file}" "${EXTRACT_DIR}/payload/asc/impl/adv_api/detail/hcomm/${relative_path}"
done < <(find "${PROJECT_ROOT}/src/aicore/hcomm" -type f -name '*.h' -print0 | sort -z)

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/include/"}
    cmp "${source_file}" \
        "${EXTRACT_DIR}/payload/asc/include/comm_api/${relative_path}"
done < <(find "${PROJECT_ROOT}/include" -type f -name '*.h' \
    ! -path "${PROJECT_ROOT}/include/aicore/hcomm/*" -print0 | sort -z)

while IFS= read -r -d '' source_file; do
    relative_path=${source_file#"${PROJECT_ROOT}/src/"}
    cmp "${source_file}" \
        "${EXTRACT_DIR}/payload/asc/impl/comm_api/${relative_path}"
done < <(find "${PROJECT_ROOT}/src" -type f -name '*.h' \
    ! -path "${PROJECT_ROOT}/src/aicore/hcomm/*" -print0 | sort -z)

mkdir -p \
    "${FAKE_CANN}/asc/include/adv_api/hcomm" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged" \
    "${BASELINE_DIR}"
write_dependency_versions "${FAKE_CANN}"

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

expect_failure run_package --install --install-path="${FAKE_CANN}"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

run_package --full --install-path="${FAKE_CANN}"

while read -r _ path; do
    cmp "${EXTRACT_DIR}/payload/${path}" "${FAKE_CANN}/${path}"
    [[ "$(stat -c %a -- "${FAKE_CANN}/${path}")" == "${EXPECTED_FILE_MODE}" ]]
done < "${EXTRACT_DIR}/manifest.sha256"
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/adv_api/hcomm")" == "${EXPECTED_HCOMM_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm")" == "${EXPECTED_HCOMM_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common")" == "${EXPECTED_HCOMM_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged")" == "701" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/comm_api")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/comm_api/aicore")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/comm_api/aicore/ain")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/comm_api/ccu")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/comm_api")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/comm_api/aicore")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/comm_api/aicore/ain")" == "${EXPECTED_COMM_API_DIR_MODE}" ]]
[[ -L "${FAKE_CANN}/asc/include/comm_api/aicore/hcomm" ]]
[[ "$(readlink -- "${FAKE_CANN}/asc/include/comm_api/aicore/hcomm")" == "../../adv_api/hcomm" ]]
[[ -L "${FAKE_CANN}/asc/impl/comm_api/aicore/hcomm" ]]
[[ "$(readlink -- "${FAKE_CANN}/asc/impl/comm_api/aicore/hcomm")" == "../../adv_api/detail/hcomm" ]]

# Relative includes must resolve through the installed layout and Hcomm links.
while IFS= read -r installed_header; do
    while IFS= read -r include_path; do
        case "${include_path}" in
            ../*)
                [[ -f "$(dirname -- "${installed_header}")/${include_path}" ]]
                ;;
        esac
    done < <(awk -F '"' '/^[[:space:]]*#[[:space:]]*include[[:space:]]*"/ {print $2}' \
        "${installed_header}")
done < <(find \
    "${FAKE_CANN}/asc/include/adv_api/hcomm" \
    "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm" \
    "${FAKE_CANN}/asc/include/comm_api" \
    "${FAKE_CANN}/asc/impl/comm_api" \
    -type f -name '*.h' | sort)

[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/active.manifest.sha256" ]]
[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/active.links.tsv" ]]
grep -qx 'FormatVersion=3' "${FAKE_CANN}/var/asc-comm-dev-patch/state.info"
[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/baseline/directories.tsv" ]]
[[ -f "${FAKE_CANN}/var/asc-comm-dev-patch/baseline/links.tsv" ]]

# Recover an incomplete baseline manifest only when --force is used and the
# original backup still provides an unambiguous baseline.
INCOMPLETE_BASELINE_PATH="asc/impl/adv_api/detail/hcomm/common/hcomm_base.h"
BASELINE_MANIFEST="${FAKE_CANN}/var/asc-comm-dev-patch/baseline/manifest.tsv"
remove_baseline_entry "${BASELINE_MANIFEST}" "${INCOMPLETE_BASELINE_PATH}"
expect_failure run_package --full --install-path="${FAKE_CANN}"
grep -q 'use --force to recover from existing backups' "${ERROR_LOG}"
run_package --full --force --install-path="${FAKE_CANN}"
grep -Fqx $'present\t'"${INCOMPLETE_BASELINE_PATH}" "${BASELINE_MANIFEST}"

UNRECOVERABLE_BASELINE_PATH="asc/impl/adv_api/detail/hcomm/common/hcomm_inner_def.h"
UNRECOVERABLE_BACKUP="${FAKE_CANN}/var/asc-comm-dev-patch/baseline/files/${UNRECOVERABLE_BASELINE_PATH}"
remove_baseline_entry "${BASELINE_MANIFEST}" "${UNRECOVERABLE_BASELINE_PATH}"
rm -f -- "${UNRECOVERABLE_BACKUP}"
expect_failure run_package --full --force --install-path="${FAKE_CANN}"
grep -q 'backup is unavailable' "${ERROR_LOG}"
mkdir -p "$(dirname -- "${UNRECOVERABLE_BACKUP}")"
cp -a "${BASELINE_DIR}/${UNRECOVERABLE_BASELINE_PATH}" "${UNRECOVERABLE_BACKUP}"
run_package --full --force --install-path="${FAKE_CANN}"

# Missing managed links are protected like modified managed files.
chmod 750 "${FAKE_CANN}/asc/include/comm_api/aicore"
rm -f -- "${FAKE_CANN}/asc/include/comm_api/aicore/hcomm"
expect_failure run_package --full --install-path="${FAKE_CANN}"
run_package --full --install-path="${FAKE_CANN}" --force
[[ "$(readlink -- "${FAKE_CANN}/asc/include/comm_api/aicore/hcomm")" == "../../adv_api/hcomm" ]]

# Protect local changes made directly in an already patched CANN tree.
chmod 750 "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
printf 'external change\n' > "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
expect_failure run_package --uninstall --install-path="${FAKE_CANN}"
grep -q '^external change$' "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
[[ -d "${FAKE_CANN}/var/asc-comm-dev-patch" ]]
expect_failure run_package --full --install-path="${FAKE_CANN}"
grep -q '^external change$' "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"

run_package --full --install-path="${FAKE_CANN}" --force

# Reinstalling must retain the original baseline rather than backing up patched files.
run_package --full --install-path="${FAKE_CANN}"
remove_baseline_entry "${BASELINE_MANIFEST}" "${INCOMPLETE_BASELINE_PATH}"
expect_failure run_package --uninstall --install-path="${FAKE_CANN}"
run_package --uninstall --force --install-path="${FAKE_CANN}"

while read -r _ path; do
    cmp "${BASELINE_DIR}/${path}" "${FAKE_CANN}/${path}"
    [[ "$(stat -c %a -- "${FAKE_CANN}/${path}")" == "540" ]]
done < "${EXTRACT_DIR}/manifest.sha256"
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/include/adv_api/hcomm")" == "751" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm")" == "711" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/common")" == "700" ]]
[[ "$(stat -c %a -- "${FAKE_CANN}/asc/impl/adv_api/detail/hcomm/unmanaged")" == "701" ]]
[[ ! -L "${FAKE_CANN}/asc/include/comm_api/aicore/hcomm" ]]
[[ ! -L "${FAKE_CANN}/asc/impl/comm_api/aicore/hcomm" ]]
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

# Correct links that predate the patch remain after uninstall.
PREEXISTING_LINK_CANN="${TEST_ROOT}/preexisting-link-cann"
mkdir -p \
    "${PREEXISTING_LINK_CANN}/asc/include/adv_api" \
    "${PREEXISTING_LINK_CANN}/asc/impl/adv_api/detail" \
    "${PREEXISTING_LINK_CANN}/asc/include/comm_api/aicore" \
    "${PREEXISTING_LINK_CANN}/asc/impl/comm_api/aicore"
ln -s ../../adv_api/hcomm \
    "${PREEXISTING_LINK_CANN}/asc/include/comm_api/aicore/hcomm"
ln -s ../../adv_api/detail/hcomm \
    "${PREEXISTING_LINK_CANN}/asc/impl/comm_api/aicore/hcomm"
write_dependency_versions "${PREEXISTING_LINK_CANN}"
run_package --full --install-path="${PREEXISTING_LINK_CANN}"
run_package --uninstall --install-path="${PREEXISTING_LINK_CANN}"
[[ "$(readlink -- "${PREEXISTING_LINK_CANN}/asc/include/comm_api/aicore/hcomm")" == \
    "../../adv_api/hcomm" ]]
[[ "$(readlink -- "${PREEXISTING_LINK_CANN}/asc/impl/comm_api/aicore/hcomm")" == \
    "../../adv_api/detail/hcomm" ]]

# Match devkit's --install-for-all behavior for a non-root installation as well.
INSTALL_FOR_ALL_CANN="${TEST_ROOT}/install-for-all-cann"
mkdir -p \
    "${INSTALL_FOR_ALL_CANN}/asc/include/adv_api" \
    "${INSTALL_FOR_ALL_CANN}/asc/impl/adv_api/detail"
chmod 755 "${INSTALL_FOR_ALL_CANN}"
write_dependency_versions "${INSTALL_FOR_ALL_CANN}"
run_package --full --install-for-all --install-path="${INSTALL_FOR_ALL_CANN}"
while read -r _ path; do
    [[ "$(stat -c %a -- "${INSTALL_FOR_ALL_CANN}/${path}")" == "555" ]]
done < "${EXTRACT_DIR}/manifest.sha256"
[[ "$(stat -c %a -- "${INSTALL_FOR_ALL_CANN}/asc/include/adv_api/hcomm")" == "755" ]]
[[ "$(stat -c %a -- "${INSTALL_FOR_ALL_CANN}/asc/include/comm_api")" == "555" ]]
run_package --uninstall --install-path="${INSTALL_FOR_ALL_CANN}"

# A restrictive public parent directory must reject install-for-all before mutation.
RESTRICTED_PARENT_CANN="${TEST_ROOT}/restricted-parent-cann"
mkdir -p \
    "${RESTRICTED_PARENT_CANN}/asc/include/adv_api" \
    "${RESTRICTED_PARENT_CANN}/asc/impl/adv_api/detail"
chmod 755 "${RESTRICTED_PARENT_CANN}"
chmod 750 "${RESTRICTED_PARENT_CANN}/asc"
write_dependency_versions "${RESTRICTED_PARENT_CANN}"
expect_failure run_package --full --install-for-all --install-path="${RESTRICTED_PARENT_CANN}"
grep -q 'directory .* does not support --install-for-all' "${ERROR_LOG}"
[[ ! -e "${RESTRICTED_PARENT_CANN}/var/asc-comm-dev-patch" ]]

if [[ $(id -u) -ne 0 ]]; then
    RESTRICTED_CANN="${TEST_ROOT}/restricted-cann"
    mkdir -p \
        "${RESTRICTED_CANN}/asc/include/adv_api" \
        "${RESTRICTED_CANN}/asc/impl/adv_api/detail"
    chmod 750 "${RESTRICTED_CANN}"
    write_dependency_versions "${RESTRICTED_CANN}"
    expect_failure run_package --full --install-for-all --install-path="${RESTRICTED_CANN}"
    grep -q 'does not support --install-for-all' "${ERROR_LOG}"
    [[ ! -e "${RESTRICTED_CANN}/var/asc-comm-dev-patch" ]]
fi

# Wrong links and non-link occupants are never overwritten, including with --force.
INVALID_LINK_CANN="${TEST_ROOT}/invalid-link-cann"
mkdir -p \
    "${INVALID_LINK_CANN}/asc/include/adv_api" \
    "${INVALID_LINK_CANN}/asc/impl/adv_api/detail" \
    "${INVALID_LINK_CANN}/asc/include/comm_api/aicore"
ln -s ../wrong-target "${INVALID_LINK_CANN}/asc/include/comm_api/aicore/hcomm"
write_dependency_versions "${INVALID_LINK_CANN}"
expect_failure run_package --full --force --install-path="${INVALID_LINK_CANN}"
[[ "$(readlink -- "${INVALID_LINK_CANN}/asc/include/comm_api/aicore/hcomm")" == \
    "../wrong-target" ]]
[[ ! -e "${INVALID_LINK_CANN}/var/asc-comm-dev-patch" ]]

OCCUPIED_LINK_CANN="${TEST_ROOT}/occupied-link-cann"
mkdir -p \
    "${OCCUPIED_LINK_CANN}/asc/include/adv_api" \
    "${OCCUPIED_LINK_CANN}/asc/impl/adv_api/detail" \
    "${OCCUPIED_LINK_CANN}/asc/impl/comm_api/aicore/hcomm"
write_dependency_versions "${OCCUPIED_LINK_CANN}"
expect_failure run_package --full --force --install-path="${OCCUPIED_LINK_CANN}"
[[ -d "${OCCUPIED_LINK_CANN}/asc/impl/comm_api/aicore/hcomm" ]]
[[ ! -e "${OCCUPIED_LINK_CANN}/var/asc-comm-dev-patch" ]]

# A dependency mismatch follows the CANN package convention: warn and continue.
printf 'Version=0.0.0\n' > "${FAKE_CANN}/share/info/asc-devkit/version.info"
run_package --check --install-path="${FAKE_CANN}" > "${ERROR_LOG}" 2>&1
grep -q 'Version compatibility check failed' "${ERROR_LOG}"
cmp "${BASELINE_DIR}/asc/include/adv_api/hcomm/hcomm.h" \
    "${FAKE_CANN}/asc/include/adv_api/hcomm/hcomm.h"
[[ ! -e "${FAKE_CANN}/var/asc-comm-dev-patch" ]]

if [[ $(id -u) -ne 0 ]]; then
    [[ "$(stat -c %a -- "${LOG_DIR}")" == "740" ]]
    [[ "$(stat -c %a -- "${LOG_DIR}/ascend_install.log")" == "640" ]]
    [[ "$(stat -c %a -- "${LOG_DIR}/operation.log")" == "640" ]]
    grep -q '^Install SUGGESTION .* succeeded installmode=full;' "${LOG_DIR}/operation.log"
    grep -q '^Upgrade MINOR .* succeeded installmode=full;' "${LOG_DIR}/operation.log"
    grep -q '^Uninstall MAJOR .* succeeded installmode=uninstall;' "${LOG_DIR}/operation.log"
fi

echo "asc-comm run package install/reinstall/uninstall test passed"
