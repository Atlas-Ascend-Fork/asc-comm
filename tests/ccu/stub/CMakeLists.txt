# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# 精简桩库：只提供 ccu UT 轻依赖组需要的 setTargetPort 符号
add_library(asccomm_ccu_llt_stub STATIC
    llt_hccl_stub_min.cc
    exception_stub.cc
    ccu_channel_stub.cc
    ccu_channel_get_stub.cc
    ccu_dev_mgr_stub.cc
    ccu_kernel_stub.cc
    ccu_rts_stub.cc
)
add_library(asccomm_ccu_rep_assist_stub STATIC
    ccu_rep_assist_stub.cc
)
target_include_directories(asccomm_ccu_llt_stub PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(asccomm_ccu_llt_stub PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
    ${ASCEND_CANN_PACKAGE_PATH}/include
)
target_include_directories(asccomm_ccu_rep_assist_stub PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)
target_link_libraries(asccomm_ccu_rep_assist_stub PRIVATE
    asccomm_ccu_hcomm_compat
    $<BUILD_INTERFACE:json>
)
target_link_libraries(asccomm_ccu_llt_stub PUBLIC
    asccomm_ccu_hcomm_compat
    $<BUILD_INTERFACE:json>
    asccomm_ccu_rep_assist_stub
)
