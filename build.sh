#!/bin/bash

SCRIPT_DIR=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")

SRC_DIR="${SCRIPT_DIR}/src/aicore/mc2"
UT_DIR="${SCRIPT_DIR}/tests/ut"

# 构建目录（源码外构建）
BUILD_DIR="${SCRIPT_DIR}/build"

CUSTOM_OPTION=(
    "-Wno-dev"
    "-DBUILD_OPEN_PROJECT=ON"
    # "-DASCEND_CANN_PACKAGE_PATH=/home/developer/Ascend/cann"
)
CPU_CORES=$(grep -c "^processor" /proc/cpuinfo)
# 默认编译线程数
THREAD_NUM="${CPU_CORES}"

Ascend_CANN_PACKAGE_PATH=""

# 默认构建类型：Release / Debug
BUILD_TYPE="Release"

# 自定义 CMake 参数（可扩展）
CMAKE_EXTRA_ARGS=""

# 分割线，用于日志美化
dotted_line="----------------------------------------"

log() {
    local time_str
    time_str=$(date "+%Y-%m-%d %H:%M:%S")
    echo "[${time_str}] $1"
}

set_env() {
    if [ -z ${ASCEND_HOME_PATH} ]; then
        log "Please set the env of the cann package"
        exit 0
    fi
    log "the path of cann package is ${ASCEND_HOME_PATH}" 
    Ascend_CANN_PACKAGE_PATH=${ASCEND_HOME_PATH}
    CUSTOM_OPTION+=("-DASCEND_CANN_PACKAGE_PATH=${Ascend_CANN_PACKAGE_PATH}")
}

clean_build() {
    log "clean the build dir: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
}

function cmake_config () {
    # local extra_option="$1"
    # log "Info: cmake config ${CUSTOM_OPTION} ${extra_option} ." 
    # echo "cmake -S $1 -B $2 $3"

    local src_dir="$1"
    local build_dir="$2"
    shift 2

    cmake -S "${src_dir}" -B "${build_dir}" "$@"
}

function build () {
    # log "Info: build target: $@ ${JOB_NUM}"
    # cmake --build . --target "$@" -j ${THREAD_NUM} 
    echo "cmake --build $1 -j ${THREAD_NUM}"
    cmake --build $1 -j ${THREAD_NUM}
}

main(){
    clean_build
    set_env
    echo "${CUSTOM_OPTION[@]}"
    local host_build_dir="${BUILD_DIR}/mc2-host"
    local device_build_dir="${BUILD_DIR}/mc2-device"
    local ut_build_dir="${BUILD_DIR}/ut-mc2"
    
    cmake_config ${SRC_DIR} ${host_build_dir} ${CUSTOM_OPTION}
    cmake_config "${SRC_DIR}" "${device_build_dir}" "${CUSTOM_OPTION} -DKERNEL_MODE=ON"
    # cmake_config "${SRC_DIR}/../../../test_cmake" "${BUILD_DIR}/hello" -DKERNEL_MODE=ON -DFLAG=1
    cmake_config "${UT_DIR}" "${ut_build_dir}" "-Wno-dev" "-DASCEND_CANN_PACKAGE_PATH=${Ascend_CANN_PACKAGE_PATH}"

    build ${host_build_dir}
    build ${device_build_dir}
    build ${ut_build_dir}
}

main "$@"


