# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

set(ASCCOMM_UT_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_UT_ROOT}/../.. ABSOLUTE)
if(NOT CANN_3RD_LIB_PATH)
    set(CANN_3RD_LIB_PATH "${ASCCOMM_ROOT}/third_party" CACHE PATH "CANN third_party path" FORCE)
endif()

include("${ASCCOMM_ROOT}/cmake/fetch_cann_cmake.cmake")

project(asccomm_hcomm_ut LANGUAGES C CXX)
init_cann_project(PREPEND_MODULE_PATH)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PRODUCT_TYPE_LIST "ascend950pr_9599_AIV;ascend910B1_AIC" CACHE STRING "Hcomm UT product types")
set(TEST_MOD "all" CACHE STRING "Comma-separated UT targets to build and run, or all")
option(ASCCOMM_UT_RUN_AFTER_BUILD "Run UT targets after build" ON)
option(ENABLE_GCOV "Enable gcov coverage for UT targets" OFF)

if(CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(_ASCEND_CANN_PACKAGE_PATH "${CUSTOM_ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ASCEND_CANN_PACKAGE_PATH)
    set(_ASCEND_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
    set(_ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(_ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
else()
    set(_ASCEND_CANN_PACKAGE_PATH "/usr/local/Ascend/cann")
endif()
set(ASCEND_CANN_PACKAGE_PATH "${_ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "CANN package path")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(ASCCOMM_CANN_ARCH "x86_64-linux")
endif()

set(ASCEND_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${ASCCOMM_CANN_ARCH}")
if(NOT EXISTS "${ASCEND_CANN_ARCH_DIR}")
    set(ASCEND_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux")
endif()
set(ASCENDC_DIR "${ASCEND_CANN_ARCH_DIR}/asc" CACHE PATH "AscendC headers path")

set(ASCCOMM_CANN_INCLUDE_DIRS
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCENDC_DIR}
    ${ASCENDC_DIR}/include
    ${ASCENDC_DIR}/include/basic_api
    ${ASCENDC_DIR}/impl/basic_api
)
set(ASCCOMM_TIKICPU_DIR ${ASCEND_CANN_PACKAGE_PATH}/tools/tikicpulib/lib)
set(ASCCOMM_TIKICPU_INCLUDE_DIR ${ASCCOMM_TIKICPU_DIR}/include)
set(ASCCOMM_TIKICPU_LIB64_DIR ${ASCEND_CANN_PACKAGE_PATH}/tools/tikicpulib/lib64)
set(ASCCOMM_CPUDEBUG_LIB64_DIR ${ASCEND_CANN_PACKAGE_PATH}/tools/cpudebug/lib64)

find_package(GTest QUIET)
if(TARGET GTest::gtest)
    set(ASCCOMM_GTEST_TARGET GTest::gtest)
    set(ASCCOMM_GTEST_USES_CXX11_ABI0 OFF)
else()
    add_cann_third_party(gtest)
    set(ASCCOMM_GTEST_TARGET GTest::gtest)
    set(ASCCOMM_GTEST_USES_CXX11_ABI0 ON)
endif()

# CCU UT 需要的第三方依赖
add_cann_third_party(json)

add_library(intf_llt_pub_basic INTERFACE)
target_compile_definitions(intf_llt_pub_basic INTERFACE
    CFG_BUILD_DEBUG
    $<$<BOOL:${ASCCOMM_GTEST_USES_CXX11_ABI0}>:_GLIBCXX_USE_CXX11_ABI=0>
)
target_compile_options(intf_llt_pub_basic INTERFACE
    -g
    -w
    $<$<BOOL:${ENABLE_GCOV}>:-fprofile-arcs -ftest-coverage>
    -fPIC
    -pipe
    -Werror
)
target_link_options(intf_llt_pub_basic INTERFACE
    $<$<BOOL:${ENABLE_GCOV}>:-fprofile-arcs -ftest-coverage>
)
target_link_libraries(intf_llt_pub_basic INTERFACE
    ${ASCCOMM_GTEST_TARGET}
    pthread
    $<$<BOOL:${ENABLE_GCOV}>:-lgcov>
)

find_package(Python3 COMPONENTS Interpreter QUIET)
add_library(kernel_tiling INTERFACE)
target_include_directories(kernel_tiling INTERFACE ${CMAKE_BINARY_DIR})
set(KERNEL_TILING_FILE ${CMAKE_BINARY_DIR}/kernel_tiling/kernel_tiling.h)
set(GEN_TILING_SCRIPT ${ASCENDC_DIR}/impl/adv_api/cmake/scripts/gen_kernel_tiling_data_def.py)
set(TILING_DEF_DIR ${ASCENDC_DIR}/include/adv_api)
if(Python3_Interpreter_FOUND AND EXISTS "${GEN_TILING_SCRIPT}" AND EXISTS "${TILING_DEF_DIR}")
    add_custom_command(
        OUTPUT ${KERNEL_TILING_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/kernel_tiling
        COMMAND ${Python3_EXECUTABLE} ${GEN_TILING_SCRIPT} ${TILING_DEF_DIR} ${KERNEL_TILING_FILE}
        DEPENDS ${GEN_TILING_SCRIPT} ${TILING_DEF_DIR}
        COMMENT "Generating kernel tiling configuration header"
    )
else()
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/kernel_tiling)
    file(WRITE ${KERNEL_TILING_FILE} "#pragma once\n")
endif()
add_custom_target(generate_kernel_headers ALL DEPENDS ${KERNEL_TILING_FILE})
add_dependencies(kernel_tiling generate_kernel_headers)

function(run_llt_test)
    cmake_parse_arguments(LLT "" "TARGET;TASK_NUM;ENV_FILE" "FILTERS;RUNTIME_DIRS" ${ARGN})

    string(REPLACE "," ";" TARGET_LIST "${TEST_MOD}")
    list(REMOVE_ITEM TARGET_LIST "")
    if(NOT "${TEST_MOD}" STREQUAL "all" AND NOT ${LLT_TARGET} IN_LIST TARGET_LIST)
        return()
    endif()

    if(ENABLE_GCOV)
        set(_collect_coverage_data_target collect_coverage_data)

        get_filename_component(_build_parent_dir ${CMAKE_BINARY_DIR} DIRECTORY)
        get_filename_component(_pkg_path "${ASCEND_CANN_PACKAGE_PATH}/../" REALPATH)
        set(_cov_report ${CMAKE_BINARY_DIR}/cov_report)
        set(_cov_html ${_cov_report})
        set(_cov_data ${_cov_report}/coverage.info)

        if(NOT TARGET ${_collect_coverage_data_target})
            add_custom_target(${_collect_coverage_data_target} ALL
                COMMAND bash ${ASCCOMM_COVERAGE_SCRIPT} ${_build_parent_dir} ${_cov_data} ${_cov_html} ${_pkg_path}
                COMMENT "Run collect coverage data"
            )
        endif()

        add_dependencies(${_collect_coverage_data_target} ${LLT_TARGET})
    endif()

    if(NOT ASCCOMM_UT_RUN_AFTER_BUILD)
        return()
    endif()

    if(LLT_ENV_FILE)
        set(ENV_PREFIX . ${LLT_ENV_FILE} &&)
    else()
        set(ENV_PREFIX "")
    endif()

    set(_llt_runtime_dirs ${LLT_RUNTIME_DIRS})
    string(REPLACE ";" ":" _llt_ld_library_path "${_llt_runtime_dirs}")

    if(LLT_FILTERS)
        foreach(test_filter ${LLT_FILTERS})
            add_custom_command(
                TARGET ${LLT_TARGET} POST_BUILD
                COMMAND ${ENV_PREFIX} ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${_llt_ld_library_path}:$ENV{LD_LIBRARY_PATH}" $<TARGET_FILE:${LLT_TARGET}> --gtest_filter=${test_filter}
                COMMENT "Run ${LLT_TARGET} ${test_filter}"
            )
        endforeach()
        return()
    endif()

    add_custom_command(
        TARGET ${LLT_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LLT_WORK_DIR}
        COMMAND ${ENV_PREFIX} ${CMAKE_COMMAND} -E chdir ${LLT_WORK_DIR} ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${_llt_ld_library_path}:$ENV{LD_LIBRARY_PATH}" $<TARGET_FILE:${LLT_TARGET}>
        COMMENT "Run ${LLT_TARGET}"
    )
endfunction()

set(ASCCOMM_COVERAGE_SCRIPT ${ASCCOMM_ROOT}/scripts/generate_cpp_cov.sh)

function(asccomm_add_product_definitions target_name product_type)
    target_compile_definitions(${target_name} PRIVATE
        UT_TEST
        ASCENDC_OOM=1
        ASCENDC_DUMP=0
        __disable_kernel_type_autoinfer__=
        $<$<STREQUAL:${product_type},ascend310p>:__NPU_ARCH__=2002;__DAV_M200__>
        $<$<STREQUAL:${product_type},ascend610>:__NPU_ARCH__=2002;__DAV_M200__>
        $<$<STREQUAL:${product_type},ascend910B1_AIC>:__NPU_ARCH__=2201;__DAV_C220__;__DAV_C220_CUBE__;__DAV_CUBE__>
        $<$<STREQUAL:${product_type},ascend910B1_AIV>:__NPU_ARCH__=2201;__DAV_C220__;__DAV_C220_VEC__;__DAV_VEC__>
        $<$<STREQUAL:${product_type},ascend950pr_9599_AIC>:__NPU_ARCH__=3510;__DAV_C310__;__DAV_C310_CUBE__>
        $<$<STREQUAL:${product_type},ascend950pr_9599_AIV>:__NPU_ARCH__=3510;__DAV_C310__;__DAV_C310_VEC__>
        $<$<STREQUAL:${product_type},ascend310B1>:__NPU_ARCH__=3002;__DAV_M300__>
    )
endfunction()

# # add_subdirectory(aicore/hcomm)

# --- CCU UT 变量定义 ---
# 自动探测 hcomm 源码树（与根 CMakeLists.txt 逻辑一致）
if(NOT ASCCOMM_HCOMM_SOURCE_DIR)
    foreach(_candidate
        "${ASCCOMM_ROOT}/../hcomm"
        "${CANN_3RD_LIB_PATH}/hcomm")
        if(EXISTS "${_candidate}/CMakeLists.txt")
            get_filename_component(ASCCOMM_HCOMM_SOURCE_DIR "${_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()
if(NOT ASCCOMM_HCOMM_SOURCE_DIR)
    message(FATAL_ERROR "ASCCOMM_HCOMM_SOURCE_DIR not found, please ensure hcomm repo exists at ${ASCCOMM_ROOT}/../hcomm or set it manually")
endif()
message(STATUS "CCU UT: ASCOMM_HCOMM_SOURCE_DIR = ${ASCCOMM_HCOMM_SOURCE_DIR}")

# hcomm 遗留变量别名（ccu 测试 CMakeLists 中引用）
set(HCCL_CODE_ROOT "${ASCCOMM_HCOMM_SOURCE_DIR}")
set(HCOMM_DIR "${ASCCOMM_HCOMM_SOURCE_DIR}")

# CCU UT 公共头文件目录（混合策略：asc-comm 本仓 + hcomm 引用 + 测试桩）
set(UT_COMMON_INCLUDE_LIST
    # asc-comm 本仓 ccu 数据面头文件
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/interface
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/sync
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/translator
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/pub_inc

    # hcomm 仓引用（legacy 异常、公共头等少量依赖）
    # 关键：ascend910/pub_inc 必须在 ascend950/common 之前（log.h 冲突）
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc/inner
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc/new
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/platform/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/common/src
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/hcom
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/exception
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/utils
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/types
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/unified_platform/pub_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/unified_platform
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_device
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_device/ccu_comp
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_device/ccu_comp/ccu_channel
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_transport
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_kernel
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_instance
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/endpoint_pairs/channels
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/endpoint_pairs/channels/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/inc/network
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/primitives/api_c_adpt
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/southbound_adpt
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/context
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/interface
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/arithmetic
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/control
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/data
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/loop
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/sync
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation/reps/translator
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_microcode
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/pub_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/pkg_inc/runtime
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/include/mmpa
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base

    # 测试桩目录（ccu 测试在 tests/ccu，与 tests/ut 平级）
    ${ASCCOMM_UT_ROOT}/../ccu/stub

    # CANN 外部依赖头
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

add_subdirectory(../ccu/hcomm ccu_hcomm)
