# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
find_package(ASC REQUIRED)
project(hccl_examples_custom_ops_reducescatter_ccu_builtin LANGUAGES CXX ASC)

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH $ENV{ASCEND_HOME_PATH})
    elseif(DEFINED ENV{ASCEND_OPP_PATH})
        get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
    else()
        message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH or ASCEND_HOME_PATH must be set")
    endif()
endif()

add_executable(demo
    main.asc
)

target_include_directories(demo PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm/ccu
)

target_compile_features(demo PRIVATE cxx_std_14)
target_compile_options(demo PRIVATE
    -Werror
    -Wno-error=deprecated-declarations
    -fstack-protector-strong
    -fPIE
    -O2
)
set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "CMAKE_ASC_ARCHITECTURES, e.g. dav-3510")
target_compile_options(demo PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

target_compile_definitions(demo PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
)
target_link_options(demo PRIVATE
    -pie
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)
target_link_directories(demo PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)
target_link_libraries(demo PRIVATE
    tiling_api
    mc2_client
    register
    platform
    hcomm
    c_sec
    ascendcl
    acl_rt
    ascendc_runtime
)
