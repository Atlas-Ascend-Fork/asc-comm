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
    ${ASCCOMM_ROOT}/include/aicpu_ts
    ${ASCCOMM_ROOT}/include/aicpu_ts/asccomm
    ${ASCCOMM_ROOT}/pkg_inc/hcomm
    ${ASCEND_CANN_ARCH_DIR}/include
    ${ASCEND_CANN_ARCH_DIR}/include/hccl
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/driver
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/hccl
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/profiling
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/common/utils
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/api_c_adpt
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/stream
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
    experimental/ut_asccomm_experimental_primitives.cc
    ut_asccomm_ub_conn_lite.cc
    ut_asccomm_sqe.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_device.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/stream/sqe_build_a5.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/experimental/asccomm_experimental_primitives_c_adpt.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/task_cache/binary_capture.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection/ub_conn_lite.cc
)
target_include_directories(asccomm_utest_comms_aicpu_primitives PRIVATE
    ${AICPU_UT_INCLUDE_DIRS}
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/task_cache
)
target_compile_definitions(asccomm_utest_comms_aicpu_primitives PRIVATE
    OPEN_BUILD_PROJECT
    ASCCOMM_ENABLE_INTERNAL_APIS
)
target_compile_definitions(asccomm_utest_comms_aicpu_primitives PRIVATE
    __NPU_ARCH__=${ASCCOMM_UT_V310_NPU_ARCH}
)
target_link_libraries(asccomm_utest_comms_aicpu_primitives PRIVATE
    intf_llt_pub_basic
    asccomm_aicpu_runtime_stub
    ${ASCEND_CANN_ARCH_DIR}/lib64/libc_sec.so
    pthread
)
add_test(NAME asccomm_utest_comms_aicpu_primitives COMMAND asccomm_utest_comms_aicpu_primitives)

add_executable(asccomm_binary_native_test main.cc ut_binary_native.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection/ub_conn_lite.cc)
target_include_directories(asccomm_binary_native_test PRIVATE ${AICPU_UT_INCLUDE_DIRS})
target_compile_definitions(asccomm_binary_native_test PRIVATE OPEN_BUILD_PROJECT)
target_link_libraries(asccomm_binary_native_test PRIVATE intf_llt_pub_basic pthread)
add_test(NAME asccomm_binary_native_test COMMAND asccomm_binary_native_test)

# Real capture/transport/runtime; only the HAL boundary is mocked. No NPU work.
add_executable(asccomm_binary_pipeline_test main.cc ut_binary_pipeline.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/task_cache/binary_capture.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_runtime.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_transport.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection/ub_conn_lite.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/stream/sqe_build_a5.cc)
target_include_directories(asccomm_binary_pipeline_test PRIVATE
    ${AICPU_UT_INCLUDE_DIRS}
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/task_cache
)
target_compile_definitions(asccomm_binary_pipeline_test PRIVATE OPEN_BUILD_PROJECT ASCCOMM_ENABLE_INTERNAL_APIS)
target_link_libraries(asccomm_binary_pipeline_test PRIVATE intf_llt_pub_basic pthread)
add_test(NAME asccomm_binary_pipeline_test COMMAND asccomm_binary_pipeline_test)
add_executable(asccomm_public_header_isolation_check
    public_header_isolation_check.cc
)
target_include_directories(asccomm_public_header_isolation_check PRIVATE
    ${ASCEND_CANN_ARCH_DIR}/include
    ${ASCCOMM_ROOT}/include/aicpu_ts
    ${ASCCOMM_ROOT}/pkg_inc/hcomm
)
target_compile_features(asccomm_public_header_isolation_check PRIVATE cxx_std_17)
target_compile_options(asccomm_public_header_isolation_check PRIVATE
    -Wall
    -Werror
)
add_test(NAME asccomm_public_header_isolation_check COMMAND asccomm_public_header_isolation_check)

