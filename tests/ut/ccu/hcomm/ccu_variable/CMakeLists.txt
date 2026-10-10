# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# ccu::variable 运算符 / 控制流宏 UT：
# 链接真实 ccu_primitives_impl.cc（C API 实现层），用本目录录制桩
# （ccu_variable_ut_stub.cc）替代 ccu_kernel / ccu_kernel_mgr，
# 断言 public C++ 运算符到 kernel 方法实参的全链路传递。
# 其余 representation 闭包源文件与 reps/logical 目标保持一致，
# 供 ccu_kernel / ccu_rep_context 构造析构符号闭合。
set(src_to_test_list
    main.cc
    ut_ccu_variable_test.cc
    ccu_variable_ut_stub.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/primitives/ccu_primitives_impl.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/logical/ccu_rep_and.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/logical/ccu_rep_or.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/logical/ccu_rep_not.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/logical/ccu_rep_xor.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/translator/ccu_rep_translator.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/translator/ccu_rep_reference_manager.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_base.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/context/ccu_rep_context.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_block.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/channel/ccu_channel.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/control/ccu_rep_funcblock.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/loop/ccu_rep_loop.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/loop/ccu_rep_loopblock.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/loop/ccu_rep_loopcall.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/loop/ccu_rep_setloop.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/loop/ccu_rep_loopgroup_bundle.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/control/ccu_rep_jump.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/control/ccu_rep_jumplabel.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_load.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_load_var.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_store.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_store_var.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_nop.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/common/ccu_rep_loadarg.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/control/ccu_rep_funccall.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/arithmetic/ccu_rep_add.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/arithmetic/ccu_rep_assign.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/arithmetic/ccu_rep_mul.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/arithmetic/ccu_rep_sub.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/arithmetic/ccu_operator.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/shift/ccu_rep_shl.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/shift/ccu_rep_shr.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_buflocread.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_buflocwrite.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_bufread.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_bufreduce.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_bufwrite.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_loccpy.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_read.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_remMem.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/data/ccu_rep_write.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/common/ccu_device_context.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_event.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_notify.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_loc_record_event.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_record_shared_notify.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_rempostsem.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_rempostvar.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/reps/sync/ccu_rep_remwaitsem.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation/interface/ccu_datatype.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode/ccu_microcode.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/microcode_optimizer.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/extract_operands.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/instruction_scheduler.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode/ccu_assist.cc
)

add_executable(hccl_utest_comms_ccu_variable ${src_to_test_list})

target_include_directories(hccl_utest_comms_ccu_variable PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_variable PRIVATE
    google=ascend_private
    -DOPEN_HCCL_TEST
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_variable PRIVATE
    -U_FORTIFY_SOURCE
    -fno-var-tracking-assignments
    -Os
    -O0 -g --coverage -fprofile-arcs -ftest-coverage -fno-access-control
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address -fsanitize-recover=address,all -fno-omit-frame-pointer>
)

target_link_libraries(hccl_utest_comms_ccu_variable PRIVATE
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address>
    intf_llt_pub_basic
    -pthread
    asccomm_ccu_llt_stub
    -lgcov
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_variable asccomm_ccu_llt_stub)

enable_testing()
add_test(NAME hccl_utest_comms_ccu_variable COMMAND hccl_utest_comms_ccu_variable)
