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

CURRENT_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]}))
BUILD_DIR=${CURRENT_DIR}/build
OUTPUT_DIR=${CURRENT_DIR}/build_out
BUILD_DEVICE_DIR=${CURRENT_DIR}/build_device
CPU_NUM=$(($(cat /proc/cpuinfo | grep "^processor" | wc -l)*2))
JOB_NUM="-j${CPU_NUM}"
CANN_3RD_LIB_PATH="${CURRENT_DIR}/third_party"
CANN_CMAKE_SOURCE_DIR="${CANN_CMAKE_SOURCE_DIR:-}"
CMAKE_BUILD_TYPE="Release"
ENABLE_SIGN="${ENABLE_SIGN:-false}"
CUSTOM_SIGN_SCRIPT="${CUSTOM_SIGN_SCRIPT:-}"

# 自定义算子工程参数
CUSTOM_OPS_NAME=""
CUSTOM_OPS_PATH=""
CUSTOM_OPS_VENDOR=""

if [ "${USER_ID}" != "0" ]; then
    DEFAULT_TOOLKIT_INSTALL_DIR="${HOME}/Ascend/ascend-toolkit/latest"
    DEFAULT_INSTALL_DIR="${HOME}/Ascend/latest"
else
    DEFAULT_TOOLKIT_INSTALL_DIR="/usr/local/Ascend/ascend-toolkit/latest"
    DEFAULT_INSTALL_DIR="/usr/local/Ascend/latest"
fi

function log() {
    local current_time=`date +"%Y-%m-%d %H:%M:%S"`
    echo "[$current_time] "$1
}

function set_env()
{
    source $ASCEND_CANN_PACKAGE_PATH/set_env.sh || echo "0"
}

function clean()
{
    if [ -n "${BUILD_DIR}" ]; then
        rm -rf ${BUILD_DIR}
    fi
    if [ -n "${BUILD_DEVICE_DIR}" ]; then
        rm -rf ${BUILD_DEVICE_DIR}
    fi
    if [ -n "${OUTPUT_DIR}" ]; then
        rm -rf ${OUTPUT_DIR}
    fi
    mkdir -p ${BUILD_DIR} ${BUILD_DEVICE_DIR} ${OUTPUT_DIR}
}

function cmake_config()
{
    local src_dir="$1"
    local build_dir="$2"
    shift 2
    log "Info: cmake -S ${src_dir} -B ${build_dir} $*"
    cmake -S "${src_dir}" -B "${build_dir}" "$@"
}

function build()
{
    local build_dir="$1"
    shift
    log "Info: build target:$@ JOB_NUM:${JOB_NUM}"
    cmake --build "${build_dir}" --target "$@" ${JOB_NUM}
}

function build_custom()
{
    if [ -z "${CUSTOM_OPS_NAME}" ] || [ -z "${CUSTOM_OPS_PATH}" ] || [ -z "${CUSTOM_OPS_VENDOR}" ]; then
        log "Error: --ops, --custom_ops_path and --vendor must be set for custom ops build."
        usage
        exit 1
    fi

    # 编译 Device (AICPU Kernel) 包
    log "Info: build_custom_device"
    clean
    cmake_config "${CURRENT_DIR}" "${BUILD_DEVICE_DIR}" \
        "-DKERNEL_MODE=ON" \
        "-DENABLE_CUSTOM=ON" \
        "-DCUSTOM_OPS_PATH=${CUSTOM_OPS_PATH}" \
        "-DCUSTOM_OPS_NAME=${CUSTOM_OPS_NAME}" \
        "-DCUSTOM_OPS_VENDOR=${CUSTOM_OPS_VENDOR}" \
        ${COMMON_OPTIONS}
    # 编译 AICPU Kernel 包
    build "${BUILD_DEVICE_DIR}" custom_aicpu

    # 编译 Host 包
    log "Info: build_custom_host"
    cmake_config "${CURRENT_DIR}" "${BUILD_DIR}" \
        "-DENABLE_CUSTOM=ON" \
        "-DCUSTOM_OPS_PATH=${CUSTOM_OPS_PATH}" \
        "-DCUSTOM_OPS_NAME=${CUSTOM_OPS_NAME}" \
        "-DCUSTOM_OPS_VENDOR=${CUSTOM_OPS_VENDOR}" \
        ${COMMON_OPTIONS}
    # 打包 run 包
    build "${BUILD_DIR}" package
}

