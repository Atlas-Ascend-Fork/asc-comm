# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
set -e

CURRENT_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]}))
BUILD_DIR=${CURRENT_DIR}/build
BUILD_DEVICE_DIR="${CURRENT_DIR}/build_device"
BUILD_OUTPUT_DIR=${CURRENT_DIR}/build_out
OUTPUT_PATH="${CURRENT_DIR}/output"
USER_ID=$(id -u)
CPU_NUM=$(cat /proc/cpuinfo | grep "^processor" | wc -l)
JOB_NUM="-j${CPU_NUM}"
ASAN="false"
COV="false"
CUSTOM_OPTION="-DCMAKE_INSTALL_PREFIX=${BUILD_OUTPUT_DIR}"
FULL_MODE="false"  # 新增变量，用于控制是否全量构建
KERNEL="false"  # 新增变量，用于控制是否只编译 ccl_kernel.so
DO_NOT_CLEAN="false" # 是否清理
CANN_3RD_LIB_PATH="${CURRENT_DIR}/third_party"
CANN_UTILS_LIB_PATH="${CURRENT_DIR}/utils"
BUILD_AARCH="false"
CUSTOM_SIGN_SCRIPT="${CURRENT_DIR}/scripts/sign/community_sign_build.py"
ENABLE_SIGN="false"
VERSION_INFO="8.5.0"

BUILD_FWK_HLT="false"
MOCK_FWK_HLT="0"

BUILD_CB_TEST="false"

ENABLE_UT="off"
ENABLE_ST="off"
ENABLE_GCOV="off"
CMAKE_BUILD_TYPE="Debug"

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
    if [ -n "${BUILD_DIR}" ];then
        rm -rf ${BUILD_DIR}
    fi

    if [ -z "${TEST}" ] && [ -z "${KERNEL}" ];then
        if [ -n "${BUILD_OUTPUT_DIR}" ];then
            rm -rf ${BUILD_OUTPUT_DIR}
        fi
    fi

    mkdir -p ${BUILD_DIR}
}

