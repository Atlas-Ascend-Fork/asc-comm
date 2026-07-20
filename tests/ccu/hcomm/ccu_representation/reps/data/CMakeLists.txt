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
    ut_ccu_rep_data_test.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_buflocread.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_buflocwrite.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_bufread.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_bufreduce.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_bufwrite.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_loccpy.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_read.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_remMem.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data/ccu_rep_write.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_base.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_block.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common/ccu_rep_nop.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context/ccu_rep_context.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/interface/ccu_datatype.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic/ccu_rep_add.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic/ccu_rep_assign.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_microcode.cc
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_assist.cc
)

add_executable(hccl_utest_comms_ccu_rep_data ${src_to_test_list})

target_include_directories(hccl_utest_comms_ccu_rep_data PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_rep_data PRIVATE
    google=ascend_private
    -DOPEN_HCCL_TEST
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_rep_data PRIVATE
    -U_FORTIFY_SOURCE
    -fno-var-tracking-assignments
    -Os
    -O0 -g --coverage -fprofile-arcs -ftest-coverage -fno-access-control
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address -fsanitize-recover=address,all -fno-omit-frame-pointer>
)

target_link_libraries(hccl_utest_comms_ccu_rep_data PRIVATE
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address>
    intf_llt_pub_basic
    -pthread
    asccomm_ccu_llt_stub
    -lgcov
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_rep_data asccomm_ccu_llt_stub)

enable_testing()
add_test(NAME hccl_utest_comms_ccu_rep_data COMMAND hccl_utest_comms_ccu_rep_data)
