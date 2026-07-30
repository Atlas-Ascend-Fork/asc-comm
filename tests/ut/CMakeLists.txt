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

# Keep one installed-package-shaped header tree for all UT modules. The source
# headers use relative paths that are part of the toolkit package layout.
set(ASCCOMM_UT_STAGE_ROOT "${CMAKE_BINARY_DIR}/package_layout")
set(ASCCOMM_UT_STAGE_INCLUDE_DIR "${ASCCOMM_UT_STAGE_ROOT}/asc/include/comm_api/aicore")
set(ASCCOMM_UT_STAGE_DETAIL_DIR "${ASCCOMM_UT_STAGE_ROOT}/asc/impl/adv_api/detail")
set(ASCCOMM_UT_STAGE_COMM_API_IMPL_DIR "${ASCCOMM_UT_STAGE_ROOT}/asc/impl/comm_api/aicore")
set(ASCCOMM_UT_STAGE_STAMP "${ASCCOMM_UT_STAGE_ROOT}/.header_layout.stamp")

file(GLOB_RECURSE ASCCOMM_UT_LAYOUT_INPUTS CONFIGURE_DEPENDS
    "${ASCCOMM_ROOT}/include/aicore/*"
    "${ASCCOMM_ROOT}/src/aicore/*"
)

add_custom_command(
    OUTPUT ${ASCCOMM_UT_STAGE_STAMP}
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASCCOMM_UT_STAGE_ROOT}/asc"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASCCOMM_UT_STAGE_INCLUDE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASCCOMM_UT_STAGE_DETAIL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ASCCOMM_UT_STAGE_COMM_API_IMPL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/include/aicore/hcomm"
        "${ASCCOMM_UT_STAGE_INCLUDE_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/include/aicore/ain"
        "${ASCCOMM_UT_STAGE_INCLUDE_DIR}/ain"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/hcomm"
        "${ASCCOMM_UT_STAGE_DETAIL_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/hcomm"
        "${ASCCOMM_UT_STAGE_COMM_API_IMPL_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/ain"
        "${ASCCOMM_UT_STAGE_COMM_API_IMPL_DIR}/ain"
    COMMAND ${CMAKE_COMMAND} -E touch ${ASCCOMM_UT_STAGE_STAMP}
    DEPENDS ${ASCCOMM_UT_LAYOUT_INPUTS}
    COMMENT "Preparing shared Ascend package-shaped headers for UT"
)

add_custom_target(prepare_asccomm_ut_header_layout
    DEPENDS ${ASCCOMM_UT_STAGE_STAMP}
)

set(ASCCOMM_COVERAGE_SCRIPT ${ASCCOMM_ROOT}/scripts/generate_cpp_cov.sh)

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

add_library(asccomm_ccu_host_launch STATIC
    ${ASCCOMM_ROOT}/src/ccu/ccu_host_launch.cpp
)
target_include_directories(asccomm_ccu_host_launch
    PUBLIC
        ${ASCCOMM_ROOT}/include
    PRIVATE
        ${ASCEND_CANN_ARCH_DIR}/include
        ${ASCEND_CANN_ARCH_DIR}/include/hcomm/ccu
        ${ASCEND_CANN_ARCH_DIR}/include/hccl
        ${ASCEND_CANN_ARCH_DIR}/pkg_inc
        ${ASCEND_CANN_ARCH_DIR}/pkg_inc/runtime
)
target_compile_features(asccomm_ccu_host_launch PUBLIC cxx_std_17)
target_compile_options(asccomm_ccu_host_launch PRIVATE
    -fPIC
    -Wall
    -Werror
)

add_executable(asccomm_ut_ccu_host_launch
    ${ASCCOMM_UT_ROOT}/ccu/test_ccu_host_launch.cpp
)
target_include_directories(asccomm_ut_ccu_host_launch PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCEND_CANN_ARCH_DIR}/include
    ${ASCEND_CANN_ARCH_DIR}/include/hcomm/ccu
    ${ASCEND_CANN_ARCH_DIR}/include/hccl
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/runtime
)
target_compile_features(asccomm_ut_ccu_host_launch PRIVATE cxx_std_17)
target_compile_options(asccomm_ut_ccu_host_launch PRIVATE
    -fPIC
    -Wall
    -Werror
)
target_link_libraries(asccomm_ut_ccu_host_launch PRIVATE
    intf_llt_pub_basic
    asccomm_ccu_host_launch
)

run_llt_test(
    TARGET asccomm_ut_ccu_host_launch
    TASK_NUM 1
    FILTERS TestCcuHostLaunch.*
)

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

add_subdirectory(aicore/hcomm)
add_subdirectory(aicore/ain)
