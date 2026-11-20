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

# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Shared path selection for builds and benchmark launchers. Sourcing this file
# only defines functions; it never sources CANN, builds, or installs packages.

_hccl_test_directory_path()
{
    if [[ -d "$1" ]]; then
        (cd -- "$1" && pwd -P)
    else
        printf '%s\n' "${1%/}"
    fi
}

hccl_test_cann_root()
{
    local root="${ASCEND_HOME_PATH:-${ASCEND_CANN_PACKAGE_PATH:-${1:-}}}"
    [[ -n "${root}" ]] || return 1
    _hccl_test_directory_path "${root}"
}

hccl_test_use_cann_environment()
{
    local root
    root="$(hccl_test_cann_root "${1:-}")" || return 1
    if [[ -n "${ASCEND_HOME_PATH:-}" && -n "${ASCEND_CANN_PACKAGE_PATH:-}" ]]; then
        local home_path package_path
        home_path="$(_hccl_test_directory_path "${ASCEND_HOME_PATH}")"
        package_path="$(_hccl_test_directory_path "${ASCEND_CANN_PACKAGE_PATH}")"
        if [[ "${home_path}" != "${package_path}" ]]; then
            printf '[WARNING] ASCEND_HOME_PATH=%s differs from ASCEND_CANN_PACKAGE_PATH=%s; using ASCEND_HOME_PATH for build and run.\n' \
                "${ASCEND_HOME_PATH}" "${ASCEND_CANN_PACKAGE_PATH}" >&2
        fi
    fi
    export ASCEND_HOME_PATH="${root}"
    export ASCEND_CANN_PACKAGE_PATH="${root}"
}

hccl_test_mpi_root()
{
    local root="${MPI_HOME:-${MPI_PATH:-}}"
    if [[ -n "${root}" ]]; then
        # Do not silently replace an explicitly configured installation.
        _hccl_test_directory_path "${root}"
        return
    fi
    for root in /usr/local/mpich /usr/local/mpi/mpich; do
        if [[ -x "${root}/bin/mpirun" ]]; then
            _hccl_test_directory_path "${root}"
            return
        fi
    done
    printf '%s\n' /usr/local/mpich
}
