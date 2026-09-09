# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# 精简桩库：只提供 ccu UT 轻依赖组需要的 setTargetPort 等符号
#
# 桩库必须与其消费方 UT 目标同为同一 CXX11 ABI：桩内实现的析构/构造会跨 TU
# 作用于测试目标构造的对象（如 ccu_kernel），ABI 不一致会导致 std::string 等
# 成员布局错位，析构时在错误偏移上操作引发段错误。
#
# 消费方分两类，因此提供两种 ABI 风味：
# - 常规 UT 目标（translator/reps/context 等）跟随 tests/ut 里 intf_llt_pub_basic
#   的 gtest 风味：系统 gtest 为 ABI=1，无系统 gtest 时 cann-cmake 缓存 gtest 为
#   ABI=0（ASCCOMM_GTEST_USES_CXX11_ABI0），链接 asccomm_ccu_llt_stub。
# - features 目标强制 _GLIBCXX_USE_CXX11_ABI=0（mockcpp 与 ABI=0 gtest），
#   链接恒为 ABI=0 的 asccomm_ccu_llt_stub_abi0。
set(ASCCOMM_STUB_SOURCES
    llt_hccl_stub_min.cc
    exception_stub.cc
    ccu_channel_stub.cc
    ccu_channel_get_stub.cc
    ccu_dev_mgr_stub.cc
    ccu_kernel_stub.cc
    ccu_rts_stub.cc
)

set(ASCCOMM_STUB_GTEST_ABI_DEFS)
if(ASCCOMM_GTEST_USES_CXX11_ABI0)
    set(ASCCOMM_STUB_GTEST_ABI_DEFS _GLIBCXX_USE_CXX11_ABI=0)
endif()

# 常规风味：ABI 跟随 gtest 风味
add_library(asccomm_ccu_llt_stub STATIC
    ${ASCCOMM_STUB_SOURCES}
)
add_library(asccomm_ccu_rep_assist_stub STATIC
    ccu_rep_assist_stub.cc
)
target_compile_definitions(asccomm_ccu_llt_stub PRIVATE
    ${ASCCOMM_STUB_GTEST_ABI_DEFS}
)
target_compile_definitions(asccomm_ccu_rep_assist_stub PRIVATE
    ${ASCCOMM_STUB_GTEST_ABI_DEFS}
)

# features 专用风味：恒为 ABI=0
add_library(asccomm_ccu_llt_stub_abi0 STATIC
    ${ASCCOMM_STUB_SOURCES}
)
add_library(asccomm_ccu_rep_assist_stub_abi0 STATIC
    ccu_rep_assist_stub.cc
)
target_compile_definitions(asccomm_ccu_llt_stub_abi0 PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
)
target_compile_definitions(asccomm_ccu_rep_assist_stub_abi0 PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
)

foreach(_asccomm_stub_target
        asccomm_ccu_llt_stub
        asccomm_ccu_llt_stub_abi0)
    target_include_directories(${_asccomm_stub_target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_include_directories(${_asccomm_stub_target} PRIVATE
        ${UT_COMMON_INCLUDE_LIST}
        ${ASCEND_CANN_PACKAGE_PATH}/include
    )
    target_link_libraries(${_asccomm_stub_target} PUBLIC
        asccomm_ccu_hcomm_compat
    )
endforeach()

foreach(_asccomm_rep_assist_target
        asccomm_ccu_rep_assist_stub
        asccomm_ccu_rep_assist_stub_abi0)
    target_include_directories(${_asccomm_rep_assist_target} PRIVATE
        ${UT_COMMON_INCLUDE_LIST}
    )
    target_link_libraries(${_asccomm_rep_assist_target} PRIVATE
        asccomm_ccu_hcomm_compat
    )
endforeach()

target_link_libraries(asccomm_ccu_llt_stub PUBLIC
    asccomm_ccu_rep_assist_stub
)
target_link_libraries(asccomm_ccu_llt_stub_abi0 PUBLIC
    asccomm_ccu_rep_assist_stub_abi0
)
