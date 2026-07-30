# -----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)
find_package(ASC REQUIRED)
project(asccomm_ccu_allgather_mesh1d_mem2mem LANGUAGES CXX ASC)

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH $ENV{ASCEND_HOME_PATH})
    elseif(DEFINED ENV{ASCEND_OPP_PATH})
        get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
    else()
        message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH or ASCEND_HOME_PATH must be set")
    endif()
endif()

add_executable(ccu_allgather_mesh1d_mem2mem
    testcase/main.asc
    op_kernel_ccu/ccu_kernel.cc
    op_host/data_utils.cc
)

target_include_directories(ccu_allgather_mesh1d_mem2mem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/op_host
    ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_ccu
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/include/ccu/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/include/ccu/hcomm/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm/ccu
)

target_compile_features(ccu_allgather_mesh1d_mem2mem PRIVATE cxx_std_17)
target_compile_options(ccu_allgather_mesh1d_mem2mem PRIVATE
    -Werror
    -fstack-protector-strong
    -fPIE
    -O2
)
set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "CMAKE_ASC_ARCHITECTURES, e.g. dav-3510")
target_compile_options(ccu_allgather_mesh1d_mem2mem PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

target_compile_definitions(ccu_allgather_mesh1d_mem2mem PRIVATE
    HOST_COMPILE
    _GLIBCXX_USE_CXX11_ABI=0
)
target_link_options(ccu_allgather_mesh1d_mem2mem PRIVATE
    -pie
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--copy-dt-needed-entries
)
target_link_directories(ccu_allgather_mesh1d_mem2mem PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)
target_link_libraries(ccu_allgather_mesh1d_mem2mem PRIVATE
    hcomm
    asccomm_ccu_dataplane
    c_sec
    ascendcl
    acl_rt
    ascendc_runtime
    pthread
)
