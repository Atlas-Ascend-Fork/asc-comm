# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(HCOMM_UT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_UT_ROOT}/common
    ${ASCCOMM_UT_STAGE_INCLUDE_DIR}
    ${ASCCOMM_TIKICPU_INCLUDE_DIR}
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

function(add_hcomm_ut target_name product_type tikicpu_product_dir)
    add_executable(${target_name}
        ${ASCCOMM_UT_ROOT}/main_global.cpp
        ${ASCCOMM_UT_ROOT}/common/k3_pvwrap.cpp
        ${ARGN}
    )
    add_dependencies(${target_name} prepare_asccomm_ut_header_layout)

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
            HcommUrmaTestSuite.Aiv_Urma_MakeBatchHandleLocalTensor
            HcommUrmaTestSuite.Aiv_Urma_MultiChannelBatchUsesOuterHandleForCommitAndDrain
            HcommUrmaTestSuite.Aiv_Urma_BatchWriteNbi
            HcommUrmaTestSuite.Aiv_Urma_BatchReadNbi
            HcommUrmaTestSuite.Aiv_Urma_BatchWriteWithNotifyNbiMixed
            HcommUrmaTestSuite.Aiv_Urma_BatchWriteWithNotifyEncodes64BitAddresses
            HcommUrmaTestSuite.Aiv_Urma_BatchReadWriteNotifyMixedCommit
            HcommUrmaTestSuite.Aiv_Urma_BatchCommitRejectsInvalidCounts
            HcommUrmaTestSuite.Aiv_Urma_BatchHandleCommitWrapAndReuse
            HcommUrmaTestSuite.Aiv_Urma_BatchWriteWithNotifyCommitWrapsSingleWqe
            HcommUrmaTestSuite.Aiv_Urma_BatchAllNoCqeCommitAndDrain
            HcommUrmaTestSuite.Aiv_Urma_BatchMultipleCommitsSingleDrain
            HcommUrmaTestSuite.Aiv_Urma_BatchDrainRejectsInvalidHandleState
            HcommUrmaTestSuite.Aiv_Urma_BatchDrainWithoutInit
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