function rmdir()
{
    if [ "${DO_NOT_CLEAN}" = "false" ] && [ $# -gt 0 ]; then
        rm -rf "$@"
    fi
}

function cmake_config()
{
    local extra_option="$1"
    log "Info: cmake config ${CUSTOM_OPTION} ${extra_option} ."
    cmake ..  ${CUSTOM_OPTION} ${extra_option}
}

function build()
{
    log "Info: build target:$@ JOB_NUM:${JOB_NUM}"
    cmake --build . --target "$@" ${JOB_NUM} #--verbose
}

function build_package(){
    cmake_config
    log "Info: build_package"
    build package
}

function build_device(){
    cmake_config
    log "Info: build_device"
    TARGET_LIST="hccp_service.bin rs net_co ccl_kernel_plf ccl_kernel_plf_a ccl_kernel aicpu_custom_json aicpu_custom scatter_aicpu_kernel"
    echo "TARGET_LIST=${TARGET_LIST}"
    PKG_TARGET_LIST="generate_device_hccp_package generate_device_aicpu_package"
    echo "PKG_TARGET_LIST=${PKG_TARGET_LIST}"
    SIGN_TARGET_LIST="sign_cann_hcomm_compat sign_aicpu_hcomm sign_aicpu_hccl"
    echo "SIGN_TARGET_LIST=${SIGN_TARGET_LIST}"
    build ${TARGET_LIST} ${PKG_TARGET_LIST} ${SIGN_TARGET_LIST}
}

function build_hccd(){
    cmake_config
    log "Info: build_hccd"
    TARGET_LIST="rs ra_peer ra_hdc ra hccd"
    echo "TARGET_LIST=${TARGET_LIST}"
    PKG_TARGET_LIST="generate_device_hccd_package"
    echo "PKG_TARGET_LIST=${PKG_TARGET_LIST}"
    SIGN_TARGET_LIST="sign_cann_hccd_compat"
    echo "SIGN_TARGET_LIST=${SIGN_TARGET_LIST}"
    build ${TARGET_LIST} ${PKG_TARGET_LIST} ${SIGN_TARGET_LIST}
}

function build_cb_test_verify(){
    cd ${CURRENT_DIR}/examples/
    bash build.sh
}

function build_test() {
    ENABLE_ST="on"
    cmake_config -DENABLE_ST=${ENABLE_ST}

    LIBRARY_DIR="${BUILD_DIR}/test:${ASCEND_HOME_PATH}/lib64:"
    # 每日构建sdk包安装路径
    if [ -d "${ASCEND_HOME_PATH}/opensdk" ];then
        LIBRARY_DIR="${LIBRARY_DIR}${ASCEND_HOME_PATH}/opensdk/opensdk/gtest_shared/lib64:"
    fi

    # 社区sdk包安装路径
    if [ -d "${ASCEND_HOME_PATH}/../../latest/opensdk" ];then
        LIBRARY_DIR="${LIBRARY_DIR}${ASCEND_HOME_PATH}/../../latest/opensdk/opensdk/gtest_shared/lib64:"
    fi

    GCC_MAJOR=`gcc -dumpversion | cut -d. -f1`
    if [ "${ASAN}" == "true" ];then
        ARCH=$(uname -m)
        if [[ $ARCH == "x86_64" || $ARCH == "i386" || $ARCH == "i686" ]]; then
            PRELOAD="/usr/lib/gcc/x86_64-linux-gnu/${GCC_MAJOR}/libasan.so:/usr/lib/gcc/x86_64-linux-gnu/${GCC_MAJOR}/libstdc++.so"
        elif [[ $ARCH == "aarch64" || $ARCH == "armv8l" || $ARCH == "armv7l" ]]; then
            PRELOAD="/usr/lib/gcc/aarch64-linux-gnu/${GCC_MAJOR}/libasan.so:/usr/lib/gcc/aarch64-linux-gnu/${GCC_MAJOR}/libstdc++.so"
        else
            echo "未知架构: $ARCH"
        fi
        echo "PRELOAD is ${PRELOAD}"
        ASAN_OPT="detect_leaks=0"
    fi

    if [ "${TEST_TASK_NAME}" == "open_hccl_test" ] || [ "$TEST" = "all" ];then
        build open_hccl_test
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/st/algorithm/testcase/testcase/open_hccl_test
    fi

    if [ "${TEST_TASK_NAME}" == "executor_hccl_test" ] || [ "$TEST" = "all" ];then
        build executor_hccl_test
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/st/algorithm/testcase/executor_testcase_generalization/executor_hccl_test
    fi

    if [ "${TEST_TASK_NAME}" == "executor_reduce_hccl_test" ] || [ "$TEST" = "all" ];then
        build executor_reduce_hccl_test
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/st/algorithm/testcase/executor_reduce_testcase_generalization/executor_reduce_hccl_test
    fi

    if [ "${TEST_TASK_NAME}" == "executor_pipeline_hccl_test" ] || [ "$TEST" = "all" ];then
        build executor_pipeline_hccl_test
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/st/algorithm/testcase/executor_alltoall_A3_pipeline_testcase/executor_pipeline_hccl_test
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_aicpu_2d_testcase" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_aicpu_2d_testcase
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/aicpu_2d_testcase/legacy_alg_aicpu_2d_testcase
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_ccu_1d_hf16p_testcase" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_ccu_1d_hf16p_testcase
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/ccu_1d_hf16p_testcase/legacy_alg_ccu_1d_hf16p_testcase
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_ccu_1d_testcase_part1" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_ccu_1d_testcase_part1
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/ccu_1d_testcase_part1/legacy_alg_ccu_1d_testcase_part1
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_ccu_1d_testcase_part2" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_ccu_1d_testcase_part2
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/ccu_1d_testcase_part2/legacy_alg_ccu_1d_testcase_part2
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_alg_ccu_reduce" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_ccu_reduce
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/ccu_reduce_testcase/legacy_alg_ccu_reduce
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_function_ut_testcase" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_function_ut_testcase
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/function_ut_testcase/legacy_alg_function_ut_testcase
    fi

    if [ "${TEST_TASK_NAME}" == "legacy_alg_testcase" ] || [ "${TEST_TASK_NAME}" == "legacy_all_testcase" ] || [ "$TEST" = "all" ];then
        build legacy_alg_testcase
        export LD_LIBRARY_PATH=${LIBRARY_DIR}${LD_LIBRARY_PATH} && export LD_PRELOAD=${PRELOAD} && export ASAN_OPTIONS=${ASAN_OPT} \
        && ${BUILD_DIR}/test/legacy/st/algorithm/testcase/legacy_alg_testcase/legacy_alg_testcase
    fi
}

function build_kernel() {
    cmake_config
    log "Info: build_kernel"
    build ccl_kernel_plf ccl_kernel_plf_a ccl_kernel aicpu_custom_json aicpu_custom scatter_aicpu_kernel
}

function mk_dir() {
  local create_dir="$1"  # the target to make
  mkdir -pv "${create_dir}"
  echo "created ${create_dir}"
}

# create build path
function build_ut() {
  echo "create build directory and build";
  mk_dir ${OUTPUT_PATH}
  mk_dir "${BUILD_DIR}"
  local report_dir="${OUTPUT_PATH}/report/ut" && mk_dir "${report_dir}"
  local log_dir="${OUTPUT_PATH}/ut_logs" && mk_dir "${log_dir}"
  cd "${BUILD_DIR}"
  unset LD_LIBRARY_PATH

  local BUILD_JOBS
  if [ "${CPU_NUM}" -ge 8 ]; then
    BUILD_JOBS=$((CPU_NUM-1))
  else
    BUILD_JOBS=8
  fi

  local LLT_KILL_TIME=200
  CMAKE_ARGS="-DPRODUCT_SIDE=host \
              -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
              -DCMAKE_INSTALL_PREFIX=${BUILD_OUTPUT_DIR} \
              -DASCEND_INSTALL_PATH=${ASCEND_INSTALL_PATH} \
              -DCANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH} \
              -DENABLE_GCOV=${ENABLE_GCOV} \
              -DENABLE_TEST=${ENABLE_TEST} \
              -DENABLE_UT=${ENABLE_UT} \
              -DOUTPUT_PATH=${OUTPUT_PATH} \
              -DLLT_KILL_TIME=${LLT_KILL_TIME}"

  echo "CMAKE_ARGS=${CMAKE_ARGS}"
  cmake ${CMAKE_ARGS} ..
  if [ $? -ne 0 ]; then
    echo "execute command: cmake ${CMAKE_ARGS} .. failed."
    return 1
  fi

  echo "Building all test targets..."
  cmake --build . -j${BUILD_JOBS}
  run_ret=${PIPESTATUS[0]}
  echo "exit code: ${run_ret}"
  if [ "${run_ret}" -eq 137 ]
  then
      echo "timeout: execute more than ${LLT_KILL_TIME}s killed"
      exit 1
  fi
  if [ "${run_ret}" -ne 0 ]; then
    echo "execute command: cmake --build . -j${BUILD_JOBS} failed."
    return 1
  fi
  echo "build success!"

  echo "Running tests with CTest (parallel jobs: ${BUILD_JOBS})..."

  local ctest_log="${log_dir}/ctest_output.log"
  local ctest_summary="${log_dir}/ctest_summary.log"

  ctest -j ${BUILD_JOBS} \
        --build-nocmake \
        --timeout ${LLT_KILL_TIME} \
        --output-on-failure \
        --stop-on-failure \
        --test-output-size-failed 10000000 \
        -O "${ctest_log}" \
        2>&1 | tee "${ctest_summary}"
  
  local ctest_ret=${PIPESTATUS[0]}
  
  if [ "${ctest_ret}" -eq 137 ]; then
      echo "timeout: execute more than ${LLT_KILL_TIME}s killed"
      exit 1
  fi
  
  if [ "${ctest_ret}" -ne 0 ]; then
    echo "CTest execution failed. See logs in ${log_dir}"
    return 1
  fi
  
  echo "Build and tests completed successfully!"
  echo "Test logs saved in: ${log_dir}"
}

