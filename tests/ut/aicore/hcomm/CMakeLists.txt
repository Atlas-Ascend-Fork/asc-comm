# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(HCOMM_STAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package_layout")
set(HCOMM_STAGE_INCLUDE_DIR "${HCOMM_STAGE_ROOT}/asc/include/comm_api")
set(HCOMM_STAGE_DETAIL_DIR "${HCOMM_STAGE_ROOT}/asc/impl/comm_api/aicore/hcomm")

add_custom_target(prepare_hcomm_header_layout
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${HCOMM_STAGE_INCLUDE_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${HCOMM_STAGE_DETAIL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HCOMM_STAGE_INCLUDE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HCOMM_STAGE_DETAIL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/include/aicore/hcomm"
        "${HCOMM_STAGE_INCLUDE_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/hcomm"
        "${HCOMM_STAGE_DETAIL_DIR}"
    COMMENT "Preparing Hcomm headers in the installed package layout"
)

set(HCOMM_UT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_UT_ROOT}/common
    ${HCOMM_STAGE_INCLUDE_DIR}
    ${ASCCOMM_TIKICPU_INCLUDE_DIR}
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

function(add_hcomm_ut target_name product_type tikicpu_product_dir)
    add_executable(${target_name}
        ${ASCCOMM_UT_ROOT}/main_global.cpp
        ${ASCCOMM_UT_ROOT}/common/k3_pvwrap.cpp
        ${ARGN}
    )
    add_dependencies(${target_name} prepare_hcomm_header_layout)

    if(previous_hcomm_ut_target)
        add_dependencies(${target_name} ${previous_hcomm_ut_target})
    endif()
    set(previous_hcomm_ut_target ${target_name} PARENT_SCOPE)

    asccomm_add_product_definitions(${target_name} ${product_type})
    target_compile_definitions(${target_name} PRIVATE ASCENDC_CPU_DEBUG=1)
    target_include_directories(${target_name} PRIVATE ${HCOMM_UT_INCLUDE_DIRS})
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
    )

    if(TARGET tikicpulib::${product_type})
        target_link_libraries(${target_name} PRIVATE tikicpulib::${product_type})
    endif()

    if(target_name MATCHES "ascend910B1_AIC")
        set(test_filters
            HcommCommonTestSuite.Aiv_Read
            HcommCommonTestSuite.Aiv_Write
            HcommCommonTestSuite.Aiv_WriteWithNotifyNbi
        )
    elseif(target_name MATCHES "roce")
        set(test_filters
            HcommRoCETestSuite.Init_ReadNbi_Drain
            HcommRoCETestSuite.Init_WriteNbi_Drain
            HcommRoCETestSuite.Init_WriteWithNotifyNbi
        )
    elseif(target_name MATCHES "urma")
        set(test_filters
            HcommUrmaTestSuite.Aiv_Urma_Read
            HcommUrmaTestSuite.Aiv_Urma_Write
            HcommUrmaTestSuite.Aiv_Urma_WriteReduce_MaxInt8
            HcommUrmaTestSuite.Aiv_Urma_WriteReduce_MinHalf
            HcommUrmaTestSuite.Aiv_Urma_WriteReduce_SumFloat
            HcommUrmaTestSuite.Aiv_Urma_WriteWithNotify
            HcommUrmaTestSuite.Aiv_Urma_WriteWithNotify_WqeBbCnt
            HcommUrmaTestSuite.Aiv_Urma_RemoteBufferNotFound
            HcommUrmaTestSuite.Aiv_Urma_BatchCommitDrain
            HcommUrmaTestSuite.Aiv_Urma_InitLocalTensor
            HcommUrmaTestSuite.Aiv_Urma_DumpHelpers_Nullptr
            HcommUrmaTestSuite.Aiv_Urma_DumpHelpers_Valid
            HcommUrmaTestSuite.Aiv_Urma_DumpAmoCtx_FAA
            HcommUrmaTestSuite.Aiv_Urma_DumpAmoCtx_CAS
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
        add_hcomm_ut(
            asccomm_ut_hcomm_ascend910B1_AIC
            ascend910B1_AIC
            Ascend910B1
            test_hcomm.cpp
        )
    elseif(product_type STREQUAL "ascend950pr_9599_AIV")
        add_hcomm_ut(
            asccomm_ut_hcomm_ascend950pr_9599_AIV_roce
            ascend950pr_9599_AIV
            Ascend950PR_9599
            test_hcomm_roce_950.cpp
        )
        add_hcomm_ut(
            asccomm_ut_hcomm_ascend950pr_9599_AIV_urma
            ascend950pr_9599_AIV
            Ascend950PR_9599
            test_hcomm_urma_950.cpp
        )
    endif()
endforeach()
