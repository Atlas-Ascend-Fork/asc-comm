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
PACKAGE_LINKS="${SOURCE_DIR}/links.tsv"
VERSION_CHECKER_DIR="${SOURCE_DIR}/script"
VERSION_CHECKER="${VERSION_CHECKER_DIR}/check_version_required.awk"
VERSION_COMPAT_LIB="${VERSION_CHECKER_DIR}/version_compatiable.inc"

ACTION=""
if [[ $(id -u) -eq 0 ]]; then
    INSTALL_PATH="/usr/local/Ascend"
    LOG_DIR="/var/log/ascend_seclog"
    LOG_DIR_MODE=750
    INSTALL_FOR_ALL=true
else
    INSTALL_PATH="${HOME}/Ascend"
    LOG_DIR="${HOME}/var/log/ascend_seclog"
    LOG_DIR_MODE=740
    INSTALL_FOR_ALL=false
fi
INSTALL_LOG="${LOG_DIR}/ascend_install.log"
OPERATION_LOG="${LOG_DIR}/operation.log"
FORCE=false
CANN_ROOT=""
STATE_DIR=""
LOCK_FILE=""
TRANSACTION_DIR=""
WORK_DIR=""
LOCK_HELD=false
MUTATION_STARTED=false
COMMITTED=false
STATE_FORMAT_VERSION=""
LOG_READY=false
ACTION_STARTED=false
OPERATION_RECORDED=false
OPERATION_TYPE=""
OPERATION_LEVEL=""
INPUT_PARAMS=""
RUN_USERNAME=$(id -un)
RUN_FILE_NAME="cann-asc-comm"

usage()
{
    cat <<EOF
Usage: $0 --full|--uninstall|--check [OPTION]...

Options:
  --full                  Install or update the asc-comm development patch
  --uninstall             Restore the files saved before the first installation
  --check                 Verify payload and dependency compatibility without modifying CANN
  --install-path=<PATH>   Ascend install root; defaults to /usr/local/Ascend for root
                          or \$HOME/Ascend for a non-root user
  --install-for-all       Allow all users to read and traverse installed files
  --force                 Ignore installed-file consistency checks
  -h, --help              Display this help
EOF
}

log()
{
    local level=$1
    local log_format
    shift
    log_format="[AscComm] [$(date '+%Y-%m-%d %H:%M:%S')] [${level}]: $*"
    echo "${log_format}"
    if [[ "${LOG_READY}" == true ]]; then
        printf '%s\n' "${log_format}" >> "${INSTALL_LOG}"
    fi
}

fail()
{
    log "ERROR" "$*" >&2
    exit 1
}

fail_with_code()
{
    local error_code=$1
    shift
    log "ERROR" "ERR_NO:${error_code};ERR_DES:$*" >&2
    exit 1
}

