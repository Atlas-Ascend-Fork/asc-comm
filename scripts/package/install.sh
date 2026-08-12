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

set -Eeuo pipefail
umask 027

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PAYLOAD_DIR="${SOURCE_DIR}/payload"
PACKAGE_MANIFEST="${SOURCE_DIR}/manifest.sha256"
PACKAGE_INFO="${SOURCE_DIR}/version.info"

ACTION=""
INSTALL_PATH=${ASCEND_HOME_PATH:-}
FORCE=false
CANN_ROOT=""
STATE_DIR=""
TRANSACTION_DIR=""
WORK_DIR=""
MUTATION_STARTED=false
COMMITTED=false

usage()
{
    cat <<EOF
Usage: $0 --full|--uninstall|--check [OPTION]...

Options:
  --full                  Install or update the asc-comm development patch
  --uninstall             Restore the files saved before the first installation
  --check                 Verify the package payload without modifying CANN
  --install-path=<PATH>   CANN root; defaults to ASCEND_HOME_PATH
  --force                 Ignore version or installed-file consistency checks
  -h, --help              Display this help
EOF
}

log()
{
    local level=$1
    shift
    echo "[AscComm] [$(date '+%Y-%m-%d %H:%M:%S')] [${level}]: $*"
}

fail()
{
    log "ERROR" "$*" >&2
    exit 1
}

