# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
project(asccomm_send_with_imm_e2e LANGUAGES C CXX)

if(NOT DEFINED ASCEND_HOME_PATH)
    set(ASCEND_HOME_PATH "$ENV{ASCEND_HOME_PATH}")
endif()
if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    set(ASCEND_CANN_PACKAGE_PATH "${ASCEND_HOME_PATH}")
endif()
if(NOT ASCEND_HOME_PATH)
    message(FATAL_ERROR "ASCEND_HOME_PATH or ASCEND_CANN_PACKAGE_PATH must be set")
endif()

foreach(required_header
    "${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/hcomm_channel.h"
    "${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/hcomm_res.h"
    "${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/hcomm_res_defs.h"
    "${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/aicpu/hcomm/asccomm_primitives.h")
    if(NOT EXISTS "${required_header}")
        message(FATAL_ERROR "Required CANN header was not found: ${required_header}. Install the asc-comm development package first.")
    endif()
endforeach()
find_library(ASCCOMM_AICPU_LIBRARY NAMES asccomm_aicpu
    PATHS "${ASCEND_CANN_PACKAGE_PATH}/lib64" "${ASCEND_CANN_PACKAGE_PATH}/asc/lib64")
if(NOT ASCCOMM_AICPU_LIBRARY)
    message(FATAL_ERROR "libasccomm_aicpu.so was not found under: ${ASCEND_CANN_PACKAGE_PATH}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


add_executable(send_with_imm_server server/main.cpp)
target_include_directories(send_with_imm_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
)
target_link_directories(send_with_imm_server PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/lib64)
target_link_libraries(send_with_imm_server PRIVATE hcomm hccl_plf hccl_alg c_sec ascendcl acl_rt pthread)

add_executable(send_with_imm_client client/main.cpp)
target_include_directories(send_with_imm_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
)
target_link_directories(send_with_imm_client PRIVATE ${ASCEND_CANN_PACKAGE_PATH}/lib64)
target_link_libraries(send_with_imm_client PRIVATE hcomm hccl_plf hccl_alg c_sec ascendcl acl_rt pthread)

add_library(send_with_imm_aicpu SHARED kernel/send_with_imm_kernel.cc)
target_include_directories(send_with_imm_aicpu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/aicpu/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
)
target_link_libraries(send_with_imm_aicpu PRIVATE ${ASCCOMM_AICPU_LIBRARY})
target_link_options(send_with_imm_aicpu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)

set(SEND_WITH_IMM_AICPU_JSON "${CMAKE_CURRENT_BINARY_DIR}/send_with_imm_aicpu.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/send_with_imm_aicpu.json.in" SEND_WITH_IMM_AICPU_JSON_TEMPLATE)
string(REPLACE "@SEND_WITH_IMM_AICPU_SO@" "$<TARGET_FILE:send_with_imm_aicpu>"
    SEND_WITH_IMM_AICPU_JSON_TEMPLATE "${SEND_WITH_IMM_AICPU_JSON_TEMPLATE}")
file(GENERATE OUTPUT "${SEND_WITH_IMM_AICPU_JSON}"
    CONTENT "${SEND_WITH_IMM_AICPU_JSON_TEMPLATE}")