function usage()
{
    echo "build script for AICPU custom ops examples"
    echo "Usage: bash build.sh [OPTION]..."
    echo ""
    echo "The following are all supported arguments:"
    echo "=========================================="
    echo "    -h, --help                 Display help information"
    echo "    -p=<PATH>|--package-path=<PATH>  Set the CANN toolkit installation path"
    echo "    --ops=<OPS>                Set the custom ops name (e.g. allgather_aicpu)"
    echo "    --custom_ops_path=<PATH>   Set custom ops project path (e.g. ./custom_ops/allgather)"
    echo "    --vendor=<VENDOR>          Set the custom ops vendor (e.g. cust)"
    echo "    --cann-cmake-path=<PATH>   Use a local cann-cmake checkout (for offline builds)"
    echo "    --enable-sign              Enable AICPU package signing"
    echo "    --sign-script=<PATH>       Use a custom signing script"
    echo "    --clean                    Clean build artifacts"
}

# 解析参数
while [ $# -gt 0 ]; do
    case "$1" in
    -h|--help)
        usage
        exit 0
        ;;
    --clean)
        clean
        exit 0
        ;;
    -p=*|--package-path=*)
        ascend_package_path="${1#*=}"
        shift
        ;;
    --ops=*)
        CUSTOM_OPS_NAME="${1#*=}"
        shift
        ;;
    --custom_ops_path=*)
        CUSTOM_OPS_PATH="$(realpath ${1#*=})"
        shift
        ;;
    --vendor=*)
        CUSTOM_OPS_VENDOR="${1#*=}"
        shift
        ;;
    --cann-cmake-path=*)
        CANN_CMAKE_SOURCE_DIR="$(realpath ${1#*=})"
        shift
        ;;
    --enable-sign)
        ENABLE_SIGN="true"
        shift
        ;;
    --sign-script=*)
        CUSTOM_SIGN_SCRIPT="$(realpath ${1#*=})"
        shift
        ;;
    *)
        log "Error: Undefined option $1"
        usage
        exit 1
        ;;
    esac
done

# 解析 CANN 安装路径
if [ -n "${ascend_package_path}" ]; then
    ASCEND_CANN_PACKAGE_PATH=${ascend_package_path}
elif [ -n "${ASCEND_HOME_PATH}" ]; then
    ASCEND_CANN_PACKAGE_PATH=${ASCEND_HOME_PATH}
elif [ -n "${ASCEND_OPP_PATH}" ]; then
    ASCEND_CANN_PACKAGE_PATH=$(dirname ${ASCEND_OPP_PATH})
elif [ -d "${DEFAULT_TOOLKIT_INSTALL_DIR}" ]; then
    ASCEND_CANN_PACKAGE_PATH=${DEFAULT_TOOLKIT_INSTALL_DIR}
elif [ -d "${DEFAULT_INSTALL_DIR}" ]; then
    ASCEND_CANN_PACKAGE_PATH=${DEFAULT_INSTALL_DIR}
else
    log "Error: Please set the toolkit package installation directory through -p=<PATH>|--package-path=<PATH>."
    exit 1
fi

COMMON_OPTIONS="-DCMAKE_INSTALL_PREFIX=${OUTPUT_DIR}"
COMMON_OPTIONS="${COMMON_OPTIONS} -DASCEND_CANN_PACKAGE_PATH=${ASCEND_CANN_PACKAGE_PATH}"
COMMON_OPTIONS="${COMMON_OPTIONS} -DCANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH}"
if [ -n "${CANN_CMAKE_SOURCE_DIR}" ]; then
    COMMON_OPTIONS="${COMMON_OPTIONS} -DCANN_CMAKE_SOURCE_DIR=${CANN_CMAKE_SOURCE_DIR}"
fi
COMMON_OPTIONS="${COMMON_OPTIONS} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
COMMON_OPTIONS="${COMMON_OPTIONS} -DENABLE_SIGN=${ENABLE_SIGN}"
COMMON_OPTIONS="${COMMON_OPTIONS} -DCUSTOM_SIGN_SCRIPT=${CUSTOM_SIGN_SCRIPT}"

set_env

build_custom

log "Info: Build finished. Output package is in ${OUTPUT_DIR}"
