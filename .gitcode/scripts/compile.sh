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
set -e

LOG_HEAD() {
    local msg=${1}
    local ts
    ts=$(date +%Y%m%d-%H%M%S)
    echo "[INFO] ${ts} ${msg}"
}

LOG_ERROR() {
    local msg=${1}
    local ts
    ts=$(date +%Y%m%d-%H%M%S)
    echo "[ERROR] ${ts} ${msg}"
}

LOG_DO() {
    local cmd="$*"
    local ts
    ts=$(date +%Y%m%d-%H%M%S)
    echo "[Command] ${ts} ${cmd}"
    ${cmd}
}

LOG_INFO() {
    local msg=${1}
    local ts
    ts=$(date +%Y%m%d-%H%M%S)
    echo "[INFO] ${ts} ${msg}"
}

DP_ASSERT_CHECK_SKIP() {
    local actual_value=${1}
    local assert_msg=${2}
    if [ "${actual_value}" != "0" ] && [ "${actual_value}" != "200" ]; then
        LOG_ERROR "${assert_msg} is failed."
        exit 1
    else
        LOG_INFO "${assert_msg} is success."
    fi
}

CHECK_ENV_VAR() {
    local var_name=${1}
    local var_value=${!var_name}
    if [[ -z "${var_value}" ]]; then
        LOG_ERROR "Environment variable ${var_name} is not set"
        exit 1
    fi
}

CHECK_ENV_VAR task_name
CHECK_ENV_VAR WORKSPACE
CHECK_ENV_VAR GIT_TARGET_BRANCH

LOG_HEAD "package_name: ${package_name}"
LOG_HEAD "task_name: ${task_name}"
LOG_HEAD "WORKSPACE: ${WORKSPACE}"
LOG_HEAD "GIT_TARGET_BRANCH: ${GIT_TARGET_BRANCH}"
LOG_HEAD "ge_st_rt2: ${ge_st_rt2}"

ASCEND_3RD_LIB_PATH="/home/jenkins/opensource"

if [[ "${task_name}" == *ubuntu24* ]]; then
    if sudo update-alternatives --set gcc /usr/bin/gcc-16 2>/dev/null; then
        echo "Switched to gcc-16"
    elif sudo update-alternatives --set gcc /usr/bin/gcc-15 2>/dev/null; then
        echo "Switched to gcc-15"
    elif sudo update-alternatives --set gcc /usr/bin/gcc-14 2>/dev/null; then
        echo "gcc-16/15 not available, fell back to gcc-14"
    fi
    export PATH=/opt/buildtools/python-3.10.2/bin:$PATH
else
    if [[ -f "/opt/rh/devtoolset-7/enable" ]]; then
        echo "source devtoolset"
        source /opt/rh/devtoolset-7/enable
    fi
    rm -rf /home/jenkins/opensource/lib_cache
    ln -s /home/jenkins/opensource/ubuntu20/lib_cache /home/jenkins/opensource/lib_cache
fi

if gcc --version | head -n1 | grep -q "15\."; then
    rm -rf /home/jenkins/opensource/lib_cache
    if [ -d /home/jenkins/opensource/gcc15 ]; then
        rm -rf /home/jenkins/opensource/gcc15/lib_cache/abseil-cpp
        rm -rf /home/jenkins/opensource/gcc15/lib_cache/device/abseil-cpp
        ln -s /home/jenkins/opensource/gcc15/lib_cache/ /home/jenkins/opensource/lib_cache
    elif [ -d /home/jenkins/opensource/gcc15x86 ]; then
        rm -rf /home/jenkins/opensource/gcc15x86/lib_cache/abseil-cpp
        rm -rf /home/jenkins/opensource/gcc15x86/lib_cache/device/abseil-cpp
        ln -s /home/jenkins/opensource/gcc15x86/lib_cache/ /home/jenkins/opensource/lib_cache
    fi
elif gcc --version | head -n1 | grep -q "14\."; then
    gcc --version
else
    gcc --version
    rm -rf /home/jenkins/opensource/lib_cache
    ln -s /home/jenkins/opensource/ubuntu20/lib_cache /home/jenkins/opensource/lib_cache
fi
gcc --version
cmake --version

cd "${WORKSPACE}" || exit

set +e
source /home/jenkins/Ascend/cann/bin/setenv.bash
LOG_DO bash build.sh
BUILD_EXIT_CODE=$?
set -e
DP_ASSERT_CHECK_SKIP "${BUILD_EXIT_CODE}" "bash build.sh"
mkdir build_out
touch build_out/cann-asc-comm_linux-x86_64.run
