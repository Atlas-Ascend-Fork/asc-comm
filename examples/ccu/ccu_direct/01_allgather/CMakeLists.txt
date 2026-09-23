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
project(hccl_examples_custom_ops_allgather_ccu_direct LANGUAGES CXX ASC)

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH $ENV{ASCEND_HOME_PATH})
    elseif(DEFINED ENV{ASCEND_OPP_PATH})
        get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
    else()
        message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH or ASCEND_HOME_PATH must be set")
    endif()
endif()

# asc-comm 仓内公开头（ccu/hcomm/*、ccu/ccu_host_launch.h、hcomm/*）：
# 出包后位于 ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api，源码构建时直接引用仓内 include/
get_filename_component(ASCCOMM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# 源码构建时 libasccomm_ccu*.so 产物目录（出包环境位于 ${ASCEND_CANN_PACKAGE_PATH}/lib64，
# 此时保持 ASCOMM_LIBRARY_DIR 为空即可）；先构建：
#   cmake -S ${ASCCOMM_SOURCE_DIR} -B ${ASCCOMM_SOURCE_DIR}/build \
#       -DASCEND_CANN_PACKAGE_PATH=... -DCANN_3RD_LIB_PATH=${ASCCOMM_SOURCE_DIR}/third_party
#   cmake --build ${ASCCOMM_SOURCE_DIR}/build --target asccomm_ccu asccomm_ccu_dataplane
set(ASCCOMM_LIBRARY_DIR "" CACHE PATH "Directory containing libasccomm_ccu.so / libasccomm_ccu_dataplane.so")
if(NOT ASCOMM_LIBRARY_DIR AND EXISTS "${ASCCOMM_SOURCE_DIR}/build/src/ccu/libasccomm_ccu_dataplane.so")
    # 仓内源码构建产物分散两处：libasccomm_ccu.so 在 build/ 顶层，dataplane 在 build/src/ccu/
    list(APPEND ASCOMM_LIBRARY_DIR "${ASCCOMM_SOURCE_DIR}/build" "${ASCCOMM_SOURCE_DIR}/build/src/ccu")
endif()

add_executable(demo
    main.asc
)

target_include_directories(demo PRIVATE
    # CANN Toolkit头文件
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    # asc-comm 仓内公开头（源码构建；出包环境由 asc/include/comm_api 提供）
    ${ASCCOMM_SOURCE_DIR}/include
    # 样例公共辅助头（ccu_instance.h）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_features(demo PRIVATE cxx_std_14)
target_compile_options(demo PRIVATE
    -Werror
    -fstack-protector-strong
    -fPIE
    -O2
    # -s
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
    -Wl,-rpath,${ASCEND_CANN_PACKAGE_PATH}/lib64
)
foreach(_asccomm_lib_dir IN LISTS ASCOMM_LIBRARY_DIR)
    target_link_options(demo PRIVATE
        -Wl,-rpath,${_asccomm_lib_dir}
    )
    target_link_directories(demo PRIVATE
        ${_asccomm_lib_dir}
    )
endforeach()
target_link_directories(demo PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)
target_link_libraries(demo PRIVATE
    -Wl,--no-as-needed
    hcomm
    asccomm_ccu_dataplane
    -Wl,--as-needed
    c_sec
    ascendcl
    acl_rt
    ascendc_runtime
)
