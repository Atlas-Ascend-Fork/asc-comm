# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
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
    $<$<COMPILE_LANGUAGE:ASC>:-iquote${OPS_TENSOR_ROOT}/include/tensor_api/include>
    $<$<COMPILE_LANGUAGE:ASC>:-iquote${OPS_TENSOR_ROOT}/include/tensor_api>
    $<$<COMPILE_LANGUAGE:ASC>:-include${OPS_TENSOR_ROOT}/examples/common/cce_pipe_stub.h>)
