# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(../fetch_cann_cmake.cmake)
project(asc_comm_device LANGUAGES C CXX)

set(PRODUCT_SIDE device)
init_cann_project()
add_cann_target_options()

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(_ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(_ASCCOMM_CANN_ARCH "x86_64-linux")
endif()
set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}")

add_subdirectory(../../src/aicpu asc-comm-build)

cann_pack_targets_and_files(
    OUTPUT_TARGET pack_asccomm_aicpu
    OUTPUT asccomm_aicpu.tar.gz
    TARGETS asccomm_aicpu
    MANIFEST bin_hash.cfg
    SIZE_LIMIT 1024
    TAR_ROOT_DIR aicpu_kernels_device
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/asccomm_aicpu.tar.gz"
    DESTINATION opp/built-in/op_impl/aicpu/kernel
    COMPONENT asccomm)

install(TARGETS asccomm_aicpu
    LIBRARY DESTINATION "${_ASCCOMM_CANN_ARCH}/devlib/device"
    COMPONENT asccomm)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../include/aicpu/asccomm/"
    DESTINATION asc/include/comm_api/aicpu/asccomm
    COMPONENT asccomm
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
