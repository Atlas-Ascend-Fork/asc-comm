# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
project(asccomm_examples_ccu_all_to_all)

if(NOT ASCEND_HOME_PATH)
    set(ASCEND_HOME_PATH $ENV{ASCEND_HOME_PATH})
endif()

if(NOT ASCEND_HOME_PATH)
    message(FATAL_ERROR "ASCEND_HOME_PATH is not set, please source CANN set_env.sh or pass -DASCEND_HOME_PATH=...")
endif()

add_executable(ccu_all_to_all
    main.cc
)

target_include_directories(ccu_all_to_all PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/op_host
    ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_ccu
    ${ASCEND_HOME_PATH}/asc/include/comm_api
    ${ASCEND_HOME_PATH}/pkg_inc/asccomm
    ${ASCEND_HOME_PATH}/include
    ${ASCEND_HOME_PATH}/pkg_inc
)

target_compile_options(ccu_all_to_all PRIVATE
    -U_FORTIFY_SOURCE
    -Werror
    -fno-common
    -fno-strict-aliasing
    -pipe
    -g -O0
    -std=c++14
    -fstack-protector-all
)
target_compile_definitions(ccu_all_to_all PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
    HOST_COMPILE
)

target_link_options(ccu_all_to_all PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--copy-dt-needed-entries
    -Wl,-rpath,${ASCEND_HOME_PATH}/lib64
)
target_link_directories(ccu_all_to_all PRIVATE
    ${ASCEND_HOME_PATH}/lib64
)
target_link_libraries(ccu_all_to_all PRIVATE
    -Wl,--no-as-needed
    hcomm
    asccomm_ccu_dataplane
    -Wl,--as-needed
    ascendcl
    acl_rt
    c_sec
    pthread
)

add_subdirectory(op_host)
add_subdirectory(op_kernel_ccu)
