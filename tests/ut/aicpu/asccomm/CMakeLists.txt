# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software: you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(AICPU_UT_INCLUDE_DIRS
    ${ASCCOMM_ROOT}/include/aicpu
    ${ASCCOMM_ROOT}/include/aicpu/hcomm
    ${ASCEND_CANN_ARCH_DIR}/include
    ${ASCEND_CANN_ARCH_DIR}/include/hccl
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/hccl
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/common/utils
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/api_c_adpt
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common
    ${ASCCOMM_ROOT}/src/common/dfx/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)
if(ASCCOMM_CONTROL_PLANE_SOURCE_DIR)
    list(APPEND AICPU_UT_INCLUDE_DIRS
        ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/include
        ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/pkg_inc
        ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/pkg_inc/hcomm)
endif()

add_library(asccomm_aicpu_launch_stub STATIC
    stub/asccomm_launch_stub.cc
)
add_library(asccomm_aicpu_runtime_stub STATIC
    stub/asccomm_runtime_stub.cc
)
target_include_directories(asccomm_aicpu_launch_stub PUBLIC
    ${AICPU_UT_INCLUDE_DIRS}
)
target_include_directories(asccomm_aicpu_runtime_stub PUBLIC
    ${AICPU_UT_INCLUDE_DIRS}
)

add_executable(asccomm_utest_comms_aicpu_launch
    main.cc
    ut_asccomm_launch.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc
)
target_include_directories(asccomm_utest_comms_aicpu_launch PRIVATE
    ${AICPU_UT_INCLUDE_DIRS}
)
target_link_libraries(asccomm_utest_comms_aicpu_launch PRIVATE
    intf_llt_pub_basic
    asccomm_aicpu_launch_stub
    pthread
)

enable_testing()
add_test(NAME asccomm_utest_comms_aicpu_launch COMMAND asccomm_utest_comms_aicpu_launch)

add_executable(asccomm_utest_comms_aicpu_primitives
    main.cc
    ut_asccomm_primitives.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
)
target_include_directories(asccomm_utest_comms_aicpu_primitives PRIVATE
    ${AICPU_UT_INCLUDE_DIRS}
)
target_compile_definitions(asccomm_utest_comms_aicpu_primitives PRIVATE
    OPEN_BUILD_PROJECT
)
target_link_libraries(asccomm_utest_comms_aicpu_primitives PRIVATE
    intf_llt_pub_basic
    asccomm_aicpu_runtime_stub
    pthread
)
add_test(NAME asccomm_utest_comms_aicpu_primitives COMMAND asccomm_utest_comms_aicpu_primitives)
