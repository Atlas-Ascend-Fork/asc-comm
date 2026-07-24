# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

set(src_to_test_list
    main.cc
    ut_ccu_rep_control_test.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control/ccu_rep_funcblock.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control/ccu_rep_funccall.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control/ccu_rep_jump.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control/ccu_rep_jumplabel.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_base.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_block.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_loadarg.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_nop.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context/ccu_rep_context.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/interface/ccu_datatype.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic/ccu_rep_add.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic/ccu_rep_assign.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop/ccu_rep_loop.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop/ccu_rep_loopblock.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop/ccu_rep_loopcall.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop/ccu_rep_setloop.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/translator/ccu_rep_reference_manager.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/translator/ccu_rep_translator.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_microcode.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_assist.cc
)

add_executable(hccl_utest_comms_ccu_rep_control ${src_to_test_list})

target_include_directories(hccl_utest_comms_ccu_rep_control PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_rep_control PRIVATE
    google=ascend_private
    -DOPEN_HCCL_TEST
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_rep_control PRIVATE
    -U_FORTIFY_SOURCE
    -fno-var-tracking-assignments
    -Os
    -O0 -g --coverage -fprofile-arcs -ftest-coverage -fno-access-control
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address -fsanitize-recover=address,all -fno-omit-frame-pointer>
)

target_link_libraries(hccl_utest_comms_ccu_rep_control PRIVATE
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address>
    intf_llt_pub_basic
    -pthread
    asccomm_ccu_llt_stub
    -lgcov
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_rep_control asccomm_ccu_llt_stub)

enable_testing()
add_test(NAME hccl_utest_comms_ccu_rep_control COMMAND hccl_utest_comms_ccu_rep_control)