capture_input_params()
{
    local args=("$@")

    if [[ ${#args[@]} -ge 2 && "${args[0]}" == --*.run && "${args[1]}" == --/* ]]; then
        RUN_FILE_NAME=$(basename -- "${args[0]#--}")
        args=("${args[@]:2}")
    fi
    if [[ ${#args[@]} -gt 0 ]]; then
        printf -v INPUT_PARAMS '%q ' "${args[@]}"
        INPUT_PARAMS=${INPUT_PARAMS% }
    fi
}

init_logging()
{
    mkdir -p "${LOG_DIR}" || {
        echo "[AscComm] [ERROR]: failed to create log directory: ${LOG_DIR}" >&2
        exit 1
    }
    chmod "${LOG_DIR_MODE}" "${LOG_DIR}" || {
        echo "[AscComm] [ERROR]: failed to set log directory permission: ${LOG_DIR}" >&2
        exit 1
    }
    touch "${INSTALL_LOG}" || {
        echo "[AscComm] [ERROR]: failed to create install log: ${INSTALL_LOG}" >&2
        exit 1
    }
    chmod 640 "${INSTALL_LOG}" || {
        echo "[AscComm] [ERROR]: failed to set install log permission: ${INSTALL_LOG}" >&2
        exit 1
    }
    LOG_READY=true
}

start_log()
{
    log "INFO" "Start time:$(date '+%Y-%m-%d %H:%M:%S')"
    log "INFO" "LogFile:${INSTALL_LOG}"
    log "INFO" "InputParams:${INPUT_PARAMS}"
    log "INFO" "OperationLogFile:${OPERATION_LOG}"
}

set_operation()
{
    case "${ACTION}" in
        --full)
            OPERATION_TYPE="Install"
            OPERATION_LEVEL="SUGGESTION"
            ;;
        --uninstall)
            OPERATION_TYPE="Uninstall"
            OPERATION_LEVEL="MAJOR"
            ;;
        --check)
            return 0
            ;;
    esac
    ACTION_STARTED=true
}

detect_upgrade_operation()
{
    if [[ "${ACTION}" == "--full" && -d "${STATE_DIR}" ]]; then
        OPERATION_TYPE="Upgrade"
        OPERATION_LEVEL="MINOR"
    fi
}

log_operation()
{
    local result=$1
    local install_mode=${ACTION#--}

    touch "${OPERATION_LOG}" || return 1
    chmod 640 "${OPERATION_LOG}" || return 1
    printf '%s %s %s %s 127.0.0.1 %s %s installmode=%s; cmdlist=%s\n' \
        "${OPERATION_TYPE}" \
        "${OPERATION_LEVEL}" \
        "${RUN_USERNAME}" \
        "$(date '+%Y-%m-%d %H:%M:%S')" \
        "${RUN_FILE_NAME}" \
        "${result}" \
        "${install_mode}" \
        "${INPUT_PARAMS}" >> "${OPERATION_LOG}" || return 1
    OPERATION_RECORDED=true
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
                [[ -z "${ACTION}" ]] || \
                    fail_with_code "0x0004" "specify exactly one operation"
                ACTION=$1
                ;;
            --install-path=*)
                INSTALL_PATH=${1#*=}
                ;;
            --install-for-all)
                INSTALL_FOR_ALL=true
                ;;
            --force)
                FORCE=true
                ;;
            --quiet)
                # Accepted for makeself compatibility; this installer has no interactive prompts.
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                fail_with_code "0x0004" "unknown argument: $1"
                ;;
        esac
        shift
    done

    [[ -n "${ACTION}" ]] || \
        fail_with_code "0x0004" "specify --full, --uninstall, or --check"
    [[ -n "${INSTALL_PATH}" ]] || fail_with_code "0x0004" "install path is empty"
    [[ "${INSTALL_PATH}" = /* ]] || \
        fail_with_code "0x0004" "CANN path must be absolute: ${INSTALL_PATH}"
}

is_cann_root()
{
    local path=$1
    [[ -d "${path}/asc/include/adv_api" && \
        -d "${path}/asc/impl/adv_api/detail" && \
        -d "${path}/share/info" ]]
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
    fail_with_code "0x0080" \
        "the path does not contain a valid CANN installation: ${INSTALL_PATH}"
}

check_owner()
{
    local current_uid
    local owner_uid
    current_uid=$(id -u)
    owner_uid=$(stat -c %u -- "${CANN_ROOT}")
    [[ "${current_uid}" == "${owner_uid}" ]] || fail_with_code "0x0093" \
        "current user uid ${current_uid} does not own CANN path ${CANN_ROOT} (uid ${owner_uid})"
}

collect_install_for_all_parent_directories()
{
    local path=$1
    local managed_directories=$2
    local output=$3
    local directory
    local relative_directory

    # Avoid a double slash when the CANN root itself is `/`; some dirname
    # implementations preserve `//`, which would otherwise never reach `/`.
    directory=$(dirname -- "${CANN_ROOT%/}/${path}")
    while :; do
        if [[ "${directory}" == "${CANN_ROOT}" ]]; then
            relative_directory=""
        elif [[ "${CANN_ROOT}" == "/" ]]; then
            relative_directory=${directory#/}
        elif [[ "${directory}" == "${CANN_ROOT}/"* ]]; then
            relative_directory=${directory#"${CANN_ROOT}/"}
        else
            relative_directory=""
        fi
        if [[ -z "${relative_directory}" ]] || \
            ! grep -Fqx -- "${relative_directory}" "${managed_directories}"; then
            if [[ -d "${directory}" ]]; then
                printf '%s\n' "${directory}" >> "${output}"
            fi
        fi
        [[ "${directory}" == "/" ]] && break
        directory=$(dirname -- "${directory}")
    done
}

check_install_for_all()
{
    local package_paths=$1
    local managed_directories=$2
    local directories_to_check="${WORK_DIR}/install-for-all.directories"
    local path
    local directory
    local mode
    local other_mode

    [[ "${INSTALL_FOR_ALL}" == true ]] || return 0
    : > "${directories_to_check}"

    # Check every existing parent of a package path. Directories recorded in
    # managed_directories receive an explicit mode later and do not need this
    # pre-check; their un-managed parents must already be traversable by all users.
    while IFS= read -r path; do
        collect_install_for_all_parent_directories \
            "${path}" "${managed_directories}" "${directories_to_check}"
    done < "${package_paths}"
    while IFS=$'\t' read -r path _; do
        collect_install_for_all_parent_directories \
            "${path}" "${managed_directories}" "${directories_to_check}"
    done < "${PACKAGE_LINKS}"

    while IFS= read -r directory; do
        mode=$(stat -c %a -- "${directory}")
        other_mode=${mode: -1}
        case "${other_mode}" in
            5|7)
                ;;
            *)
                fail "directory ${directory} permission ${mode} does not support --install-for-all"
                ;;
        esac
    done < <(sort -u "${directories_to_check}")
}

acquire_install_lock()
{
    command -v flock >/dev/null 2>&1 || fail "command flock was not found"
    LOCK_FILE="${CANN_ROOT}/ascend.lock"
    check_target_parent "${LOCK_FILE}"
    if [[ -L "${LOCK_FILE}" || (-e "${LOCK_FILE}" && ! -f "${LOCK_FILE}") ]]; then
        fail "install lock path is invalid: ${LOCK_FILE}"
    fi

    exec 9>"${LOCK_FILE}" || fail "failed to open install lock: ${LOCK_FILE}"
    if ! flock -n 9; then
        exec 9>&-
        fail "failed to acquire ${LOCK_FILE}; another process may be installing in this directory"
    fi
    LOCK_HELD=true
}

release_install_lock()
{
    [[ "${LOCK_HELD}" == true ]] || return 0
    rm -f -- "${LOCK_FILE}" || log "WARNING" "failed to remove install lock: ${LOCK_FILE}"
    flock -u 9 || true
    exec 9>&-
    LOCK_HELD=false
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
    [[ -f "${file}" ]] || return 0
    awk -F= -v key="${key}" '$1 == key {sub(/^[^=]*=/, ""); print; exit}' "${file}"
}

comm_log()
{
    log "$@"
}

is_allowed_path()
{
    local path=$1
    case "${path}" in
        asc/include/adv_api/*/*.h|asc/impl/adv_api/detail/*/*.h|\
        asc/include/comm_api/*.h|asc/impl/comm_api/*.h)
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
        asc/include/comm_api|asc/include/comm_api/*|\
        asc/impl/comm_api|asc/impl/comm_api/*)
            ;;
        *)
            return 1
            ;;
    esac
    [[ "${path}" != /* && "${path}" != *"/../"* && "${path}" != *"/.." && \
        "${path}" != ../* && "${path}" != *"/./"* && "${path}" != *"/." && \
        "${path}" != *"//"* && "${path}" != *$'\t'* ]]
}

payload_file_mode()
{
    local path=$1
    local mode

    case "${path}" in
        asc/include/adv_api/*|asc/impl/adv_api/detail/*|\
        asc/include/comm_api/*|asc/impl/comm_api/*)
            mode=550
            ;;
        *)
            fail "cannot determine file permission for managed path: ${path}"
            ;;
    esac
    effective_install_mode "${mode}"
}

managed_directory_mode()
{
    local path=$1
    local mode

    case "${path}" in
        asc/include/adv_api/*|asc/impl/adv_api/detail/*)
            mode=750
            ;;
        asc/include/comm_api|asc/include/comm_api/*|\
        asc/impl/comm_api|asc/impl/comm_api/*)
            mode=550
            ;;
        *)
            fail "cannot determine directory permission for managed path: ${path}"
            ;;
    esac
    effective_install_mode "${mode}"
}

effective_install_mode()
{
    local mode=$1
    local group_mode

    if [[ "${INSTALL_FOR_ALL}" == true ]]; then
        # Match devkit: grant other users the group's read/execute bits, never write access.
        group_mode=${mode:1:1}
        printf '%s%d\n' "${mode:0:2}" "$((group_mode & 5))"
    else
        echo "${mode}"
    fi
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
            asc/include/comm_api/*)
                managed_root="asc/include/comm_api"
                ;;
            asc/impl/comm_api/*)
                managed_root="asc/impl/comm_api"
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

is_allowed_link()
{
    local path=$1
    local link_target=$2

    case "${path}:${link_target}" in
        asc/include/comm_api/aicore/hcomm:../../adv_api/hcomm|\
        asc/impl/comm_api/aicore/hcomm:../../adv_api/detail/hcomm)
            ;;
        *)
            return 1
            ;;
    esac
}

validate_link_manifest()
{
    local manifest=$1
    local path
    local link_target
    local extra
    local count=0

    [[ -f "${manifest}" && ! -L "${manifest}" ]] || \
        fail "link manifest is missing or invalid: ${manifest}"
    while IFS=$'\t' read -r path link_target extra; do
        [[ -z "${extra:-}" ]] || fail "invalid link entry in ${manifest}"
        is_allowed_link "${path}" "${link_target}" || \
            fail "unsafe link entry in ${manifest}: ${path}"
        count=$((count + 1))
    done < "${manifest}"
    [[ ${count} -eq 2 ]] || fail "link manifest must contain both Hcomm links: ${manifest}"
    [[ $(cut -f1 "${manifest}" | sort -u | wc -l) -eq ${count} ]] || \
        fail "duplicate path in ${manifest}"
}

validate_link_destinations()
{
    local manifest=$1
    local path
    local link_target
    local target
    local actual_target

    while IFS=$'\t' read -r path link_target; do
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -L "${target}" ]]; then
            actual_target=$(readlink -- "${target}")
            [[ "${actual_target}" == "${link_target}" ]] || \
                fail "Hcomm link has an unexpected target: ${target} -> ${actual_target}"
        elif [[ -e "${target}" ]]; then
            fail "refusing to replace a non-symbolic Hcomm link path: ${target}"
        fi
    done < "${manifest}"
}

check_managed_links()
{
    local manifest=$1
    local path
    local link_target
    local target

    while IFS=$'\t' read -r path link_target; do
        target="${CANN_ROOT}/${path}"
        [[ -L "${target}" && "$(readlink -- "${target}")" == "${link_target}" ]] || \
            return 1
    done < "${manifest}"
}

record_link_baseline()
{
    local link_manifest=$1
    local output=$2
    local path
    local link_target
    local target

    : > "${output}"
    while IFS=$'\t' read -r path link_target; do
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -L "${target}" ]]; then
            [[ "$(readlink -- "${target}")" == "${link_target}" ]] || \
                fail "Hcomm link has an unexpected target: ${target}"
            printf 'present\t%s\t%s\n' "${path}" "${link_target}" >> "${output}"
        elif [[ -e "${target}" ]]; then
            fail "refusing to replace a non-symbolic Hcomm link path: ${target}"
        else
            printf 'absent\t%s\t%s\n' "${path}" "${link_target}" >> "${output}"
        fi
    done < "${link_manifest}"
}

validate_link_baseline()
{
    local manifest=$1
    local status
    local path
    local link_target
    local extra
    local count=0

    [[ -f "${manifest}" && ! -L "${manifest}" ]] || \
        fail "link baseline is missing or invalid: ${manifest}"
    while IFS=$'\t' read -r status path link_target extra; do
        [[ -z "${extra:-}" ]] || fail "invalid link baseline entry in ${manifest}"
        [[ "${status}" == "present" || "${status}" == "absent" ]] || \
            fail "invalid link baseline state in ${manifest}: ${status}"
        is_allowed_link "${path}" "${link_target}" || \
            fail "unsafe link baseline entry in ${manifest}: ${path}"
        count=$((count + 1))
    done < "${manifest}"
    [[ ${count} -eq 2 ]] || fail "link baseline must contain both Hcomm links: ${manifest}"
}

restore_link_baseline()
{
    local manifest=$1
    local status
    local path
    local link_target
    local target

    while IFS=$'\t' read -r status path link_target; do
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -L "${target}" ]]; then
            rm -f -- "${target}"
        elif [[ -e "${target}" ]]; then
            fail "refusing to replace a non-symbolic Hcomm link path: ${target}"
        fi
        if [[ "${status}" == "present" ]]; then
            mkdir -p "$(dirname -- "${target}")"
            ln -s "${link_target}" "${target}"
        fi
    done < "${manifest}"
}

install_managed_links()
{
    local manifest=$1
    local path
    local link_target
    local target

    while IFS=$'\t' read -r path link_target; do
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -L "${target}" ]]; then
            [[ "$(readlink -- "${target}")" == "${link_target}" ]] || \
                fail "Hcomm link has an unexpected target: ${target}"
        elif [[ -e "${target}" ]]; then
            fail "refusing to replace a non-symbolic Hcomm link path: ${target}"
        else
            mkdir -p "$(dirname -- "${target}")"
            ln -s "${link_target}" "${target}"
        fi
    done < "${manifest}"
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

check_dependency_versions()
{
    local package
    local metadata_key
    local check_result

    for package in asc-devkit hcomm runtime; do
        metadata_key="required_package_${package}_version"
        [[ -n "$(read_info_value "${metadata_key}" "${PACKAGE_INFO}")" ]] || \
            fail "package metadata does not declare the required ${package} version"
    done

    [[ -f "${VERSION_COMPAT_LIB}" && -f "${VERSION_CHECKER}" ]] || \
        fail "package version compatibility scripts are incomplete"
    # shellcheck source=/dev/null
    . "${VERSION_COMPAT_LIB}"

    # The CANN compatibility library expects the non-errexit shell mode used by
    # the official asc-devkit and HCOMM installers.
    set +e
    set +o pipefail
    _check_version_compatiable "asc-comm" "${CANN_ROOT}" "${VERSION_CHECKER_DIR}"
    check_result=$?
    set -o pipefail
    set -e
    [[ ${check_result} -eq 0 ]] || fail "dependency version compatibility check failed"
}

validate_package()
{
    local package_arch
    local current_arch

    [[ -d "${PAYLOAD_DIR}" && -f "${PACKAGE_INFO}" ]] || \
        fail "package payload is incomplete"
    package_arch=$(read_info_value "Arch" "${PACKAGE_INFO}")
    current_arch=$(normalize_arch)
    [[ "${package_arch}" == "${current_arch}" ]] || \
        fail "package architecture ${package_arch} does not match ${current_arch}"

    validate_link_manifest "${PACKAGE_LINKS}"
    check_dependency_versions
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
    STATE_FORMAT_VERSION=$(read_info_value "FormatVersion" "${state_info}")
    case "${STATE_FORMAT_VERSION}" in
        1)
            ;;
        2)
            [[ -f "${directory_manifest}" && ! -L "${directory_manifest}" ]] || \
                fail "directory mode manifest is missing from patch state: ${directory_manifest}"
            ;;
        3)
            [[ -f "${directory_manifest}" && ! -L "${directory_manifest}" ]] || \
                fail "directory mode manifest is missing from patch state: ${directory_manifest}"
            validate_link_manifest "${STATE_DIR}/active.links.tsv"
            validate_link_baseline "${STATE_DIR}/baseline/links.tsv"
            ;;
        *)
            fail "unsupported patch state format: ${STATE_FORMAT_VERSION:-missing}"
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
        if [[ -d "${target}" && ! -L "${target}" ]]; then
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
        chmod "${mode}" "${target}" || return 1
    done < "${manifest}"
}

apply_directory_modes()
{
    local directories_file=$1
    local path
    local target
    local mode

    while IFS= read -r path; do
        is_allowed_directory_path "${path}" || fail "unsafe managed directory path: ${path}"
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -d "${target}" && ! -L "${target}" ]]; then
            mode=$(managed_directory_mode "${path}")
            chmod "${mode}" "${target}"
        fi
    done < "${directories_file}"
}

prepare_directories_for_install()
{
    local directories_file=$1
    local path
    local target

    while IFS= read -r path; do
        is_allowed_directory_path "${path}" || fail "unsafe managed directory path: ${path}"
        target="${CANN_ROOT}/${path}"
        check_target_parent "${target}"
        if [[ -d "${target}" && ! -L "${target}" ]]; then
            chmod 750 "${target}"
        fi
    done < "${directories_file}"
}

prepare_directories_from_mode_manifest()
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
        if [[ -d "${target}" && ! -L "${target}" ]]; then
            chmod 750 "${target}" || return 1
        fi
    done < "${manifest}"
}

build_baseline_directory_manifest()
{
    local directories_file=$1
    local transaction_manifest=$2
    local previous_manifest=$3
    local previous_active_paths=$4
    local output=$5
    local path
    local entry

    : > "${output}"
    while IFS= read -r path; do
        entry=""
        if [[ -f "${previous_manifest}" ]]; then
            entry=$(awk -F '\t' -v path="${path}" '$2 == path {print; exit}' "${previous_manifest}")
        fi
        if [[ -z "${entry}" ]] && \
            ! awk -v prefix="${path}/" 'index($0, prefix) == 1 {found=1} END {exit !found}' \
                "${previous_active_paths}"; then
            entry=$(awk -F '\t' -v path="${path}" '$2 == path {print; exit}' "${transaction_manifest}")
        fi
        [[ -z "${entry}" ]] || printf '%s\n' "${entry}" >> "${output}"
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
    install -m 600 "${directories_file}" "${TRANSACTION_DIR}/managed.directories"
    record_link_baseline "${PACKAGE_LINKS}" "${TRANSACTION_DIR}/links.tsv"

    MUTATION_STARTED=true
}

rollback_transaction()
{
    local status
    local path
    local target

    set +e
    prepare_directories_for_install "${TRANSACTION_DIR}/managed.directories"
    prepare_directories_from_mode_manifest "${TRANSACTION_DIR}/directories.tsv"
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

    if [[ -f "${TRANSACTION_DIR}/links.tsv" ]]; then
        restore_link_baseline "${TRANSACTION_DIR}/links.tsv"
    fi

    remove_new_empty_directories \
        "${TRANSACTION_DIR}/managed.directories" \
        "${TRANSACTION_DIR}/directories.tsv"
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
    local operation_result
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
    if [[ "${ACTION_STARTED}" == true && "${OPERATION_RECORDED}" != true ]]; then
        if [[ ${exit_code} -eq 0 ]]; then
            operation_result="succeeded"
        else
            operation_result="failed"
        fi
        log_operation "${operation_result}" || \
            log "WARNING" "failed to write operation log: ${OPERATION_LOG}"
    fi
    log "INFO" "End time:$(date '+%Y-%m-%d %H:%M:%S')"
    release_install_lock
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
    local source_directory_manifest=$2
    local source_link_manifest=$3
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
    install -m 440 "${source_directory_manifest}" "${directory_manifest}"
    chmod 440 "${directory_manifest}"
    install -m 440 "${source_link_manifest}" "${STATE_DIR}/baseline/links.tsv"
}

install_payload()
{
    local paths_file=$1
    local directories_file=$2
    local path
    local target
    local mode

    prepare_directories_for_install "${directories_file}"
    while IFS= read -r path; do
        [[ -f "${PAYLOAD_DIR}/${path}" && ! -L "${PAYLOAD_DIR}/${path}" ]] || \
            fail "payload file is missing or invalid: ${path}"
        target="${CANN_ROOT}/${path}"
        check_regular_target "${target}"
        mode=$(payload_file_mode "${path}")
        install -D -m "${mode}" "${PAYLOAD_DIR}/${path}" "${target}"
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
    local baseline_directory_manifest
    local baseline_link_manifest
    local directory_manifest="${STATE_DIR}/baseline/directories.tsv"

    validate_package
    WORK_DIR=$(mktemp -d)
    package_paths="${WORK_DIR}/package.paths"
    baseline_paths="${WORK_DIR}/baseline.paths"
    active_paths="${WORK_DIR}/active.paths"
    touched_paths="${WORK_DIR}/touched.paths"
    package_directories="${WORK_DIR}/package.directories"
    touched_directories="${WORK_DIR}/touched.directories"
    baseline_directory_manifest="${WORK_DIR}/baseline.directories.tsv"
    baseline_link_manifest="${WORK_DIR}/baseline.links.tsv"

    collect_checksum_paths "${PACKAGE_MANIFEST}" "${package_paths}"
    collect_managed_directories "${package_paths}" "${package_directories}"
    check_install_for_all "${package_paths}" "${package_directories}"
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
        if [[ "${STATE_FORMAT_VERSION}" == "3" ]]; then
            validate_link_destinations "${STATE_DIR}/active.links.tsv"
            if ! check_managed_links "${STATE_DIR}/active.links.tsv"; then
                if [[ "${FORCE}" == true ]]; then
                    log "WARNING" "installed Hcomm links are missing; --force will recreate them"
                else
                    fail "installed Hcomm links were changed; use --force to recreate missing links"
                fi
            fi
        fi
    fi
    validate_link_destinations "${PACKAGE_LINKS}"

    log "INFO" "Start installing asc-comm development patch."
    log "INFO" "Package version: $(read_info_value PackageVersion "${PACKAGE_INFO}"); source commit: $(read_info_value GitCommit "${PACKAGE_INFO}"); source modified: $(read_info_value GitDirty "${PACKAGE_INFO}")"
    log "INFO" "Target CANN: ${CANN_ROOT}; dependency versions: asc-devkit=$(read_info_value Version "${CANN_ROOT}/share/info/asc-devkit/version.info"), hcomm=$(read_info_value Version "${CANN_ROOT}/share/info/hcomm/version.info"), runtime=$(read_info_value Version "${CANN_ROOT}/share/info/runtime/version.info")"
    log "INFO" "Files to replace: $(wc -l < "${package_paths}"); original files backup: ${STATE_DIR}/baseline"

    sort -u "${package_paths}" "${active_paths}" > "${touched_paths}"
    collect_managed_directories "${touched_paths}" "${touched_directories}"
    validate_target_paths "${touched_paths}"
    snapshot_transaction "${touched_paths}" "${touched_directories}"
    build_baseline_directory_manifest \
        "${package_directories}" \
        "${TRANSACTION_DIR}/directories.tsv" \
        "${directory_manifest}" \
        "${active_paths}" \
        "${baseline_directory_manifest}"
    prepare_directories_for_install "${touched_directories}"
    restore_baseline "${active_paths}"
    if [[ "${STATE_FORMAT_VERSION}" == "3" ]]; then
        restore_link_baseline "${STATE_DIR}/baseline/links.tsv"
    fi
    remove_new_empty_directories "${touched_directories}" "${directory_manifest}"
    restore_directory_modes "${directory_manifest}" || fail "failed to restore original directory permissions"
    rm -rf -- "${STATE_DIR}"
    record_link_baseline "${PACKAGE_LINKS}" "${baseline_link_manifest}"
    record_new_baselines \
        "${package_paths}" \
        "${baseline_directory_manifest}" \
        "${baseline_link_manifest}"
    install_payload "${package_paths}" "${package_directories}"
    prepare_directories_for_install "${package_directories}"
    install_managed_links "${PACKAGE_LINKS}"
    apply_directory_modes "${package_directories}"

    install -m 440 "${PACKAGE_MANIFEST}" "${STATE_DIR}/active.manifest.sha256"
    install -m 440 "${PACKAGE_LINKS}" "${STATE_DIR}/active.links.tsv"
    install -m 440 "${PACKAGE_INFO}" "${STATE_DIR}/version.info"
    printf 'FormatVersion=3\n' > "${WORK_DIR}/state.info"
    install -m 440 "${WORK_DIR}/state.info" "${STATE_DIR}/state.info"
    chmod 750 "${STATE_DIR}" "${STATE_DIR}/baseline"

    rm -rf -- "${WORK_DIR}"
    WORK_DIR=""
    log_operation "succeeded" || fail "failed to write operation log: ${OPERATION_LOG}"
    COMMITTED=true
    log "INFO" "AscComm development patch installed successfully."
    log "INFO" "Recompile the target Ascend C kernel before verification."
}

check_package()
{
    local package_paths

    validate_package
    WORK_DIR=$(mktemp -d)
    package_paths="${WORK_DIR}/package.paths"
    collect_checksum_paths "${PACKAGE_MANIFEST}" "${package_paths}"
    verify_checksums "${PAYLOAD_DIR}" "${PACKAGE_MANIFEST}" || fail "package payload checksum verification failed"
    log "INFO" "Package version: $(read_info_value PackageVersion "${PACKAGE_INFO}"); architecture: $(read_info_value Arch "${PACKAGE_INFO}")"
    log "INFO" "Source commit: $(read_info_value GitCommit "${PACKAGE_INFO}"); source modified: $(read_info_value GitDirty "${PACKAGE_INFO}")"
    log "INFO" "Required package versions: asc-devkit=$(read_info_value required_package_asc-devkit_version "${PACKAGE_INFO}"), hcomm=$(read_info_value required_package_hcomm_version "${PACKAGE_INFO}"), runtime=$(read_info_value required_package_runtime_version "${PACKAGE_INFO}"); files: $(wc -l < "${package_paths}"); links: $(wc -l < "${PACKAGE_LINKS}")"
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
    if [[ "${STATE_FORMAT_VERSION}" == "3" ]]; then
        validate_link_destinations "${STATE_DIR}/active.links.tsv"
        if ! check_managed_links "${STATE_DIR}/active.links.tsv"; then
            if [[ "${FORCE}" == true ]]; then
                log "WARNING" "installed Hcomm links are missing; --force will restore the saved baseline"
            else
                fail "installed Hcomm links were changed; use --force to restore the saved baseline anyway"
            fi
        fi
    fi

    log "INFO" "Start uninstalling asc-comm development patch from ${CANN_ROOT}."
    log "INFO" "Installed package version: $(read_info_value PackageVersion "${STATE_DIR}/version.info"); files to restore or remove: $(wc -l < "${active_paths}")"

    sort -u "${active_paths}" > "${touched_paths}"
    collect_managed_directories "${touched_paths}" "${touched_directories}"
    validate_target_paths "${touched_paths}"
    snapshot_transaction "${touched_paths}" "${touched_directories}"
    prepare_directories_for_install "${touched_directories}"
    restore_baseline "${active_paths}"
    if [[ "${STATE_FORMAT_VERSION}" == "3" ]]; then
        restore_link_baseline "${STATE_DIR}/baseline/links.tsv"
    fi
    remove_new_empty_directories "${touched_directories}" "${directory_manifest}"
    restore_directory_modes "${directory_manifest}" || fail "failed to restore original directory permissions"
    rm -rf -- "${STATE_DIR}"

    rm -rf -- "${WORK_DIR}"
    WORK_DIR=""
    log_operation "succeeded" || fail "failed to write operation log: ${OPERATION_LOG}"
    COMMITTED=true
    log "INFO" "AscComm development patch uninstalled successfully."
}

main()
{
    capture_input_params "$@"
    init_logging
    trap handle_exit EXIT
    start_log
    parse_args "$@"
    set_operation
    if [[ "${ACTION}" == "--check" ]]; then
        resolve_cann_root
        check_package
        return 0
    fi

    resolve_cann_root
    check_owner
    acquire_install_lock
    detect_upgrade_operation
    validate_state_dir

    if [[ "${ACTION}" == "--full" ]]; then
        install_patch
    else
        uninstall_patch
    fi
}

main "$@"
