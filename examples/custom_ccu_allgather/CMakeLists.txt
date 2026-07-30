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
project(asccomm_examples_custom_ccu_allgather)

if(NOT ASCEND_HOME_PATH)
    set(ASCEND_HOME_PATH $ENV{ASCEND_HOME_PATH})
endif()

if(NOT ASCEND_HOME_PATH)
    message(FATAL_ERROR "ASCEND_HOME_PATH is not set, please source CANN set_env.sh or pass -DASCEND_HOME_PATH=...")
endif()

if(NOT HCOMM_HOME_PATH)
    set(HCOMM_HOME_PATH $ENV{HCOMM_HOME_PATH})
endif()
if(NOT HCOMM_HOME_PATH)
    set(HCOMM_HOME_PATH ${ASCEND_HOME_PATH})
endif()

set(_HCOMM_PACKAGE_PATH "")
foreach(_candidate
    "${HCOMM_HOME_PATH}/aarch64-linux"
    "${HCOMM_HOME_PATH}/x86_64-linux"
    "${HCOMM_HOME_PATH}")
    if(EXISTS "${_candidate}/include" AND EXISTS "${_candidate}/pkg_inc" AND EXISTS "${_candidate}/lib64")
        set(_HCOMM_PACKAGE_PATH "${_candidate}")
        break()
    endif()
endforeach()
if(NOT _HCOMM_PACKAGE_PATH)
    message(FATAL_ERROR "HCOMM_HOME_PATH does not contain an hcomm package: ${HCOMM_HOME_PATH}")
endif()

find_library(HCOMM_LIBRARY NAMES hcomm PATHS "${_HCOMM_PACKAGE_PATH}/lib64" NO_DEFAULT_PATH)
find_library(ASCCOMM_CCU_DATAPLANE_LIBRARY NAMES asccomm_ccu_dataplane
    PATHS "${_HCOMM_PACKAGE_PATH}/lib64" NO_DEFAULT_PATH)
if(NOT HCOMM_LIBRARY OR NOT ASCCOMM_CCU_DATAPLANE_LIBRARY)
    message(FATAL_ERROR "libhcomm.so and libasccomm_ccu_dataplane.so must both exist under ${_HCOMM_PACKAGE_PATH}/lib64")
endif()

add_executable(custom_allgather_ccu
    testcase/main.cc
)

target_include_directories(custom_allgather_ccu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/op_host
    ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_ccu
    ${ASCEND_HOME_PATH}/include
    ${ASCEND_HOME_PATH}/pkg_inc
    ${_HCOMM_PACKAGE_PATH}/include
    ${_HCOMM_PACKAGE_PATH}/include/ccu/hcomm
    ${_HCOMM_PACKAGE_PATH}/pkg_inc
)

target_compile_options(custom_allgather_ccu PRIVATE
    -U_FORTIFY_SOURCE
    -Werror
    -fno-common
    -fno-strict-aliasing
    -pipe
    -g -O0
    -std=c++14
    -fstack-protector-all
)
target_compile_definitions(custom_allgather_ccu PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
    HOST_COMPILE
)

target_link_options(custom_allgather_ccu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--copy-dt-needed-entries
    -Wl,-rpath,${ASCEND_HOME_PATH}/lib64
    -Wl,-rpath,${_HCOMM_PACKAGE_PATH}/lib64
    -Wl,-rpath-link,${_HCOMM_PACKAGE_PATH}/lib64
)
target_link_directories(custom_allgather_ccu PRIVATE
    ${ASCEND_HOME_PATH}/lib64
)
target_link_libraries(custom_allgather_ccu PRIVATE
    -Wl,--no-as-needed
    ${HCOMM_LIBRARY}
    ${ASCCOMM_CCU_DATAPLANE_LIBRARY}
    -Wl,--as-needed
    ascendcl
    acl_rt
    c_sec
    pthread
)

add_subdirectory(op_host)
add_subdirectory(op_kernel_ccu)
