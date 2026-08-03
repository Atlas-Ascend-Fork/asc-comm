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

CANN_PATH=${ASCEND_HOME_PATH:-}
OUTPUT_DIR="${PROJECT_ROOT}/build_out"
PACKAGE_VERSION=${ASCCOMM_PACKAGE_VERSION:-1.0.0}
STAGING_DIR="${PROJECT_ROOT}/build/package/asc-comm"
PACKAGE_INPUT_PATHS=(
    build.sh
    include/aicore/hcomm
    src/aicore/hcomm
    scripts/package
)

usage()
{
    cat <<EOF
Usage: bash scripts/package/build_package.sh [OPTION]...

Options:
  --cann-path=<PATH>       CANN installation root. Default: ASCEND_HOME_PATH
  --output-dir=<PATH>      Output directory. Default: ${OUTPUT_DIR}
  --package-version=<VER>  Package version. Default: ${PACKAGE_VERSION}
  -h, --help               Display this help
EOF
}

log()
{
    echo "[asc-comm package] $*"
}

fail()
{
    echo "[asc-comm package] ERROR: $*" >&2
    exit 1
}

parse_args()
{
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --cann-path=*)
                CANN_PATH=${1#*=}
                ;;
            --output-dir=*)
                OUTPUT_DIR=${1#*=}
                ;;
            --package-version=*)
                PACKAGE_VERSION=${1#*=}
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

find_makeself_dir()
{
    local candidate
    local candidates=(
        "${CANN_PATH}/toolkit/tools/op_project_templates/ascendc/customize/cmake/util/makeself"
        "${CANN_PATH}/tools/tikcpp/ascendc_kernel_cmake/fwk_modules/util/makeself"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -x "${candidate}/makeself.sh" && -f "${candidate}/makeself-header.sh" ]]; then
            echo "${candidate}"
            return 0
        fi
    done
    fail "makeself was not found under CANN path: ${CANN_PATH}"
}

read_asc_devkit_version()
{
    local version_file="${CANN_PATH}/share/info/asc-devkit/version.info"
    local version
    [[ -f "${version_file}" ]] || fail "asc-devkit version file not found: ${version_file}"
    version=$(awk -F= '$1 == "Version" {print $2; exit}' "${version_file}")
    [[ -n "${version}" ]] || fail "failed to read asc-devkit version from ${version_file}"
    echo "${version}"
}

copy_headers()
{
    local source_root=$1
    local destination_root=$2
    local source_file
    local relative_path
    local count=0

    while IFS= read -r -d '' source_file; do
        relative_path=${source_file#"${source_root}/"}
        install -D -m 550 "${source_file}" "${destination_root}/${relative_path}"
        count=$((count + 1))
    done < <(find "${source_root}" -type f -name '*.h' -print0 | sort -z)

    [[ ${count} -gt 0 ]] || fail "no header files found under ${source_root}"
}

write_manifest()
{
    local file
    (
        cd "${STAGING_DIR}/payload"
        while IFS= read -r -d '' file; do
            sha256sum "${file}"
        done < <(find asc -type f -name '*.h' -print0 | sort -z)
    ) > "${STAGING_DIR}/manifest.sha256"
}

write_version_info()
{
    local arch=$1
    local required_version=$2
    local commit="unknown"
    local dirty="true"

    if git -C "${PROJECT_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        commit=$(git -C "${PROJECT_ROOT}" rev-parse HEAD)
        if [[ -z "$(git -C "${PROJECT_ROOT}" status --porcelain --untracked-files=all -- \
            "${PACKAGE_INPUT_PATHS[@]}")" ]]; then
            dirty="false"
        fi
    fi

    cat > "${STAGING_DIR}/version.info" <<EOF
PackageName=asc-comm-dev-patch
PackageVersion=${PACKAGE_VERSION}
Arch=${arch}
GitCommit=${commit}
GitDirty=${dirty}
RequiredAscDevkitVersion=${required_version}
BuildTime=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
EOF
}

build_package()
{
    local arch
    local required_version
    local makeself_dir
    local run_file

    [[ -n "${CANN_PATH}" ]] || fail "CANN path is not set; source set_env.sh or use --cann-path"
    [[ "${CANN_PATH}" = /* ]] || fail "CANN path must be absolute: ${CANN_PATH}"
    CANN_PATH=$(readlink -f -- "${CANN_PATH}")
    [[ -d "${CANN_PATH}" ]] || fail "CANN path does not exist: ${CANN_PATH}"
    [[ "${PACKAGE_VERSION}" =~ ^[0-9A-Za-z._+-]+$ ]] || fail "invalid package version: ${PACKAGE_VERSION}"

    arch=$(normalize_arch)
    required_version=$(read_asc_devkit_version)
    makeself_dir=$(find_makeself_dir)

    mkdir -p "$(dirname -- "${STAGING_DIR}")" "${OUTPUT_DIR}"
    rm -rf -- "${STAGING_DIR}"
    mkdir -p "${STAGING_DIR}/payload"

    # Match the Hcomm layout installed by asc-devkit's asc-comm integration.
    copy_headers \
        "${PROJECT_ROOT}/include/aicore/hcomm" \
        "${STAGING_DIR}/payload/asc/include/adv_api/hcomm"
    copy_headers \
        "${PROJECT_ROOT}/src/aicore/hcomm" \
        "${STAGING_DIR}/payload/asc/impl/adv_api/detail/hcomm"

    write_manifest
    write_version_info "${arch}" "${required_version}"
    install -m 550 "${SCRIPT_DIR}/install.sh" "${STAGING_DIR}/install.sh"

    run_file="${OUTPUT_DIR}/Ascend-asccomm_${PACKAGE_VERSION}_linux-${arch}.run"
    rm -f -- "${run_file}"
    "${makeself_dir}/makeself.sh" \
        --header "${makeself_dir}/makeself-header.sh" \
        --help-header "${SCRIPT_DIR}/help.info" \
        --gzip --complevel 4 --nomd5 --sha256 --chown \
        "${STAGING_DIR}" "${run_file}" "asc-comm development patch" ./install.sh

    log "generated ${run_file}"
    log "required asc-devkit version: ${required_version}"
}

parse_args "$@"
build_package
