# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# the CANN Open Software License Agreement Version 2.0 (the "License").
# ----------------------------------------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH
    "${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake/asc_modules")
find_package(ASC REQUIRED)
enable_language(ASC)

set(ASCCOMM_DFX_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_DFX_ROOT}/../../../../ ABSOLUTE)

set_source_files_properties(hcomm_dfx.cpp PROPERTIES LANGUAGE ASC)
ascendc_library(mc2_client_dfx SHARED hcomm_dfx.cpp)
set_target_properties(mc2_client_dfx PROPERTIES OUTPUT_NAME mc2_client_dfx)
target_include_directories(mc2_client_dfx PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/aicore
    ${ASCCOMM_ROOT}/src
    ${ASCCOMM_CANN_ARCH_DIR}/asc/include
    ${ASCCOMM_CANN_ARCH_DIR}/include
)
target_compile_options(mc2_client_dfx PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=dav-3510>
)