add_executable(asccomm_experimental_header_isolation_check
    experimental/experimental_header_isolation_check.cc
)
target_include_directories(asccomm_experimental_header_isolation_check PRIVATE
    ${ASCEND_CANN_ARCH_DIR}/include
    ${ASCCOMM_ROOT}/include/aicpu_ts
    ${ASCCOMM_ROOT}/pkg_inc/hcomm
)
target_compile_features(asccomm_experimental_header_isolation_check PRIVATE cxx_std_17)
target_compile_options(asccomm_experimental_header_isolation_check PRIVATE
    -Wall
    -Werror
)
add_test(NAME asccomm_experimental_header_isolation_check COMMAND asccomm_experimental_header_isolation_check)

# 工单104(G1/G2/G4/P3)：UB 连接缝、transport 导出守卫与运行时状态行
add_executable(asccomm_utest_comms_aicpu_transport
    main.cc
    ut_asccomm_transport.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/task_cache/binary_capture.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_runtime.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/stream/sqe_build_a5.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection/ub_conn_lite.cc
)
target_include_directories(asccomm_utest_comms_aicpu_transport PRIVATE
    ${AICPU_UT_INCLUDE_DIRS}
)
target_compile_definitions(asccomm_utest_comms_aicpu_transport PRIVATE OPEN_BUILD_PROJECT ASCCOMM_ENABLE_INTERNAL_APIS)
target_link_libraries(asccomm_utest_comms_aicpu_transport PRIVATE
    intf_llt_pub_basic
    ${ASCEND_CANN_ARCH_DIR}/lib64/libc_sec.so
    pthread
)
add_test(NAME asccomm_utest_comms_aicpu_transport COMMAND asccomm_utest_comms_aicpu_transport)

add_executable(asccomm_utest_comms_aicpu_runtime
    main.cc
    ut_asccomm_runtime.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_device.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/task_cache/binary_capture.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_transport.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/stream/sqe_build_a5.cc
    ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/connection/ub_conn_lite.cc
)
target_include_directories(asccomm_utest_comms_aicpu_runtime PRIVATE
    ${AICPU_UT_INCLUDE_DIRS}
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/driver
)
target_compile_definitions(asccomm_utest_comms_aicpu_runtime PRIVATE OPEN_BUILD_PROJECT ASCCOMM_ENABLE_INTERNAL_APIS)
target_link_libraries(asccomm_utest_comms_aicpu_runtime PRIVATE
    intf_llt_pub_basic
    ${ASCEND_CANN_ARCH_DIR}/lib64/libc_sec.so
    pthread
)
add_test(NAME asccomm_utest_comms_aicpu_runtime COMMAND asccomm_utest_comms_aicpu_runtime)

# Link real profiling hooks for every target exercising native publication.
foreach(target asccomm_utest_comms_aicpu_primitives asccomm_binary_native_test asccomm_binary_pipeline_test asccomm_utest_comms_aicpu_runtime asccomm_utest_comms_aicpu_transport)
    target_sources(${target} PRIVATE ${ASCCOMM_ROOT}/src/aicpu/asccomm/diagnostics/profiling_session.cc)
    target_include_directories(${target} PRIVATE
        ${ASCEND_CANN_ARCH_DIR}/pkg_inc/profiling
        ${ASCEND_CANN_ARCH_DIR}/pkg_inc/aicpu_sched
        ${ASCEND_CANN_ARCH_DIR}/pkg_inc/aicpu
        ${ASCEND_CANN_ARCH_DIR}/include/experiment/slog)
    target_link_libraries(${target} PRIVATE dl
        ${ASCEND_CANN_ARCH_DIR}/lib64/libc_sec.so
        ${ASCEND_CANN_ARCH_DIR}/lib64/libunified_dlog.so)
endforeach()

foreach(target asccomm_binary_native_test asccomm_binary_pipeline_test asccomm_utest_comms_aicpu_transport)
    target_sources(${target} PRIVATE ${ASCCOMM_ROOT}/src/aicpu/asccomm/resource/common/asccomm_device.cc)
endforeach()