function make_ut_gov() {
  if [[ "X$ENABLE_UT" = "Xon" && "X$ENABLE_GCOV" = "Xon" ]]; then
    echo "Generated coverage statistics, please wait..."
    cd ${CURRENT_DIR}
    rm -rf ${CURRENT_DIR}/cov
    mkdir -p ${CURRENT_DIR}/cov
    lcov -c -d ${BUILD_DIR}/test/ut/ -d ${BUILD_DIR}/test/legacy/ut/ -o cov/coverage.info
    lcov -r cov/coverage.info */src/platform/hccp/external_depends/* -o cov/coverage.info
    lcov -e cov/coverage.info */src/algorithm/* */src/common/* */src/hccd/* */src/legacy/* */src/framework/* */src/platform/* */src/pub_inc/* -o cov/coverage.info

    cd ${CURRENT_DIR}/cov
    genhtml coverage.info
  fi
}

function run_ut() {
  if [[ "X$ENABLE_UT" = "Xon" ]]; then
    local ut_dir="${BUILD_DIR}/test"
    echo "ut_dir = ${ut_dir}"
    find "$ut_dir" -type f -executable | while read -r ut_exec; do
        filename=$(basename "$ut_exec")
        echo "Executing: $filename"
        ${ut_exec}
    done
  else
    echo "Unit tests is not enabled, sh build.sh with parameter -u or --ut to enable it"
  fi
}

# run_st: shell-based system test runner (from hccl)
function run_st() {
  if [[ "X$ENABLE_ST" = "Xon" ]]; then
    local st_build_shell="${CURRENT_DIR}/test/st/algorithm/build.sh"
    echo "st_build_shell = ${st_build_shell}"
    if [ -e ${st_build_shell} ]; then
      echo "开始执行st..."
      bash ${st_build_shell}
    else
      echo "${st_build_shell} 文件不存在!"
    fi
  else
    echo "System tests is not enabled, sh build.sh with parameter -s or --st to enable it"
  fi
}

# print usage message
function usage() {
  echo "Usage:"
  echo "  sh build.sh --pkg [-h | --help] [-j<N>]"
  echo "              [--cann_3rd_lib_path=<PATH>] [-p|--package-path <PATH>]"
  echo "              [--asan]"
  echo "              [--sign-script <PATH>] [--enable-sign] [--version <VERSION>]"
  echo ""
  echo "Options:"
  echo "    -h, --help     Print usage"
  echo "    --asan         Enable AddressSanitizer"
  echo "    -build-type=<TYPE>"
  echo "                   Specify build type (TYPE options: Release/Debug), Default: Release"
  echo "    -j<N>          Set the number of threads used for building, default is 8"
  echo "    --cann_3rd_lib_path=<PATH>"
  echo "                   Set ascend third_party package install path, default ./output/third_party"
  echo "    -p|--package-path <PATH>"
  echo "                   Set ascend package install path, default /usr/local/Ascend/cann"
  echo "    --sign-script <PATH>"
  echo "                   Set sign-script's path to <PATH>"
  echo "    --enable-sign"
  echo "                   Enable to sign"
  echo "    --version <VERSION>"
  echo "                   Set sign version to <VERSION>"
  echo ""
}

while [[ $# -gt 0 ]]; do
    case "$1" in
      -h | --help)
        usage
        exit 0
        ;;
    -j*)
        JOB_NUM="$1"
        shift
        ;;
    --build-type=*)
        OPTARG=$1
        BUILD_TYPE="${OPTARG#*=}"
        shift
        ;;
    --ccache)
        CCACHE_PROGRAM="$2"
        shift 2
        ;;
    -p|--package-path)
        ascend_package_path="$2"
        shift 2
        ;;
    --nlohmann_path)
        third_party_nlohmann_path="$2"
        shift 2
        ;;
    --pkg)
        # 跳过 --pkg，不做处理
        shift
        ;;
    --cann_3rd_lib_path=*)
        OPTARG=$1
        CANN_3RD_LIB_PATH="$(realpath ${OPTARG#*=})"
        shift
        ;;
    -u|--ut)
        ENABLE_TEST="on"
        ENABLE_UT="on"
        shift
        ;;
    -s|--st)
        ENABLE_TEST="on"
        ENABLE_ST="on"
        TEST="all"
        shift
        ;;
    -t|--test)
        TEST="all"
        shift
        ;;
    --open_hccl_test)
        TEST="partial"
        TEST_TASK_NAME="open_hccl_test"
        shift
        ;;
    --executor_hccl_test)
        TEST="partial"
        TEST_TASK_NAME="executor_hccl_test"
        shift
        ;;
    --executor_reduce_hccl_test)
        TEST="partial"
        TEST_TASK_NAME="executor_reduce_hccl_test"
        shift
        ;;
    --executor_pipeline_hccl_test)
        TEST="partial"
        TEST_TASK_NAME="executor_pipeline_hccl_test"
        shift
        ;;
    --legacy_all_testcase)
        TEST="partial"
        TEST_TASK_NAME="legacy_all_testcase"
        shift
        ;;
    --legacy_aicpu_2d_testcase)
        TEST="partial"
        TEST_TASK_NAME="legacy_aicpu_2d_testcase"
        shift
        ;;
    --legacy_ccu_1d_hf16p_testcase)
        TEST="partial"
        TEST_TASK_NAME="legacy_ccu_1d_hf16p_testcase"
        shift
        ;;
    --legacy_ccu_1d_testcase_part1)
        TEST="partial"
        TEST_TASK_NAME="legacy_ccu_1d_testcase_part1"
        shift
        ;;
    --legacy_ccu_1d_testcase_part2)
        TEST="partial"
        TEST_TASK_NAME="legacy_ccu_1d_testcase_part2"
        shift
        ;;
    --legacy_alg_ccu_reduce)
        TEST="partial"
        TEST_TASK_NAME="legacy_alg_ccu_reduce"
        shift
        ;;
    --legacy_function_ut_testcase)
        TEST="partial"
        TEST_TASK_NAME="legacy_function_ut_testcase"
        shift
        ;;
    --legacy_alg_testcase)
        TEST="partial"
        TEST_TASK_NAME="legacy_alg_testcase"
        shift
        ;;
    --aicpu)  # 新增选项，用于只编译 ccl_kernel.so
        KERNEL="true"
        shift
        ;;
    --full)
        FULL_MODE="true"
        shift
        ;;
    --build_aarch)
        BUILD_AARCH="true"
        shift
        ;;
    --asan)
        ASAN="true"
        shift
        ;;
    --cov)
        ENABLE_GCOV="on"
        COV="true"
        shift
        ;;
    --noclean)
        DO_NOT_CLEAN="true"
        shift
        ;;
    --fwk_test_hlt)
        BUILD_FWK_HLT="true"
        MOCK_FWK_HLT="0"
        shift
        ;;
    --fwk_test_hlt_mock)
        BUILD_FWK_HLT="true"
        MOCK_FWK_HLT="1"
        shift
        ;;
    --cb_test_verify)
        BUILD_CB_TEST="true"
        shift
        ;;
    --enable-sign)
        ENABLE_SIGN="true"
        shift
        ;;
    --sign-script)
        CUSTOM_SIGN_SCRIPT="$(realpath $2)"
        shift 2
        ;;
    --version)
        VERSION_INFO="$2"
        shift 2
        ;;
    *)
        log "Error: Undefined option: $1"
        usage
        exit 1
        ;;
    esac
done

if [ -n "${TEST}" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DENABLE_TEST=ON"
fi

if [ "${KERNEL}" == "true" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DKERNEL_MODE=ON -DDEVICE_MODE=ON -DPRODUCT=ascend -DPRODUCT_SIDE=device"
fi

if [ "${FULL_MODE}" == "true" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DFULL_MODE=ON"
else
    CUSTOM_OPTION="${CUSTOM_OPTION} -DFULL_MODE=OFF"
fi

if [ "${BUILD_AARCH}" == "true" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DAARCH_MODE=ON"
else
    CUSTOM_OPTION="${CUSTOM_OPTION} -DAARCH_MODE=OFF"
fi

if [ "${ASAN}" == "true" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DENABLE_ASAN=ON"
fi

if [ "${COV}" == "true" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DENABLE_GCOV=ON"
fi

CUSTOM_OPTION="${CUSTOM_OPTION} -DASCENDC_DUMP=0" # AIV算子调测阶段可配置为1以打开AscendC打印功能

if [ -n "${ascend_package_path}" ];then
    ASCEND_CANN_PACKAGE_PATH=${ascend_package_path}
elif [ -n "${ASCEND_HOME_PATH}" ];then
    ASCEND_CANN_PACKAGE_PATH=${ASCEND_HOME_PATH}
elif [ -n "${ASCEND_OPP_PATH}" ];then
    ASCEND_CANN_PACKAGE_PATH=$(dirname ${ASCEND_OPP_PATH})
elif [ -d "${DEFAULT_TOOLKIT_INSTALL_DIR}" ];then
    ASCEND_CANN_PACKAGE_PATH=${DEFAULT_TOOLKIT_INSTALL_DIR}
elif [ -d "${DEFAULT_INSTALL_DIR}" ];then
    ASCEND_CANN_PACKAGE_PATH=${DEFAULT_INSTALL_DIR}
else
    log "Error: Please set the toolkit package installation directory through parameter -p|--package-path."
    exit 1
fi

if [ -n "${third_party_nlohmann_path}" ];then
    CUSTOM_OPTION="${CUSTOM_OPTION} -DTHIRD_PARTY_NLOHMANN_PATH=${third_party_nlohmann_path}"
fi

CUSTOM_OPTION="${CUSTOM_OPTION} -DCUSTOM_ASCEND_CANN_PACKAGE_PATH=${ASCEND_CANN_PACKAGE_PATH}"
# CUSTOM_OPTION="${CUSTOM_OPTION} -DCANN_3RD_LIB_PATH=${cann_3rd_lib_path}"
CUSTOM_OPTION="$CUSTOM_OPTION -DCANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH} -DCANN_UTILS_LIB_PATH=${CANN_UTILS_LIB_PATH}"

CUSTOM_OPTION="$CUSTOM_OPTION -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"

set_env

if [ "${DO_NOT_CLEAN}" = "false" ]; then
    clean
else
    mkdir -p "${BUILD_DIR}"
fi

cd ${BUILD_DIR}

if [ "${ENABLE_UT}" == "on" ]; then
    build_ut
    make_ut_gov
elif [ "${ENABLE_ST}" == "on" ]; then
    run_st
elif [ -n "${TEST}" ];then
    build_test
elif [ "${KERNEL}" == "true" ]; then
    build_kernel
elif [ "${BUILD_FWK_HLT}" == "true" ]; then
    log "Info: Building fwk_test with MOCK_HCCL=${MOCK_FWK_HLT}"
    cmake ${CUSTOM_OPTION} -DMOCK_HCCL=${MOCK_FWK_HLT} ../test/hlt
    build hcomm_test
    log "Info: fwk_test execution example: ${BUILD_DIR}/hcomm_test --cluster_info test/hlt/ranktable.json --rank 0 --list"
    log "Info: fwk_test execution example: ${BUILD_DIR}/hcomm_test --cluster_info test/hlt/ranktable.json --rank 0 --test allocthread"
elif [ "${BUILD_CB_TEST}" == "true" ]; then
    log "Info: Building cb_test_verify"
    build_cb_test_verify
    if grep -q "Make Failure" ${BUILD_DIR}/build.log || grep -q "Make test Failure" ${BUILD_DIR}/build.log; then
        log "Info: Building cb_test_verify failed"
        exit 1
    else
        log "Info: Building cb_test_verify success"
    fi
elif [ "${FULL_MODE}" == "true" ]; then
    cd ..
    mkdir -p ${BUILD_DEVICE_DIR}
    cd ${BUILD_DEVICE_DIR}
    CURRENT_CUSTOM_OPTION="${CUSTOM_OPTION}"
    CUSTOM_OPTION="${CURRENT_CUSTOM_OPTION} -DFULL_MODE=ON -DDEVICE_MODE=ON -DKERNEL_MODE=ON -DPRODUCT=ascend910B -DPRODUCT_SIDE=device -DUSE_ALOG=0 -DCUSTOM_SIGN_SCRIPT=${CUSTOM_SIGN_SCRIPT} -DENABLE_SIGN=${ENABLE_SIGN} -DVERSION_INFO=${VERSION_INFO}"
    build_device
    BUILD_HCCD_DIR="${CURRENT_DIR}/build_hccd"
    mkdir -p ${BUILD_HCCD_DIR}
    cd ${BUILD_HCCD_DIR}
    CUSTOM_OPTION="${CURRENT_CUSTOM_OPTION} -DDEVICE_MODE=ON -DPRODUCT=ascend -DPRODUCT_SIDE=device -DUSE_ALOG=1 -DCUSTOM_SIGN_SCRIPT=${CUSTOM_SIGN_SCRIPT} -DENABLE_SIGN=${ENABLE_SIGN} -DVERSION_INFO=${VERSION_INFO}"
    build_hccd
    cd .. & cd ${BUILD_DIR}
    CUSTOM_OPTION="${CURRENT_CUSTOM_OPTION} -DDEVICE_MODE=OFF -DPRODUCT=ascend -DPRODUCT_SIDE=host -DUSE_ALOG=1"
    build_package
    rmdir ${BUILD_DEVICE_DIR} ${BUILD_HCCD_DIR}
else
    cd ..
    mkdir -p ${BUILD_DEVICE_DIR}
    cd ${BUILD_DEVICE_DIR}
    CURRENT_CUSTOM_OPTION="${CUSTOM_OPTION}"
    CUSTOM_OPTION="${CURRENT_CUSTOM_OPTION} -DDEVICE_MODE=ON -DKERNEL_MODE=ON -DPRODUCT=ascend -DPRODUCT_SIDE=device -DUSE_ALOG=0 -DCUSTOM_SIGN_SCRIPT=${CUSTOM_SIGN_SCRIPT} -DENABLE_SIGN=${ENABLE_SIGN} -DVERSION_INFO=${VERSION_INFO}"
    build_kernel
    cd .. & cd ${BUILD_DIR}
    CUSTOM_OPTION="${CURRENT_CUSTOM_OPTION} -DDEVICE_MODE=OFF -DPRODUCT=ascend -DPRODUCT_SIDE=host -DUSE_ALOG=1"
    build_package
    rmdir ${BUILD_DEVICE_DIR}
fi
