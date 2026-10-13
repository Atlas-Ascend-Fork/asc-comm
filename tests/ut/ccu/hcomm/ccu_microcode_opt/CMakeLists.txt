# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(src_to_test_list
    main.cc
    ut_microcode_optimizer_test.cc
    ut_extract_operands_test.cc
    ut_instruction_scheduler_test.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/microcode_optimizer.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/extract_operands.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/instruction_scheduler.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode/ccu_microcode.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode/ccu_assist.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/channel/ccu_channel.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/common/ccu_device_context.cc
)

add_executable(hccl_utest_comms_ccu_microcode_opt ${src_to_test_list})

target_include_directories(hccl_utest_comms_ccu_microcode_opt PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_microcode_opt PRIVATE
    google=ascend_private
    -DOPEN_HCCL_TEST
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_microcode_opt PRIVATE
    -U_FORTIFY_SOURCE
    -fno-var-tracking-assignments
    -Os
    -O0 -g --coverage -fprofile-arcs -ftest-coverage -fno-access-control
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address -fsanitize-recover=address,all -fno-omit-frame-pointer>
)

target_link_libraries(hccl_utest_comms_ccu_microcode_opt PRIVATE
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address>
    intf_llt_pub_basic
    -pthread
    asccomm_ccu_llt_stub
    -lgcov
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_microcode_opt asccomm_ccu_llt_stub)

enable_testing()
add_test(NAME hccl_utest_comms_ccu_microcode_opt COMMAND hccl_utest_comms_ccu_microcode_opt)
