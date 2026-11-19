# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_UT_INCLUDE_DIRS
    ${ASCCOMM_UT_ROOT}/aicore/stub
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_UT_ROOT}/common
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/pkg_inc
    ${ASCCOMM_UT_STAGE_INCLUDE_DIR}
    ${ASCCOMM_TIKICPU_INCLUDE_DIR}
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

add_library(asccomm_ut_hcomm_mock SHARED hcomm_channel_mock.cpp)
target_include_directories(asccomm_ut_hcomm_mock PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)
set_target_properties(asccomm_ut_hcomm_mock PROPERTIES
    OUTPUT_NAME hcomm
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mock_hcomm"
)

function(add_asccomm_ut target_name product_type tikicpu_product_dir)
    add_executable(${target_name}
        ${ASCCOMM_UT_ROOT}/main_global.cpp
        ${ASCCOMM_UT_ROOT}/common/k3_pvwrap.cpp
        ${ARGN}
    )
    add_dependencies(${target_name} prepare_asccomm_ut_header_layout)

    asccomm_add_product_definitions(${target_name} ${product_type})
    target_compile_definitions(${target_name} PRIVATE ASCENDC_CPU_DEBUG=1)
    target_include_directories(${target_name} PRIVATE ${ASCCOMM_UT_INCLUDE_DIRS})
    target_compile_options(${target_name} PRIVATE
        -fno-access-control
        -Werror
        -UASCENDC_DEBUG
    )
    set(runtime_dirs
        ${ASCEND_CANN_PACKAGE_PATH}/lib64
        ${ASCEND_CANN_ARCH_DIR}/lib64
        ${ASCEND_CANN_ARCH_DIR}/devlib/device
        ${ASCCOMM_TIKICPU_DIR}
        ${ASCCOMM_TIKICPU_DIR}/${tikicpu_product_dir}
        ${ASCCOMM_TIKICPU_LIB64_DIR}
        ${ASCCOMM_TIKICPU_LIB64_DIR}/${tikicpu_product_dir}
        ${ASCCOMM_CPUDEBUG_LIB64_DIR}
        ${ASCCOMM_CPUDEBUG_LIB64_DIR}/${tikicpu_product_dir}
        ${ASCEND_CANN_PACKAGE_PATH}/tools/simulator/${tikicpu_product_dir}/lib
    )
    if(target_name MATCHES "ascend910B1_AIC")
        list(PREPEND runtime_dirs "$<TARGET_FILE_DIR:asccomm_ut_hcomm_mock>")
        add_dependencies(${target_name} asccomm_ut_hcomm_mock)
    endif()
    target_link_directories(${target_name} PRIVATE
        ${runtime_dirs}
    )
    set_target_properties(${target_name} PROPERTIES BUILD_RPATH "${runtime_dirs}")
    target_link_libraries(${target_name} PRIVATE
        intf_llt_pub_basic
        kernel_tiling
        -Wl,--no-as-needed
        tikcpp_debug
        cpudebug
        pem_davinci
        cpudebug_stubreg
        cpudebug_cceprint
        cpudebug_npuchk
        error_manager
        mmpa
        c_sec
        -Wl,--as-needed
        ${CMAKE_DL_LIBS}
    )

    if(TARGET tikicpulib::${product_type})
        target_link_libraries(${target_name} PRIVATE tikicpulib::${product_type})
    endif()

    if(target_name MATCHES "ascend910B1_AIC")
        set(test_filters
            asccomm_common_test_suite.AIV_READ
            asccomm_common_test_suite.AIV_WRITE
            asccomm_common_test_suite.AIV_WRITE_WITH_NOTIFY_NBI
            asccomm_host_cleanup_test.*
        )
    elseif(target_name MATCHES "roce")
        set(test_filters
            asccomm_roce_test_suite.*
        )
    elseif(target_name MATCHES "jetty")
        set(test_filters
            asccomm_jetty_test_suite.AIV_JETTY_PEER_RESOLVES_REMOTE_METADATA
            asccomm_jetty_test_suite.AIV_JETTY_INFO_SPLITS_PACKED_HEAD
            asccomm_jetty_test_suite.AIV_JETTY_ADVANCE_SQ_AND_RING_DOORBELL
            asccomm_jetty_test_suite.AIV_JETTY_ADVANCE_SQ_WRAPS_HEAD
            asccomm_jetty_test_suite.AIV_JETTY_DRAIN_NO_PENDING_COMPLETIONS
        )
    elseif(target_name MATCHES "urma")
        set(test_filters
            asccomm_urma_test_suite.AIV_URMA_LOCK_UNLOCK
            asccomm_urma_test_suite.AIV_URMA_UNLOCK_WITHOUT_LOCK
            asccomm_urma_test_suite.AIV_URMA_READ
            asccomm_urma_test_suite.AIV_URMA_WRITE
            asccomm_urma_test_suite.AIV_URMA_WRITE_REDUCE_MAX_INT8
            asccomm_urma_test_suite.AIV_URMA_WRITE_REDUCE_MIN_HALF
            asccomm_urma_test_suite.AIV_URMA_WRITE_REDUCE_SUM_FLOAT
            asccomm_urma_test_suite.AIV_URMA_WRITE_WITH_NOTIFY
            asccomm_urma_test_suite.AIV_URMA_WRITE_WITH_VALUE
            asccomm_urma_test_suite.AIV_URMA_WRITE_WITH_VALUE_DELAYED_COMMIT
            asccomm_urma_test_suite.AIV_URMA_WRITE_WITH_VALUE_WQEBB_CNT
            asccomm_urma_test_suite.AIV_URMA_WRITE_WITH_NOTIFY_WQEBB_CNT
            asccomm_urma_test_suite.AIV_URMA_BATCH_COMMIT_DRAIN
            asccomm_urma_test_suite.AIV_URMA_INIT_UB_POINTER
            asccomm_urma_test_suite.AIV_URMA_MAKE_BATCH_HANDLE_UB_POINTER
            asccomm_urma_test_suite.AIV_URMA_MULTI_CHANNEL_BATCH_USES_OUTER_HANDLE_FOR_COMMIT_AND_DRAIN
            asccomm_urma_test_suite.AIV_URMA_BATCH_WRITE_NBI
            asccomm_urma_test_suite.AIV_URMA_BATCH_WRITE_NBI_MULTI_SGE
            asccomm_urma_test_suite.AIV_URMA_BATCH_WRITE_WITH_NOTIFY_NBI_MULTI_SGE
            asccomm_urma_test_suite.AIV_URMA_BATCH_READ_NBI
            asccomm_urma_test_suite.AIV_URMA_BATCH_WRITE_WITH_NOTIFY_NBI_MIXED
            asccomm_urma_test_suite.AIV_URMA_BATCH_WRITE_WITH_NOTIFY_ENCODES64_BIT_ADDRESSES
            asccomm_urma_test_suite.AIV_URMA_BATCH_READ_WRITE_NOTIFY_MIXED_COMMIT
            asccomm_urma_test_suite.AIV_URMA_BATCH_HANDLE_COMMIT_WRAP_AND_REUSE
            asccomm_urma_test_suite.AIV_URMA_BATCH_WRITE_WITH_NOTIFY_COMMIT_WRAPS_SINGLE_WQE
            asccomm_urma_test_suite.AIV_URMA_BATCH_ALL_NO_CQE_COMMIT_AND_DRAIN
            asccomm_urma_test_suite.AIV_URMA_BATCH_MULTIPLE_COMMITS_SINGLE_DRAIN
            asccomm_urma_test_suite.AIV_URMA_BATCH_DRAIN_WITHOUT_INIT
            asccomm_urma_test_suite.AIV_URMA_DUMP_HELPERS_VALID
            asccomm_urma_test_suite.AIV_URMA_DUMP_AMO_CTX_FAA
            asccomm_urma_test_suite.AIV_URMA_DUMP_AMO_CTX_CAS
        )
    endif()

    run_llt_test(
        TARGET ${target_name}
        TASK_NUM 1
        FILTERS ${test_filters}
        RUNTIME_DIRS ${runtime_dirs}
    )
endfunction()

foreach(product_type ${PRODUCT_TYPE_LIST})
    if(product_type STREQUAL "ascend910B1_AIC")
        add_asccomm_ut(
            asccomm_ut_asccomm_ascend910B1_AIC
            ascend910B1_AIC
            Ascend910B1
            test_asccomm.cpp
            test_asccomm_host_cleanup.cpp
        )
    elseif(product_type STREQUAL "ascend950pr_9599_AIV")
        add_asccomm_ut(
            asccomm_ut_asccomm_ascend950pr_9599_AIV_roce
            ascend950pr_9599_AIV
            Ascend950PR_9599
            test_asccomm_roce_950.cpp
        )
        add_asccomm_ut(
            asccomm_ut_asccomm_ascend950pr_9599_AIV_urma
            ascend950pr_9599_AIV
            Ascend950PR_9599
            test_asccomm_urma_950.cpp
        )
        add_asccomm_ut(
            asccomm_ut_asccomm_ascend950pr_9599_AIV_jetty
            ascend950pr_9599_AIV
            Ascend950PR_9599
            test_asccomm_jetty_950.cpp
        )
    endif()
endforeach()