parse_args()
{
    # CANN's makeself header prepends the run-file path and caller directory.
    if [[ $# -ge 2 && "$1" == --*.run && "$2" == --/* ]]; then
        shift 2
    fi

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --full|--uninstall|--check)
                [[ -z "${ACTION}" ]] || fail "specify exactly one operation"
                ACTION=$1
                ;;
            --install-path=*)
                INSTALL_PATH=${1#*=}
                ;;
            --force)
                FORCE=true
                ;;
            --quiet)
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                fail "unknown argument: $1"
                ;;
        esac
        shift
    done

    [[ -n "${ACTION}" ]] || fail "specify --full, --uninstall, or --check"
    [[ "${ACTION}" == "--check" ]] && return 0
    [[ -n "${INSTALL_PATH}" ]] || fail "CANN path is not set; source set_env.sh or use --install-path"
    [[ "${INSTALL_PATH}" = /* ]] || fail "CANN path must be absolute: ${INSTALL_PATH}"
}

is_cann_root()
{
    local path=$1
    [[ -d "${path}/asc/include/adv_api" && \
        -d "${path}/asc/impl/adv_api/detail" && \
        -f "${path}/share/info/asc-devkit/version.info" ]]
}

resolve_cann_root()
{
    local candidate
    local resolved
    local candidates=(
        "${INSTALL_PATH}"
        "${INSTALL_PATH}/cann"
        "${INSTALL_PATH}/ascend-toolkit/latest"
    )

    for candidate in "${candidates[@]}"; do
        [[ -e "${candidate}" ]] || continue
        resolved=$(readlink -f -- "${candidate}")
        if is_cann_root "${resolved}"; then
            CANN_ROOT=${resolved}
            STATE_DIR="${CANN_ROOT}/var/asc-comm-dev-patch"
            return 0
        fi
    done
    fail "the path does not contain a valid CANN installation: ${INSTALL_PATH}"
}

check_owner()
{
    local current_uid
    local owner_uid
    current_uid=$(id -u)
    owner_uid=$(stat -c %u -- "${CANN_ROOT}")
    [[ "${current_uid}" == "${owner_uid}" ]] || \
        fail "current user uid ${current_uid} does not own CANN path ${CANN_ROOT} (uid ${owner_uid})"
}

normalize_arch()
{
    case "$(uname -m)" in
        x86_64)
            echo "x86_64"
            ;;
        aarch64|arm64)
            echo "aarch64"
            ;;
        *)
            fail "unsupported architecture: $(uname -m)"
            ;;
    esac
}

read_info_value()
{
    local key=$1
    local file=$2
    awk -F= -v key="${key}" '$1 == key {sub(/^[^=]*=/, ""); print; exit}' "${file}"
}

is_allowed_path()
{
    local path=$1
    case "${path}" in
        asc/include/adv_api/*/*.h|asc/impl/adv_api/detail/*/*.h|\
        aarch64-linux/include/hcomm/*/*.h|x86_64-linux/include/hcomm/*/*.h|\
        aarch64-linux/lib64/libasccomm_hcomm_host.so|x86_64-linux/lib64/libasccomm_hcomm_host.so)
            ;;
        *)
            return 1
            ;;
    esac
    [[ "${path}" != /* && "${path}" != *"/../"* && "${path}" != *"/.." && \
        "${path}" != ../* && "${path}" != *"/./"* && "${path}" != *"/." && \
        "${path}" != *"//"* && "${path}" != *$'\t'* ]]
}

is_allowed_directory_path()
{
    local path=$1
    case "${path}" in
        asc/include/adv_api/*|asc/impl/adv_api/detail/*|\
        aarch64-linux/include/hcomm|aarch64-linux/include/hcomm/*|aarch64-linux/lib64|\
        x86_64-linux/include/hcomm|x86_64-linux/include/hcomm/*|x86_64-linux/lib64)
            ;;
        *)
            return 1
            ;;
    esac
    [[ "${path}" != /* && "${path}" != *"/../"* && "${path}" != *"/.." && \
        "${path}" != ../* && "${path}" != *"/./"* && "${path}" != *"/." && \
        "${path}" != *"//"* && "${path}" != *$'\t'* ]]
}

collect_managed_directories()
{
    local paths_file=$1
    local output=$2
    local path
    local relative_path
    local module
    local directory
    local managed_root

    : > "${output}"
    while IFS= read -r path; do
        is_allowed_path "${path}" || fail "unsafe managed path: ${path}"
        case "${path}" in
            asc/include/adv_api/*)
                relative_path=${path#asc/include/adv_api/}
                module=${relative_path%%/*}
                managed_root="asc/include/adv_api/${module}"
                ;;
            asc/impl/adv_api/detail/*)
                relative_path=${path#asc/impl/adv_api/detail/}
                module=${relative_path%%/*}
                managed_root="asc/impl/adv_api/detail/${module}"
                ;;
            aarch64-linux/include/hcomm/*|x86_64-linux/include/hcomm/*)
                managed_root="${path%%/include/hcomm/*}/include/hcomm"
                ;;
            aarch64-linux/lib64/libasccomm_hcomm_host.so|x86_64-linux/lib64/libasccomm_hcomm_host.so)
                managed_root="${path%%/lib64/*}/lib64"
                ;;
        esac

        directory=$(dirname -- "${path}")
        while [[ "${directory}" == "${managed_root}" || "${directory}" == "${managed_root}/"* ]]; do
            printf '%s\n' "${directory}" >> "${output}"
            [[ "${directory}" == "${managed_root}" ]] && break
            directory=$(dirname -- "${directory}")
        done
    done < "${paths_file}"
    sort -u -o "${output}" "${output}"
}

collect_checksum_paths()
{
    local manifest=$1
    local output=$2
    local checksum
    local path
    local extra
    local count=0

    [[ -f "${manifest}" ]] || fail "manifest not found: ${manifest}"
    : > "${output}"
    while read -r checksum path extra; do
        [[ -z "${extra:-}" ]] || fail "invalid manifest entry in ${manifest}"
        [[ "${checksum}" =~ ^[0-9A-Fa-f]{64}$ ]] || fail "invalid checksum in ${manifest}"
        is_allowed_path "${path}" || fail "unsafe path in ${manifest}: ${path}"
        printf '%s\n' "${path}" >> "${output}"
        count=$((count + 1))
    done < "${manifest}"
    [[ ${count} -gt 0 ]] || fail "manifest is empty: ${manifest}"
    [[ $(sort -u "${output}" | wc -l) -eq ${count} ]] || fail "duplicate path in ${manifest}"
}

collect_baseline_paths()
{
    local manifest=$1
    local output=$2
    local status
    local path
    local extra
    local count=0

    [[ -f "${manifest}" ]] || fail "baseline manifest not found: ${manifest}"
    : > "${output}"
    while IFS=$'\t' read -r status path extra; do
        [[ -z "${extra:-}" ]] || fail "invalid baseline entry in ${manifest}"
        [[ "${status}" == "present" || "${status}" == "absent" ]] || \
            fail "invalid baseline state in ${manifest}: ${status}"
        is_allowed_path "${path}" || fail "unsafe path in ${manifest}: ${path}"
        if [[ "${status}" == "present" ]]; then
            [[ -f "${STATE_DIR}/baseline/files/${path}" && ! -L "${STATE_DIR}/baseline/files/${path}" ]] || \
                fail "baseline backup is missing or invalid: ${path}"
        fi
        printf '%s\n' "${path}" >> "${output}"
        count=$((count + 1))
    done < "${manifest}"
    [[ ${count} -gt 0 ]] || fail "baseline manifest is empty: ${manifest}"
    [[ $(sort -u "${output}" | wc -l) -eq ${count} ]] || fail "duplicate path in ${manifest}"
}

validate_baseline_coverage()
{
    local active_paths=$1
    local baseline_paths=$2
    local path

    while IFS= read -r path; do
        grep -Fqx -- "${path}" "${baseline_paths}" || \
            fail "baseline does not cover installed patch path: ${path}"
    done < "${active_paths}"
}

verify_checksums()
{
    local root=$1
    local manifest=$2

    (cd "${root}" && sha256sum -c "${manifest}" >/dev/null)
}

validate_package()
{
    local package_arch
    local current_arch
    local required_version
    local installed_version

    [[ -d "${PAYLOAD_DIR}" && -f "${PACKAGE_INFO}" ]] || fail "package payload is incomplete"
    package_arch=$(read_info_value "Arch" "${PACKAGE_INFO}")
    current_arch=$(normalize_arch)
    [[ "${package_arch}" == "${current_arch}" ]] || \
        fail "package architecture ${package_arch} does not match ${current_arch}"

    required_version=$(read_info_value "RequiredAscDevkitVersion" "${PACKAGE_INFO}")
    installed_version=$(read_info_value "Version" "${CANN_ROOT}/share/info/asc-devkit/version.info")
    [[ -n "${required_version}" && -n "${installed_version}" ]] || fail "failed to read asc-devkit version"
    if [[ "${required_version}" != "${installed_version}" ]]; then
        if [[ "${FORCE}" == true ]]; then
            log "WARNING" "asc-devkit version mismatch: package=${required_version}, installed=${installed_version}"
        else
            fail "asc-devkit version mismatch: package=${required_version}, installed=${installed_version}; use --force to override"
        fi
    fi
}

check_target_parent()
{
    local target=$1
    local parent
    local resolved_parent

    parent=$(dirname -- "${target}")
    resolved_parent=$(readlink -m -- "${parent}") || \
        fail "failed to resolve target parent directory: ${parent}"
    if [[ "${CANN_ROOT}" != "/" && \
        "${resolved_parent}" != "${CANN_ROOT}" && \
        "${resolved_parent}" != "${CANN_ROOT}/"* ]]; then
        fail "target path resolves outside CANN root: ${target}"
    fi
}

directory_path_has_symlink()
{
    local path=$1
    local current="${CANN_ROOT}"
    local component
    local components=()

    IFS='/' read -r -a components <<< "${path}"
    for component in "${components[@]}"; do
        current="${current}/${component}"
        [[ -L "${current}" ]] && return 0
    done
    return 1
}

validate_directory_mode_manifest()
{
    local manifest=$1
    local mode
    local path
    local extra

    [[ -e "${manifest}" ]] || return 0
    [[ -f "${manifest}" && ! -L "${manifest}" ]] || \
        fail "directory mode manifest is invalid: ${manifest}"
    while IFS=$'\t' read -r mode path extra; do
        [[ -z "${extra:-}" ]] || fail "invalid directory mode entry in ${manifest}"
        [[ "${mode}" =~ ^[0-7]{1,4}$ ]] || fail "invalid directory mode in ${manifest}: ${mode}"
        is_allowed_directory_path "${path}" || fail "unsafe directory path in ${manifest}: ${path}"
    done < "${manifest}"
}

validate_state_format()
{
    local state_info="${STATE_DIR}/state.info"
    local directory_manifest="${STATE_DIR}/baseline/directories.tsv"
    local format_version

    [[ -f "${state_info}" && ! -L "${state_info}" ]] || fail "patch state metadata is invalid: ${state_info}"
    format_version=$(read_info_value "FormatVersion" "${state_info}")
    case "${format_version}" in
        1)
            ;;
        2)
            [[ -f "${directory_manifest}" && ! -L "${directory_manifest}" ]] || \
                fail "directory mode manifest is missing from patch state: ${directory_manifest}"
            ;;
        *)
            fail "unsupported patch state format: ${format_version:-missing}"
            ;;
    esac
}

record_directory_modes()
{
    local directories_file=$1
    local manifest=$2
    local path
    local target

    : > "${manifest}"
    while IFS= read -r path; do
        is_allowed_directory_path "${path}" || fail "unsafe managed directory path: ${path}"
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -d "${target}" && ! -L "${target}" ]] && ! directory_path_has_symlink "${path}"; then
            printf '%s\t%s\n' "$(stat -c %a -- "${target}")" "${path}" >> "${manifest}"
        fi
    done < "${directories_file}"
}

restore_directory_modes()
{
    local manifest=$1
    local mode
    local path
    local target

    [[ -f "${manifest}" ]] || return 0
    while IFS=$'\t' read -r mode path; do
        [[ "${mode}" =~ ^[0-7]{1,4}$ ]] || return 1
        is_allowed_directory_path "${path}" || return 1
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}" || return 1
        [[ -d "${target}" && ! -L "${target}" ]] || return 1
        directory_path_has_symlink "${path}" && return 1
        chmod "${mode}" "${target}" || return 1
    done < "${manifest}"
}

apply_directory_modes()
{
    local directories_file=$1
    local path
    local target

    while IFS= read -r path; do
        is_allowed_directory_path "${path}" || fail "unsafe managed directory path: ${path}"
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -d "${target}" && ! -L "${target}" ]] && ! directory_path_has_symlink "${path}"; then
            chmod 750 "${target}"
        fi
    done < "${directories_file}"
}

remove_new_empty_directories()
{
    local directories_file=$1
    local baseline_manifest=$2
    local path
    local target

    [[ -f "${baseline_manifest}" ]] || return 0
    sort -r "${directories_file}" | while IFS= read -r path; do
        if awk -F '\t' -v path="${path}" '$2 == path {found=1} END {exit !found}' "${baseline_manifest}"; then
            continue
        fi
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        [[ ! -L "${target}" ]] && rmdir -- "${target}" 2>/dev/null || true
    done
}

validate_state_dir()
{
    check_target_parent "${STATE_DIR}"
    if [[ -L "${STATE_DIR}" || (-e "${STATE_DIR}" && ! -d "${STATE_DIR}") ]]; then
        fail "backup state path is invalid; expected a directory or an absent path: ${STATE_DIR}"
    fi
}

check_regular_target()
{
    local target=$1

    check_target_parent "${target}"
    if [[ -L "${target}" || (-e "${target}" && ! -f "${target}") ]]; then
        fail "refusing to replace a non-regular file: ${target}"
    fi
}

validate_target_paths()
{
    local paths_file=$1
    local path

    while IFS= read -r path; do
        check_regular_target "${CANN_ROOT}/${path}"
    done < "${paths_file}"
}

snapshot_transaction()
{
    local paths_file=$1
    local directories_file=$2
    local path
    local target

    validate_state_dir
    mkdir -p "${CANN_ROOT}/var"
    TRANSACTION_DIR=$(mktemp -d "${CANN_ROOT}/var/.asc-comm-transaction.XXXXXX")
    : > "${TRANSACTION_DIR}/targets.tsv"

    if [[ -d "${STATE_DIR}" ]]; then
        cp -a -- "${STATE_DIR}" "${TRANSACTION_DIR}/state"
        touch "${TRANSACTION_DIR}/state.existed"
    fi

    while IFS= read -r path; do
        target="${CANN_ROOT}/${path}"
        check_regular_target "${target}"
        if [[ -f "${target}" ]]; then
            printf 'present\t%s\n' "${path}" >> "${TRANSACTION_DIR}/targets.tsv"
            mkdir -p "${TRANSACTION_DIR}/targets/$(dirname -- "${path}")"
            cp -a -- "${target}" "${TRANSACTION_DIR}/targets/${path}"
        else
            printf 'absent\t%s\n' "${path}" >> "${TRANSACTION_DIR}/targets.tsv"
        fi
    done < "${paths_file}"

    record_directory_modes "${directories_file}" "${TRANSACTION_DIR}/directories.tsv"

    MUTATION_STARTED=true
}

rollback_transaction()
{
    local status
    local path
    local target

    set +e
    if [[ -f "${TRANSACTION_DIR}/targets.tsv" ]]; then
        while IFS=$'\t' read -r status path; do
            target="${CANN_ROOT}/${path}"
            if [[ "${status}" == "present" ]]; then
                mkdir -p "$(dirname -- "${target}")"
                rm -f -- "${target}"
                cp -a -- "${TRANSACTION_DIR}/targets/${path}" "${target}"
            else
                rm -f -- "${target}"
            fi
        done < "${TRANSACTION_DIR}/targets.tsv"
    fi

    restore_directory_modes "${TRANSACTION_DIR}/directories.tsv"

    rm -rf -- "${STATE_DIR}"
    if [[ -f "${TRANSACTION_DIR}/state.existed" ]]; then
        cp -a -- "${TRANSACTION_DIR}/state" "${STATE_DIR}"
    fi
    log "WARNING" "operation failed; restored the state before this operation"
}

handle_exit()
{
    local exit_code=$?
    trap - EXIT
    set +e
    if [[ "${MUTATION_STARTED}" == true && "${COMMITTED}" != true ]]; then
        rollback_transaction
    fi
    if [[ -n "${TRANSACTION_DIR}" && -d "${TRANSACTION_DIR}" ]]; then
        rm -rf -- "${TRANSACTION_DIR}"
    fi
    if [[ -n "${WORK_DIR}" && -d "${WORK_DIR}" ]]; then
        rm -rf -- "${WORK_DIR}"
    fi
    exit "${exit_code}"
}

restore_baseline()
{
    local paths_file=$1
    local manifest="${STATE_DIR}/baseline/manifest.tsv"
    local status
    local path
    local target

    [[ -f "${manifest}" ]] || {
        [[ ! -s "${paths_file}" ]] && return 0
        fail "baseline manifest not found: ${manifest}"
    }
    while IFS= read -r path; do
        status=$(awk -F '\t' -v path="${path}" '$2 == path {print $1; exit}' "${manifest}")
        [[ "${status}" == "present" || "${status}" == "absent" ]] || \
            fail "baseline does not cover installed patch path: ${path}"
        target="${CANN_ROOT}/${path}"
        check_regular_target "${target}"
        if [[ "${status}" == "present" ]]; then
            mkdir -p "$(dirname -- "${target}")"
            rm -f -- "${target}"
            cp -a -- "${STATE_DIR}/baseline/files/${path}" "${target}"
        else
            rm -f -- "${target}"
        fi
    done < "${paths_file}"
}

record_new_baselines()
{
    local paths_file=$1
    local directories_file=$2
    local manifest="${STATE_DIR}/baseline/manifest.tsv"
    local directory_manifest="${STATE_DIR}/baseline/directories.tsv"
    local path
    local target

    mkdir -p "${STATE_DIR}/baseline/files"
    touch "${manifest}"
    chmod 640 "${manifest}"
    while IFS= read -r path; do
        if awk -F '\t' -v path="${path}" '$2 == path {found=1} END {exit !found}' "${manifest}"; then
            continue
        fi
        target="${CANN_ROOT}/${path}"
        check_regular_target "${target}"
        if [[ -f "${target}" ]]; then
            printf 'present\t%s\n' "${path}" >> "${manifest}"
            mkdir -p "${STATE_DIR}/baseline/files/$(dirname -- "${path}")"
            cp -a -- "${target}" "${STATE_DIR}/baseline/files/${path}"
        else
            printf 'absent\t%s\n' "${path}" >> "${manifest}"
        fi
    done < "${paths_file}"
    chmod 440 "${manifest}"
    record_directory_modes "${directories_file}" "${directory_manifest}"
    chmod 440 "${directory_manifest}"
}

install_payload()
{
    local paths_file=$1
    local directories_file=$2
    local path
    local target

    apply_directory_modes "${directories_file}"
    while IFS= read -r path; do
        [[ -f "${PAYLOAD_DIR}/${path}" && ! -L "${PAYLOAD_DIR}/${path}" ]] || \
            fail "payload file is missing or invalid: ${path}"
        target="${CANN_ROOT}/${path}"
        check_regular_target "${target}"
        install -D -m 550 "${PAYLOAD_DIR}/${path}" "${target}"
    done < "${paths_file}"

    apply_directory_modes "${directories_file}"
}

install_patch()
{
    local package_paths
    local baseline_paths
    local active_paths
    local touched_paths
    local package_directories
    local touched_directories
    local directory_manifest="${STATE_DIR}/baseline/directories.tsv"

    validate_package
    WORK_DIR=$(mktemp -d)
    package_paths="${WORK_DIR}/package.paths"
    baseline_paths="${WORK_DIR}/baseline.paths"
    active_paths="${WORK_DIR}/active.paths"
    touched_paths="${WORK_DIR}/touched.paths"
    package_directories="${WORK_DIR}/package.directories"
    touched_directories="${WORK_DIR}/touched.directories"

    collect_checksum_paths "${PACKAGE_MANIFEST}" "${package_paths}"
    collect_managed_directories "${package_paths}" "${package_directories}"
    verify_checksums "${PAYLOAD_DIR}" "${PACKAGE_MANIFEST}" || fail "package payload checksum verification failed"
    : > "${baseline_paths}"
    : > "${active_paths}"

    if [[ -d "${STATE_DIR}" ]]; then
        validate_state_format
        collect_baseline_paths "${STATE_DIR}/baseline/manifest.tsv" "${baseline_paths}"
        collect_checksum_paths "${STATE_DIR}/active.manifest.sha256" "${active_paths}"
        validate_directory_mode_manifest "${directory_manifest}"
        validate_baseline_coverage "${active_paths}" "${baseline_paths}"
        if ! verify_checksums "${CANN_ROOT}" "${STATE_DIR}/active.manifest.sha256"; then
            if [[ "${FORCE}" == true ]]; then
                log "WARNING" "installed patch files were changed; --force will overwrite them"
            else
                fail "installed patch files were changed; use --force to overwrite or preserve them manually"
            fi
        fi
    fi

    log "INFO" "Start installing asc-comm development patch."
    log "INFO" "Package version: $(read_info_value PackageVersion "${PACKAGE_INFO}"); source commit: $(read_info_value GitCommit "${PACKAGE_INFO}"); source modified: $(read_info_value GitDirty "${PACKAGE_INFO}")"
    log "INFO" "Target CANN: ${CANN_ROOT}; asc-devkit version: $(read_info_value Version "${CANN_ROOT}/share/info/asc-devkit/version.info")"
    log "INFO" "Files to replace: $(wc -l < "${package_paths}"); original files backup: ${STATE_DIR}/baseline"

    sort -u "${package_paths}" "${active_paths}" > "${touched_paths}"
    collect_managed_directories "${touched_paths}" "${touched_directories}"
    validate_target_paths "${touched_paths}"
    snapshot_transaction "${touched_paths}" "${touched_directories}"
    restore_baseline "${active_paths}"
    remove_new_empty_directories "${touched_directories}" "${directory_manifest}"
    restore_directory_modes "${directory_manifest}" || fail "failed to restore original directory permissions"
    rm -rf -- "${STATE_DIR}"
    record_new_baselines "${package_paths}" "${package_directories}"
    install_payload "${package_paths}" "${package_directories}"

    install -m 440 "${PACKAGE_MANIFEST}" "${STATE_DIR}/active.manifest.sha256"
    install -m 440 "${PACKAGE_INFO}" "${STATE_DIR}/version.info"
    printf 'FormatVersion=2\n' > "${WORK_DIR}/state.info"
    install -m 440 "${WORK_DIR}/state.info" "${STATE_DIR}/state.info"
    chmod 750 "${STATE_DIR}" "${STATE_DIR}/baseline"

    COMMITTED=true
    rm -rf -- "${WORK_DIR}"
    WORK_DIR=""
    log "INFO" "AscComm development patch installed successfully."
    log "INFO" "Recompile the target Ascend C kernel before verification."
}

check_package()
{
    local package_paths

    WORK_DIR=$(mktemp -d)
    package_paths="${WORK_DIR}/package.paths"
    collect_checksum_paths "${PACKAGE_MANIFEST}" "${package_paths}"
    verify_checksums "${PAYLOAD_DIR}" "${PACKAGE_MANIFEST}" || fail "package payload checksum verification failed"
    log "INFO" "Package version: $(read_info_value PackageVersion "${PACKAGE_INFO}"); architecture: $(read_info_value Arch "${PACKAGE_INFO}")"
    log "INFO" "Source commit: $(read_info_value GitCommit "${PACKAGE_INFO}"); source modified: $(read_info_value GitDirty "${PACKAGE_INFO}")"
    log "INFO" "Required asc-devkit version: $(read_info_value RequiredAscDevkitVersion "${PACKAGE_INFO}"); files: $(wc -l < "${package_paths}")"
    rm -rf -- "${WORK_DIR}"
    WORK_DIR=""
    log "INFO" "Package payload checksums are valid."
}

uninstall_patch()
{
    local baseline_paths
    local active_paths
    local touched_paths
    local touched_directories
    local directory_manifest="${STATE_DIR}/baseline/directories.tsv"

    [[ -d "${STATE_DIR}" ]] || fail "no asc-comm development patch is installed in ${CANN_ROOT}"
    WORK_DIR=$(mktemp -d)
    baseline_paths="${WORK_DIR}/baseline.paths"
    active_paths="${WORK_DIR}/active.paths"
    touched_paths="${WORK_DIR}/touched.paths"
    touched_directories="${WORK_DIR}/touched.directories"

    validate_state_format
    collect_baseline_paths "${STATE_DIR}/baseline/manifest.tsv" "${baseline_paths}"
    collect_checksum_paths "${STATE_DIR}/active.manifest.sha256" "${active_paths}"
    validate_directory_mode_manifest "${directory_manifest}"
    validate_baseline_coverage "${active_paths}" "${baseline_paths}"
    if ! verify_checksums "${CANN_ROOT}" "${STATE_DIR}/active.manifest.sha256"; then
        if [[ "${FORCE}" == true ]]; then
            log "WARNING" "installed patch files were changed; --force will discard those changes"
        else
            fail "installed patch files were changed; use --force to restore the saved baseline anyway"
        fi
    fi

    log "INFO" "Start uninstalling asc-comm development patch from ${CANN_ROOT}."
    log "INFO" "Installed package version: $(read_info_value PackageVersion "${STATE_DIR}/version.info"); files to restore or remove: $(wc -l < "${active_paths}")"

    sort -u "${active_paths}" > "${touched_paths}"
    collect_managed_directories "${touched_paths}" "${touched_directories}"
    validate_target_paths "${touched_paths}"
    snapshot_transaction "${touched_paths}" "${touched_directories}"
    restore_baseline "${active_paths}"
    remove_new_empty_directories "${touched_directories}" "${directory_manifest}"
    restore_directory_modes "${directory_manifest}" || fail "failed to restore original directory permissions"
    rm -rf -- "${STATE_DIR}"

    COMMITTED=true
    rm -rf -- "${WORK_DIR}"
    WORK_DIR=""
    log "INFO" "AscComm development patch uninstalled successfully."
}

main()
{
    parse_args "$@"
    trap handle_exit EXIT
    if [[ "${ACTION}" == "--check" ]]; then
        check_package
        return 0
    fi

    resolve_cann_root
    check_owner
    validate_state_dir

    if [[ "${ACTION}" == "--full" ]]; then
        install_patch
    else
        uninstall_patch
    fi
}

main "$@"
