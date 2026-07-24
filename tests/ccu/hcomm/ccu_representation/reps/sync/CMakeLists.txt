# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# sync 测试需要的 CCU 实现源文件（按需编译，避免拉入不需要的重依赖）
set(CCU_SYNC_IMPL_SOURCES
    ${CCU_REP_DIR}/reps/sync/ccu_rep_loc_wait_event.cc
    ${CCU_REP_DIR}/reps/sync/ccu_rep_loc_wait_notify.cc
    ${CCU_REP_DIR}/reps/sync/ccu_rep_loc_record_event.cc
    ${CCU_REP_DIR}/reps/sync/ccu_rep_record_shared_notify.cc
    ${CCU_REP_DIR}/reps/sync/ccu_rep_rempostsem.cc
    ${CCU_REP_DIR}/reps/sync/ccu_rep_rempostvar.cc
    ${CCU_REP_DIR}/reps/sync/ccu_rep_remwaitsem.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_base.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_assign.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_add.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_operator.cc
    ${CCU_REP_DIR}/interface/ccu_datatype.cc
    ${CCU_MICROCODE_DIR}/ccu_microcode.cc
)

set(src_to_test_list
    main.cc
    ut_ccu_rep_sync_test.cc
    ${CCU_SYNC_IMPL_SOURCES}
)

add_executable(hccl_utest_comms_ccu_rep_sync ${src_to_test_list})

target_include_directories(hccl_utest_comms_ccu_rep_sync PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_rep_sync PRIVATE
    google=ascend_private
    -DOPEN_HCCL_TEST
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_rep_sync PRIVATE
    -U_FORTIFY_SOURCE
    -fno-var-tracking-assignments
    -Os
    -O0 -g -fno-access-control
    -w
)

target_link_libraries(hccl_utest_comms_ccu_rep_sync PRIVATE
    intf_llt_pub_basic
    -pthread
    asccomm_ccu_llt_stub
    $<BUILD_INTERFACE:json>
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_rep_sync asccomm_ccu_llt_stub)

enable_testing()
add_test(NAME hccl_utest_comms_ccu_rep_sync COMMAND hccl_utest_comms_ccu_rep_sync)
