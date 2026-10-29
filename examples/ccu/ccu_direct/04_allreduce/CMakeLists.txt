# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
find_package(ASC REQUIRED)
project(asccomm_examples_ccu_allreduce LANGUAGES CXX ASC)

set(SCENARIO_NUM "1" CACHE STRING "AllReduce scenario: 1 allreduce sum, 2 allreduce sum plus local_reduce demo")
if(NOT SCENARIO_NUM MATCHES "^[1-2]$")
    message(FATAL_ERROR "SCENARIO_NUM must be 1 or 2")
endif()

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH $ENV{ASCEND_HOME_PATH})
    elseif(DEFINED ENV{ASCEND_OPP_PATH})
        get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
    else()
        message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH or ASCEND_HOME_PATH must be set")
    endif()
endif()

add_executable(ccu_allreduce
    main.asc
)

target_include_directories(ccu_allreduce PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
)

target_compile_features(ccu_allreduce PRIVATE cxx_std_17)
target_compile_options(ccu_allreduce PRIVATE
    -Werror
    -fstack-protector-strong
    -fPIE
    -O2
)
set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "CMAKE_ASC_ARCHITECTURES, e.g. dav-3510")
target_compile_options(ccu_allreduce PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

target_compile_definitions(ccu_allreduce PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
    SCENARIO_NUM=${SCENARIO_NUM}
)
target_link_options(ccu_allreduce PRIVATE
    -pie
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)
target_link_directories(ccu_allreduce PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)
target_link_libraries(ccu_allreduce PRIVATE
    -Wl,--no-as-needed
    hcomm
    asccomm_ccu
    hccl_v2
    -Wl,--as-needed
    c_sec
    ascendcl
    acl_rt
    ascendc_runtime
    pthread
)
