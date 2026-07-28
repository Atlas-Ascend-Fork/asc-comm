# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# CCU 源文件路径前缀
set(CCU_REP_DIR ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation)
set(CCU_MICROCODE_DIR ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode)

# CCU UT 公共头文件目录：仅限本仓、测试兼容层和 CANN 安装包。
set(UT_COMMON_INCLUDE_LIST
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${CCU_REP_DIR}
    ${CCU_REP_DIR}/context
    ${CCU_REP_DIR}/interface
    ${CCU_REP_DIR}/reps/arithmetic
    ${CCU_REP_DIR}/reps/common
    ${CCU_REP_DIR}/reps/control
    ${CCU_REP_DIR}/reps/data
    ${CCU_REP_DIR}/reps/loop
    ${CCU_REP_DIR}/reps/sync
    ${CCU_REP_DIR}/reps/translator
    ${CCU_MICROCODE_DIR}
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/pub_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCCOMM_UT_ROOT}/../ccu/stub
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/include
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

enable_testing()

add_library(asccomm_ccu_hcomm_compat INTERFACE)
target_include_directories(asccomm_ccu_hcomm_compat INTERFACE
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/include
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/source/base_comm/resources/ccu/ccu_representation/context
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/source/base_comm/resources/ccu/ccu_representation/reps/data
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/source/base_comm/resources/ccu/ccu_representation/reps/sync
)
target_compile_options(asccomm_ccu_hcomm_compat INTERFACE
    -include ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/include/ccu_ut_legacy_compat.h
)

add_subdirectory(../stub stub)

# CCU 数据面子目标
# mockcpp + ABI=0 gtest for features target (ut_ccu_microcode.cc /
# ut_ccu_interface_assist.cc use mockcpp which is built with CXX11_ABI=0; the
# system gtest from intf_llt_pub_basic is ABI=1 and incompatible, so features
# links its own ABI=0 gtest from the third_party cache).
set(ASCCOMM_MOCKCPP_ROOT ${ASCCOMM_ROOT}/third_party/lib_cache/mockcpp)
add_library(asccomm_mockcpp STATIC IMPORTED)
set_target_properties(asccomm_mockcpp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ASCCOMM_MOCKCPP_ROOT}/include;${ASCCOMM_ROOT}/third_party/boost"
    IMPORTED_LOCATION "${ASCCOMM_MOCKCPP_ROOT}/lib/libmockcpp.a"
)

set(ASCCOMM_GTEST_ROOT ${ASCCOMM_ROOT}/third_party/lib_cache/gtest)
add_library(asccomm_gtest_abi0 STATIC IMPORTED)
set_target_properties(asccomm_gtest_abi0 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ASCCOMM_GTEST_ROOT}/include"
    IMPORTED_LOCATION "${ASCCOMM_GTEST_ROOT}/lib/libgtest.a"
)

# CCU features target (f8c8fd2 baseline): ut_ccu_microcode + ut_ccu_interface_assist
# + groupcopy demo. groupcopy has no tests but is part of the f8c8fd2 features target.
set(CCU_FEATURES_IMPL_SOURCES
    ${CCU_MICROCODE_DIR}/ccu_microcode.cc
    ${CCU_REP_DIR}/context/ccu_rep_context.cc
    ${CCU_REP_DIR}/interface/ccu_interface_assist.cc
    ${CCU_REP_DIR}/interface/ccu_datatype.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_base.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_block.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_add.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_assign.cc
)

set(src_to_test_list
    main.cc
    ut_ccu_microcode.cc
    ./ccu_representation/interface/ut_ccu_interface_assist.cc
    ${CCU_FEATURES_IMPL_SOURCES}
)

add_executable(hccl_utest_comms_ccu_features ${src_to_test_list})

target_include_directories(hccl_utest_comms_ccu_features PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_definitions(hccl_utest_comms_ccu_features PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
    google=ascend_private
    -DOPEN_HCCL_TEST
    OPEN_BUILD_PROJECT
)

target_compile_options(hccl_utest_comms_ccu_features PRIVATE
    -U_FORTIFY_SOURCE
    -fno-var-tracking-assignments
    -Os
    -O0 -g --coverage -fprofile-arcs -ftest-coverage -fno-access-control
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address -fsanitize-recover=address,all -fno-omit-frame-pointer>
)

target_link_libraries(hccl_utest_comms_ccu_features PRIVATE
    asccomm_ccu_hcomm_compat
    $<$<STREQUAL:${ENABLE_ASAN},true>:-fsanitize=address>
    asccomm_gtest_abi0
    asccomm_mockcpp
    -pthread
    asccomm_ccu_llt_stub
    -lgcov
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_features asccomm_ccu_llt_stub)

add_test(NAME hccl_utest_comms_ccu_features COMMAND hccl_utest_comms_ccu_features)

add_subdirectory(ccu_representation/reps/sync)
add_subdirectory(ccu_representation/reps/control)
add_subdirectory(ccu_representation/reps/common)
add_subdirectory(ccu_representation/reps/data)
add_subdirectory(ccu_representation/reps/arithmetic)
add_subdirectory(ccu_representation/reps/loop)
add_subdirectory(ccu_representation/context)
add_subdirectory(ccu_representation/interface)
add_subdirectory(ccu_microcode)
