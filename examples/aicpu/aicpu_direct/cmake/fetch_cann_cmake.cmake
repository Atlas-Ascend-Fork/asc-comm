# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# 引入CANN/cmake库

if(NOT PROJECT_SOURCE_DIR)
    # Allow callers to point at an already unpacked cann-cmake checkout.  This is
    # useful for offline builds and avoids coupling the source tree to a build
    # directory from another project.
    if(CANN_CMAKE_SOURCE_DIR)
        get_filename_component(CANN_CMAKE_SOURCE_DIR "${CANN_CMAKE_SOURCE_DIR}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${CANN_CMAKE_SOURCE_DIR}" OR
           NOT EXISTS "${CANN_CMAKE_SOURCE_DIR}/function/prepare.cmake")
            message(FATAL_ERROR
                "CANN_CMAKE_SOURCE_DIR must contain function/prepare.cmake: "
                "${CANN_CMAKE_SOURCE_DIR}")
        endif()
        include("${CANN_CMAKE_SOURCE_DIR}/function/prepare.cmake")
    elseif(CANN_3RD_LIB_PATH AND IS_DIRECTORY "${CANN_3RD_LIB_PATH}/cann-cmake")
        include("${CANN_3RD_LIB_PATH}/cann-cmake/function/prepare.cmake")
    else()
        include(FetchContent)

        set(CANN_CMAKE_TAG "master-044")
        if(CANN_3RD_LIB_PATH AND EXISTS "${CANN_3RD_LIB_PATH}/cmake-${CANN_CMAKE_TAG}.tar.gz")
            FetchContent_Declare(
                cann-cmake
                URL "${CANN_3RD_LIB_PATH}/cmake-${CANN_CMAKE_TAG}.tar.gz"
                URL_HASH SHA256=2d8827b33327978c7b49d09e3b1cfbd891b41b7b5123c290d41534d29a625056
            )
        else()
            FetchContent_Declare(
                cann-cmake
                GIT_REPOSITORY https://gitcode.com/cann/cmake.git
                GIT_TAG        ${CANN_CMAKE_TAG}
                GIT_SHALLOW    TRUE
            )
        endif()
        FetchContent_GetProperties(cann-cmake)
        if(NOT cann-cmake_POPULATED)
            FetchContent_Populate(cann-cmake)
        endif()
        include("${cann-cmake_SOURCE_DIR}/function/prepare.cmake")
    endif()
endif()
