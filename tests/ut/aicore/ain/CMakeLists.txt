# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# ain.h uses CANN installed-package relative includes:
#   ../hcomm/hcomm.h                             -> asc/include/comm_api/aicore/hcomm/hcomm.h
#   ../../../../impl/comm_api/aicore/ain/impl/*.h -> asc/impl/comm_api/aicore/ain/impl/*.h
# hcomm.h uses:
#   ../../../impl/adv_api/detail/hcomm/impl/*.h  -> asc/impl/adv_api/detail/hcomm/impl/*.h
# ain_impl_def.h uses:
#   ../../hcomm/impl/hcomm_impl_def.h            -> asc/impl/comm_api/aicore/hcomm/impl/hcomm_impl_def.h
#   hcomm/hcomm_team_entity_defs.h               -> CANN pkg_inc/hcomm (sibling of the asc dir)
set(AIN_UT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_UT_ROOT}/common
    ${ASCCOMM_UT_STAGE_INCLUDE_DIR}
    ${ASCCOMM_TIKICPU_INCLUDE_DIR}
    ${ASCCOMM_CANN_INCLUDE_DIRS}
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/include/hcomm
)

function(add_ain_ut target_name product_type tikicpu_product_dir)
    add_executable(${target_name}
        ${ASCCOMM_UT_ROOT}/main_global.cpp
        ${ASCCOMM_UT_ROOT}/common/k3_pvwrap.cpp
        ${ARGN}
    )
    add_dependencies(${target_name} prepare_asccomm_ut_header_layout)

    asccomm_add_product_definitions(${target_name} ${product_type})
    target_compile_definitions(${target_name} PRIVATE ASCENDC_CPU_DEBUG=1)
    target_include_directories(${target_name} PRIVATE ${AIN_UT_INCLUDE_DIRS})
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

    if(target_name MATCHES "ain")
        set(test_filters
            AinUrmaTestSuite.PutImmediateCommitsWrite
            AinUrmaTestSuite.GetImmediateCommitsRead
            AinUrmaTestSuite.DelayedPutFlushCommitsAndDrains
            AinUrmaTestSuite.DelayedGetFlushCommitsAndDrains
            AinUrmaTestSuite.DelayedOperationsFlushPeerChannel
            AinUrmaTestSuite.ReadSignalReturnsValueFromPool
            AinUrmaTestSuite.ReadSignalWithBitsMasksValue
            AinUrmaTestSuite.SignalImmediateCommitsAtomicFAA
            AinUrmaTestSuite.SignalWithSetValueCommitsAtomicFAA
            AinUrmaTestSuite.SignalDelayedDoesNotRingDoorbell
            AinUrmaTestSuite.SignalFlushCommitsAndDrains
            AinUrmaTestSuite.WaitSignalSucceedsWhenValueUpdated
            AinUrmaTestSuite.WaitSignalWithMaskBitsWhenValueUpdated
            AinUrmaTestSuite.BarrierSessionSyncSignalsPeersAndWaits
            AinUrmaTestSuite.BarrierSessionSyncWithTimeout
            AinUrmaTestSuite.BarrierSessionSyncTimeoutExpired
            AinUrmaTestSuite.GetPeerPointerReturnsPeerAddressFromWorldTeamId
            AinUrmaTestSuite.GetPeerPointerReturnsSelfAddressWithoutStride
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
    if(product_type STREQUAL "ascend950pr_9599_AIV")
        add_ain_ut(
            asccomm_ut_ain_ascend950pr_9599_AIV_urma
            ascend950pr_9599_AIV
            Ascend950PR_9599
            test_ain_urma_950.cpp
        )
    endif()
endforeach()
