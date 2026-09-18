# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# CCU 源文件路径前缀
set(CCU_REP_DIR ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/representation)
set(CCU_MICROCODE_DIR ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode)

# CCU UT 公共头文件目录：仅限本仓、测试兼容层和 CANN 安装包。
set(UT_COMMON_INCLUDE_LIST
    ${ASCCOMM_UT_ROOT}/ccu/hcomm_compat/include
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/src/ccu
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCCOMM_UT_ROOT}/ccu/stub
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

enable_testing()

add_library(asccomm_ccu_hcomm_compat INTERFACE)
target_include_directories(asccomm_ccu_hcomm_compat INTERFACE
    ${ASCCOMM_UT_ROOT}/ccu/hcomm_compat/include
)
target_compile_options(asccomm_ccu_hcomm_compat INTERFACE
    -include ${ASCCOMM_UT_ROOT}/ccu/hcomm_compat/include/ccu_ut_legacy_compat.h
)

add_subdirectory(../stub stub)

# CCU 数据面子目标
# mockcpp + ABI=0 gtest for features target (ut_ccu_microcode.cc /
# ut_ccu_interface_assist.cc use mockcpp which is built with CXX11_ABI=0; the
# system gtest from intf_llt_pub_basic is ABI=1 and incompatible, so features
# links its own ABI=0 gtest from the third_party cache).
add_cann_third_party(boost)
add_cann_third_party(boost)
add_cann_third_party(mockcpp)

set(ASCCOMM_MOCKCPP_ROOT ${CANN_3RD_LIB_PATH}/lib_cache/mockcpp)
add_library(asccomm_mockcpp STATIC IMPORTED)
add_dependencies(asccomm_mockcpp mockcpp)
set_target_properties(asccomm_mockcpp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ASCCOMM_MOCKCPP_ROOT}/include;${CANN_3RD_LIB_PATH}/boost"
    IMPORTED_LOCATION "${ASCCOMM_MOCKCPP_ROOT}/lib/libmockcpp.a"
)
if(NOT EXISTS "${ASCCOMM_MOCKCPP_ROOT}/include")
    file(MAKE_DIRECTORY "${ASCCOMM_MOCKCPP_ROOT}/include")
endif()

set(ASCCOMM_GTEST_ROOT ${CANN_3RD_LIB_PATH}/lib_cache/gtest)
if(NOT TARGET asccomm_gtest_abi0)
    add_library(asccomm_gtest_abi0 STATIC IMPORTED)
    set_target_properties(asccomm_gtest_abi0 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ASCCOMM_GTEST_ROOT}/include"
        IMPORTED_LOCATION "${ASCCOMM_GTEST_ROOT}/lib/libgtest.a"
    )
endif()
if(NOT EXISTS "${ASCCOMM_GTEST_ROOT}/include")
    file(MAKE_DIRECTORY "${ASCCOMM_GTEST_ROOT}/include")
endif()
# cann-cmake 的 gtest.cmake 只在系统无 gtest（tests/ut 里 find_package(GTest) 失败）
# 时才生成 third_party_gtest 并填充 lib_cache/gtest；宿主机装有系统 gtest（ABI=1）
# 时该缓存永远不会被填充，上面的导入目标无库可链，导致 make 报
# "No rule to make target .../lib_cache/gtest/lib/libgtest.a"。
# 这里在缓存缺失且 cann-cmake 未接管时，用与 gtest.cmake 相同的来源（本地归档优先，
# 否则 CANN OBS 镜像）构建 CXX11_ABI=0 的 googletest 1.14.0 并安装到同一缓存位置。
if(TARGET third_party_gtest)
    # cann-cmake 已接管 gtest 缓存，只需保证它在链接 features 前完成安装。
    add_dependencies(asccomm_gtest_abi0 third_party_gtest)
elseif(NOT EXISTS "${ASCCOMM_GTEST_ROOT}/lib/libgtest.a")
    set(ASCCOMM_GTEST_TARBALL "googletest-1.14.0.tar.gz")
    if(EXISTS "${CANN_3RD_LIB_PATH}/gtest/${ASCCOMM_GTEST_TARBALL}")
        set(ASCCOMM_GTEST_URL "${CANN_3RD_LIB_PATH}/gtest/${ASCCOMM_GTEST_TARBALL}")
    elseif(EXISTS "${CANN_3RD_LIB_PATH}/pkg/${ASCCOMM_GTEST_TARBALL}")
        set(ASCCOMM_GTEST_URL "${CANN_3RD_LIB_PATH}/pkg/${ASCCOMM_GTEST_TARBALL}")
    else()
        set(ASCCOMM_GTEST_URL
            "https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/googletest/${ASCCOMM_GTEST_TARBALL}")
    endif()
    include(ExternalProject)
    ExternalProject_Add(asccomm_gtest_abi0_build
        URL ${ASCCOMM_GTEST_URL}
        DOWNLOAD_DIR ${CANN_3RD_LIB_PATH}/pkg
        CMAKE_ARGS
            -DCMAKE_CXX_FLAGS=-D_GLIBCXX_USE_CXX11_ABI=0
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${ASCCOMM_GTEST_ROOT}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DBUILD_TESTING=OFF
            -DBUILD_SHARED_LIBS=OFF
        EXCLUDE_FROM_ALL TRUE
    )
    add_dependencies(asccomm_gtest_abi0 asccomm_gtest_abi0_build)
