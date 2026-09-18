# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

add_executable(hccl_utest_comms_ccu_channel
    ${ASCCOMM_ROOT}/tests/ut/ccu/hcomm/main.cc
    ut_ccu_channel_test.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/channel/ccu_channel.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/common/ccu_device_context.cc
)

target_include_directories(hccl_utest_comms_ccu_channel PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_channel PRIVATE
    google=ascend_private
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_channel PRIVATE
    -U_FORTIFY_SOURCE
    -O0 -g
)

target_link_libraries(hccl_utest_comms_ccu_channel PRIVATE
    intf_llt_pub_basic
    asccomm_ccu_llt_stub
)

add_dependencies(hccl_utest_comms_ccu_channel asccomm_ccu_llt_stub)

enable_testing()
add_test(NAME hccl_utest_comms_ccu_channel COMMAND hccl_utest_comms_ccu_channel)
