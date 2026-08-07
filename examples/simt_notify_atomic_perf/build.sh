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

usage() {
    cat <<'EOF'
Usage: build.sh [--ub-staging on|off]

Options:
  --ub-staging on|off  Enable or disable per-lane 128B UB WQE staging (default: on)
EOF
}

ub_staging="ON"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --ub-staging)
            case "${2:-}" in
                on) ub_staging="ON" ;;
                off) ub_staging="OFF" ;;
                *) echo "--ub-staging must be on or off" >&2; exit 1 ;;
            esac
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "ASCEND_HOME_PATH is not set. Source the CANN environment first:" >&2
    echo "  source /usr/local/Ascend/cann/set_env.sh" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build"
build_jobs="${BUILD_JOBS:-$(nproc)}"

echo "SIMT UB WQE staging: ${ub_staging}"
rm -rf -- "${build_dir}"

cmake \
    -S "${script_dir}" \
    -B "${build_dir}" \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510 \
    -DSIMT_NOTIFY_ATOMIC_UB_STAGING="${ub_staging}"

cmake --build "${build_dir}" --verbose --parallel "${build_jobs}"
