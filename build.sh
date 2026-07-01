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

SCRIPT_DIR=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")

UT_DIR="${SCRIPT_DIR}/tests/ut"

# 构建目录（源码外构建）
BUILD_DIR="${SCRIPT_DIR}/build"

CUSTOM_OPTION=(
    "-Wno-dev"
)
CPU_CORES=$(grep -c "^processor" /proc/cpuinfo)
# 默认编译线程数
THREAD_NUM="${CPU_CORES}"

Ascend_CANN_PACKAGE_PATH=""
CANN_3RD_LIB_PATH="${SCRIPT_DIR}/third_party"

# 默认构建类型：Release / Debug
BUILD_TYPE="Release"

# 自定义 CMake 参数（可扩展）
CMAKE_EXTRA_ARGS=""

# 分割线，用于日志美化
dotted_line="----------------------------------------"

TEST=false
COV=false

LOG_LEVEL=0

# log level map
declare -A LOG_LEVEL_MAP=(
    ["DEBUG"]=0
    ["INFO"]=1
    ["ERROR"]=2
)

COLOR_DEBUG="\033[34m"   # 34 = 前景蓝色
COLOR_INFO="\033[32m"    # 32 = 前景绿色
COLOR_ERROR="\033[31m"   # 31 = 前景红色
COLOR_RESET="\033[0m"    # 0 = 重置所有样式（恢复黑白默认）

# 用法：log "DEBUG" "content"
log() {
    local level="$1"
    local time_str
    shift
    local msg="$*"
    local level_num=${LOG_LEVEL_MAP[$level]}

    # 屏蔽低于 LOG_LEVEL 等级的日志
    if [[ $level_num -lt $LOG_LEVEL ]]; then
        return 0
    fi

    time_str=$(date "+%Y-%m-%d %H:%M:%S")

    # 日志内容
    local plain_log="[${time_str}] [${level}] ${msg}"

    local color=""
    case "${level}" in
        DEBUG) color="$COLOR_DEBUG" ;;
        INFO) color="$COLOR_INFO" ;;
        ERROR) color="$COLOR_ERROR" ;;
        *) color="" ;;
    esac
    
    # 带颜色的日志内容
    echo -e "${color}${plain_log}${COLOR_RESET}"
}

usage() {
    echo "build script for asc-comm repository"
    echo "Usage: bash build.sh [OPTION]..."
    echo ""
    echo "The following are all supported arguments:"
    echo "$dotted_line"
    echo "    -h, --help           Display help information"
    echo "    -t, --test           Build and run all unit tests"
    echo "    --cov                Enable code coverage for unit tests"
    echo "    --make_clean         Clean build artifacts"
    echo "    --cann_3rd_lib_path=<PATH>"
    echo "                         Set CANN third_party package install path, Default:./third_party"
    echo "    --build-type=<TYPE>"
    echo "                         Specify build type (TYPE options: Release/Debug), Default:Release"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help)
                usage
                exit 0
                ;;
            -t|--test)
                TEST=true
                shift
                ;;
            --cov)
                COV=true
                shift
                ;;
            --make_clean)
                clean_build
                exit 0
                ;;
            --cann_3rd_lib_path=*)
                CANN_3RD_LIB_PATH="$(realpath "${1#*=}")"
                shift
                ;;
            *)
                log "ERROR" "未知参数：$1"
                usage
                exit 1
                ;;
        esac
    done
    if [[ "${COV}" == true && "${TEST}" != true ]]; then
        log "ERROR" "--cov must be used with -t/--test"
        usage
        exit 1
    fi
}

set_env() {
    if [ -z "${ASCEND_HOME_PATH:-}" ]; then
        log "ERROR" "未配置 CANN 环境，请先source set_env.sh"
        exit 1
    fi
    log "INFO" "the path of cann package is ${ASCEND_HOME_PATH}" 
    Ascend_CANN_PACKAGE_PATH=${ASCEND_HOME_PATH}
    CUSTOM_OPTION+=("-DASCEND_CANN_PACKAGE_PATH=${Ascend_CANN_PACKAGE_PATH}")
    CUSTOM_OPTION+=("-DCANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH}")
}

clean_build() {
    log "INFO" "clean the build dir: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
}

function cmake_config () {
    local src_dir="$1"
    local build_dir="$2"
    shift 2

    cmake -S "${src_dir}" -B "${build_dir}" "$@"
}

function build () { 
    local build_dir="$1"
    shift
    echo "cmake --build ${build_dir} $* -j ${THREAD_NUM}"
    cmake --build "${build_dir}" "$@" -j "${THREAD_NUM}"
}

main(){
    parse_args "$@"
    clean_build
    set_env
    echo "${CUSTOM_OPTION[@]}"
    local ut_build_dir="${BUILD_DIR}/ut-hcomm"

    if [[ "${TEST}" != true ]]; then
        log "INFO" "use -t/--test to build hcomm UT"
        exit 0
    fi

    if [[ "${COV}" == true ]]; then
        CUSTOM_OPTION+=("-DENABLE_GCOV=ON")
    fi
    
    if [[ "${COV}" == true ]]; then
        TARGETS="--target collect_coverage_data"
    else
        TARGETS=""
    fi

    cmake_config "${UT_DIR}" "${ut_build_dir}" "${CUSTOM_OPTION[@]}"
    build "${ut_build_dir}" ${TARGETS}
    if [[ "${COV}" == true ]]; then
        log "INFO" "coverage report generated at ${ut_build_dir}/cov_report/index.html"
    fi
}

main "$@"
