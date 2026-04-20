# ------------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ------------------------------------------------------------------------------------------------------------

message("Build third party library urma")
set(URMA_NAME "urma")
set(URMA_BUILD_PATH "${CMAKE_BINARY_DIR}")
set(URMA_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/../ubengine/ssapi/")
set(URMA_INSTALL_PATHS "${CMAKE_CURRENT_BINARY_DIR}/${URMA_NAME}/build")
set(URMA_SRC_DIR ${URMA_BUILD_PATH}/urma)
set(URMA_SRC_DIRS
    "${URMA_SEARCH_PATHS}/kernelspace/urma/code/lib/urma/include"
    "${URMA_SEARCH_PATHS}/kernelspace/urma/code/include"
    "${URMA_SEARCH_PATHS}/userspace/udma/src/urma/hw/udma/include"
)
set(URMA_INCLUDE_DIR ${URMA_BUILD_PATH}/urma)
file(MAKE_DIRECTORY "${URMA_INCLUDE_DIR}")

if(EXISTS ${URMA_SEARCH_PATHS})
    foreach(SRC_DIR ${URMA_SRC_DIRS})
        if(EXISTS "${SRC_DIR}")
            file(COPY "${SRC_DIR}/" DESTINATION "${URMA_INCLUDE_DIR}")
        endif()
    endforeach()
    message(STATUS "Successfully copied ${URMA_SEARCH_PATHS} to ${URMA_BUILD_PATH}.")
else()
    set(URMA_URL "")
    set(URMA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/platform/hccp/external_depends/ubengine)
    message(STATUS "downloading ${URMA_URL} to ${URMA_SRC_DIR}")
endif()