# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms of conditions of
# CANN Open Software License Agreement Version 2.0 ("License").
# Please refer to the License for details.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
project(asccomm_examples_ccu_transfer)

if(NOT ASCEND_HOME_PATH)
    set(ASCEND_HOME_PATH $ENV{ASCEND_HOME_PATH})
endif()

if(NOT ASCEND_HOME_PATH)
    message(FATAL_ERROR "ASCEND_HOME_PATH is not set, please source CANN set_env.sh or pass -DASCEND_HOME_PATH=...")
endif()

if(NOT ASCEND_CANN_PACKAGE_PATH)
    set(ASCEND_CANN_PACKAGE_PATH "${ASCEND_HOME_PATH}")
endif()

set(SCENARIO_NUM "1" CACHE STRING "Transfer scenario: 1 local copy, 2 remote write, 3 remote write reduce")
if(NOT SCENARIO_NUM MATCHES "^[1-3]$")
    message(FATAL_ERROR "SCENARIO_NUM must be 1, 2, or 3")
endif()

add_executable(ccu_transfer
    main.cc
)

target_include_directories(ccu_transfer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/op_host
    ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_ccu
    ${ASCEND_HOME_PATH}/aarch64-linux/include
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/include
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/pkg_inc
)

target_compile_options(ccu_transfer PRIVATE
    -U_FORTIFY_SOURCE
    -Werror
    -fno-common
    -fno-strict-aliasing
    -pipe
    -g
    -O0
    -std=c++14
    -fstack-protector-all
)

target_compile_definitions(ccu_transfer PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
    HOST_COMPILE
    SCENARIO_NUM=${SCENARIO_NUM}
)

target_link_options(ccu_transfer PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--copy-dt-needed-entries
    -Wl,-rpath,${ASCEND_HOME_PATH}/aarch64-linux/lib64:${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/lib64
)

target_link_directories(ccu_transfer PRIVATE
    ${ASCEND_HOME_PATH}/aarch64-linux/lib64
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/lib64
)

target_link_libraries(ccu_transfer PRIVATE
    -Wl,--no-as-needed
    hcomm
    ${ASCEND_HOME_PATH}/aarch64-linux/lib64/libasccomm_ccu_dataplane.so
    -Wl,--as-needed
    ascendcl
    acl_rt
    c_sec
    pthread
)

add_subdirectory(op_host)
add_subdirectory(op_kernel_ccu)
