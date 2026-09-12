# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
find_package(ASC REQUIRED)
project(asccomm_ccu_direct_decoupled_example LANGUAGES CXX ASC)

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    else()
        message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH or ASCEND_HOME_PATH must be set")
    endif()
endif()

get_filename_component(ASCCOMM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ASCCOMM_LIBRARY_DIR "" CACHE PATH "Directory containing libasccomm_ccu_dataplane.so")

add_executable(ccu_direct_decoupled_example main.asc)
target_include_directories(ccu_direct_decoupled_example PRIVATE
    ${ASCCOMM_SOURCE_DIR}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm/ccu
)
target_compile_features(ccu_direct_decoupled_example PRIVATE cxx_std_17)
target_compile_options(ccu_direct_decoupled_example PRIVATE
    -Werror
    -fstack-protector-strong
    -fPIE
    -O2
    $<$<COMPILE_LANGUAGE:ASC>:--cce-host-only>
    $<$<COMPILE_LANGUAGE:ASC>:-fcce-kernel-launch-custom>
    $<$<COMPILE_LANGUAGE:ASC>:-Wno-error=unused-command-line-argument>
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)
target_compile_definitions(ccu_direct_decoupled_example PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

target_link_options(ccu_direct_decoupled_example PRIVATE
    -pie
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)

if(ASCCOMM_LIBRARY_DIR)
    target_link_directories(ccu_direct_decoupled_example PRIVATE ${ASCCOMM_LIBRARY_DIR})
endif()
target_link_directories(ccu_direct_decoupled_example PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/lib64)
target_link_libraries(ccu_direct_decoupled_example PRIVATE
    asccomm_ccu_dataplane
    hcomm
    ascendcl
    acl_rt
    c_sec
    ascendc_runtime
    pthread
)