endif()

# CCU features target (f8c8fd2 baseline): ut_ccu_microcode + ut_ccu_interface_assist
# + groupcopy demo. groupcopy has no tests but is part of the f8c8fd2 features target.
set(CCU_FEATURES_IMPL_SOURCES
    ${CCU_MICROCODE_DIR}/ccu_microcode.cc
    ${CCU_REP_DIR}/context/ccu_rep_context.cc
    ${CCU_REP_DIR}/interface/ccu_interface_assist.cc
    ${CCU_REP_DIR}/interface/ccu_datatype.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_base.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/channel/ccu_channel.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/common/ccu_device_context.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_block.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_add.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_assign.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_sub.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_rep_mul.cc
    ${CCU_REP_DIR}/reps/arithmetic/ccu_operator.cc
    ${CCU_REP_DIR}/reps/logical/ccu_rep_and.cc
    ${CCU_REP_DIR}/reps/logical/ccu_rep_or.cc
    ${CCU_REP_DIR}/reps/logical/ccu_rep_not.cc
    ${CCU_REP_DIR}/reps/logical/ccu_rep_xor.cc
    ${CCU_REP_DIR}/reps/shift/ccu_rep_shl.cc
    ${CCU_REP_DIR}/reps/shift/ccu_rep_shr.cc
    ${CCU_REP_DIR}/reps/translator/ccu_ins_generater_v1.cc
    ${CCU_REP_DIR}/reps/translator/ccu_rep_translator.cc
    ${CCU_REP_DIR}/reps/translator/ccu_rep_reference_manager.cc
    ${CCU_MICROCODE_DIR}/ccu_assist.cc
    ${CCU_REP_DIR}/reps/control/ccu_rep_funcblock.cc
    ${CCU_REP_DIR}/reps/control/ccu_rep_funccall.cc
    ${CCU_REP_DIR}/reps/loop/ccu_rep_loop.cc
    ${CCU_REP_DIR}/reps/loop/ccu_rep_loopblock.cc
    ${CCU_REP_DIR}/reps/loop/ccu_rep_loopcall.cc
    ${CCU_REP_DIR}/reps/loop/ccu_rep_setloop.cc
    ${CCU_REP_DIR}/reps/loop/ccu_rep_loopgroup_bundle.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_loadarg.cc
    ${CCU_REP_DIR}/reps/common/ccu_rep_nop.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/microcode_optimizer.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/extract_operands.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/microcode_opt/instruction_scheduler.cc
)

set(src_to_test_list
    main.cc
    ut_ccu_microcode.cc
    ut_ccu_kernel_res_allocator.cc
    ./ccu_representation/interface/ut_ccu_interface_assist.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/kernel/ccu_kernel_res_allocator.cc
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
    asccomm_ccu_llt_stub_abi0
    -lgcov
    -lrt
    -ldl
)

add_dependencies(hccl_utest_comms_ccu_features asccomm_ccu_llt_stub_abi0)

add_test(NAME hccl_utest_comms_ccu_features COMMAND hccl_utest_comms_ccu_features)

add_executable(hccl_utest_comms_asc_ccu_res_snapshot
    asc_ccu_res_snapshot_main.cc
    ut_asc_ccu_res_snapshot.cc
    ${ASCCOMM_ROOT}/src/ccu/hcomm/resource/common/asc_ccu_res_snapshot.cc
)
target_include_directories(hccl_utest_comms_asc_ccu_res_snapshot PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)
target_link_libraries(hccl_utest_comms_asc_ccu_res_snapshot PRIVATE
    intf_llt_pub_basic
)
add_test(NAME hccl_utest_comms_asc_ccu_res_snapshot COMMAND hccl_utest_comms_asc_ccu_res_snapshot)

add_subdirectory(ccu_representation/reps/sync)
add_subdirectory(ccu_representation/reps/control)
add_subdirectory(ccu_representation/reps/common)
add_subdirectory(ccu_representation/reps/data)
add_subdirectory(ccu_representation/reps/arithmetic)
add_subdirectory(ccu_representation/reps/loop)
add_subdirectory(ccu_representation/reps/logical)
add_subdirectory(ccu_representation/reps/shift)
add_subdirectory(ccu_representation/context)
add_subdirectory(ccu_representation/interface)
add_subdirectory(ccu_representation/translator)
add_subdirectory(ccu_microcode)
add_subdirectory(ccu_microcode_opt)
add_subdirectory(resource/channel)
