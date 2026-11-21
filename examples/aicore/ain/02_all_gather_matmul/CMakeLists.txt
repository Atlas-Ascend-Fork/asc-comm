# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
cmake_minimum_required(VERSION 3.16)
option(AGMM_HOST_TESTS_ONLY "Build portable host checks without CANN" OFF)

if(AGMM_HOST_TESTS_ONLY)
    project(ain_agmm_host_tests LANGUAGES CXX)
    enable_testing()
    add_executable(agmm_host_tests tests/host_tests.cpp)
    target_compile_features(agmm_host_tests PRIVATE cxx_std_17)
    target_include_directories(agmm_host_tests PRIVATE . ../..)
    target_compile_options(agmm_host_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME agmm_host_tests COMMAND agmm_host_tests)
    return()
endif()

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "Ascend 950 architecture")
set(OPS_TENSOR_ROOT "" CACHE PATH "ops-tensor source with initialized include/tensor_api submodule")
foreach(dep include/blaze/gemm/block/block_mmad_matmul_basic.h
            include/tensor_api/include/tensor_api/tensor.h examples/common/cce_pipe_stub.h)
    if(NOT EXISTS "${OPS_TENSOR_ROOT}/${dep}")
        message(FATAL_ERROR "Missing ${OPS_TENSOR_ROOT}/${dep}; set OPS_TENSOR_ROOT and initialize its Tensor API submodule")
    endif()
endforeach()
find_package(ASC REQUIRED)
project(ain_all_gather_matmul LANGUAGES ASC CXX)
set(CANN_ROOT "$ENV{ASCEND_HOME_PATH}")
set(CANN_ARCH "${CMAKE_SYSTEM_PROCESSOR}-linux")
add_executable(ain_all_gather_matmul main.asc)
set_target_properties(ain_all_gather_matmul PROPERTIES LINKER_LANGUAGE ASC CXX_STANDARD 17)
target_include_directories(ain_all_gather_matmul PRIVATE
    . ../..
    "${OPS_TENSOR_ROOT}/include" "${OPS_TENSOR_ROOT}/include/blaze"
    "${OPS_TENSOR_ROOT}/include/tensor_api/include" "${OPS_TENSOR_ROOT}/include/tensor_api"
    "${CANN_ROOT}/include" "${CANN_ROOT}/${CANN_ARCH}/include"
    "${CANN_ROOT}/include/hcomm" "${CANN_ROOT}/${CANN_ARCH}/include/hcomm"
    "${CANN_ROOT}/asc/include/comm_api/aicore" "${CANN_ROOT}/asc/include/comm_api/hcomm"
    "${CANN_ROOT}/${CANN_ARCH}/asc" "${CANN_ROOT}/${CANN_ARCH}/asc/include"
    "${CANN_ROOT}/${CANN_ARCH}/asc/include/tiling"
    "${CANN_ROOT}/include/op_common" "${CANN_ROOT}/pkg_inc/op_common"
    "${CANN_ROOT}/pkg_inc/base" "${CANN_ROOT}/pkg_inc"
    "${CANN_ROOT}/asc/impl/c_api" "${CANN_ROOT}/asc/impl/utils"
    "${CANN_ROOT}/compiler/ascendc/include/highlevel_api")
target_link_directories(ain_all_gather_matmul PRIVATE "${CANN_ROOT}/lib64" "${CANN_ROOT}/${CANN_ARCH}/lib64")
target_link_libraries(ain_all_gather_matmul PRIVATE hcomm hccl ascendcl ascendc_runtime runtime platform tiling_api dl m)
target_compile_options(ain_all_gather_matmul PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:-include${OPS_TENSOR_ROOT}/examples/common/cce_pipe_stub.h>)

# 新版 CANN 在 asc/impl/c_api/reg_base_impl 下自带 c_api 实现头，ops-tensor 的
# tensor_api 子模块又 vendored 了一份同类实现，include guard 不同无法自动去重。
# 这里在 configure 阶段按文件是否存在决定是否预定义 vendored guard；旧版 CANN
# 没有这些头时保持原路径不变。
set(CANN_C_API_IMPL_ROOT "${CANN_ROOT}/asc/impl/c_api")
if(EXISTS "${CANN_C_API_IMPL_ROOT}/reg_base_impl/utils_impl.h")
    message(STATUS "AIN AGMM: CANN c_api impl headers found under ${CANN_C_API_IMPL_ROOT}, skipping vendored copies")
    target_compile_options(ain_all_gather_matmul PRIVATE
        $<$<COMPILE_LANGUAGE:ASC>:-DIMPL_C_API_C_API_INSTR_IMPL_UTILS_C_API_IMPL_UTILS_C_API_IMPL_H>
        $<$<COMPILE_LANGUAGE:ASC>:-DIMPL_C_API_INSTR_IMPL_NPU_ARCH_3510_UTILS_IMPL_UTILS_C_API_IMPL_H>)
else()
    message(STATUS "AIN AGMM: CANN c_api impl headers not found under ${CANN_C_API_IMPL_ROOT}, using vendored copies")
endif()
if(EXISTS "${CANN_C_API_IMPL_ROOT}/reg_base_impl/npu_arch_3510/vector_compute_composite_intf_impl.h")
    target_compile_options(ain_all_gather_matmul PRIVATE
        $<$<COMPILE_LANGUAGE:ASC>:-DIMPL_C_API_INSTR_IMPL_NPU_ARCH_3510_VECTOR_COMPUTE_IMPL_H>)
endif()
