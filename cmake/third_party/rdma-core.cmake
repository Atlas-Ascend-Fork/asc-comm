# ------------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ------------------------------------------------------------------------------------------------------------

function(download_file URL FILE_PATH EXPECTED_HASH)
    if(NOT EXISTS "${FILE_PATH}")
        get_filename_component(FILENAME "${FILE_PATH}" NAME)
        message(STATUS "Downloading ${FILENAME}...")

        file(DOWNLOAD
            ${URL}
            "${FILE_PATH}"
            EXPECTED_HASH ${EXPECTED_HASH}
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
        )

        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            file(REMOVE "${FILE_PATH}")
            message(FATAL_ERROR "Failed to download ${FILENAME}: ${ERROR_MSG}")
        endif()
    else()
        get_filename_component(FILENAME "${FILE_PATH}" NAME)
        message(STATUS "${FILENAME} already exists, skipping download")
    endif()
endfunction()

message("Build third party library rdma-core")
set(RDMA_CORE_NAME "rdma-core")
set(RDMA_CORE_VERSION "42.7")
set(RDMA_CORE_URL "https://gitcode.com/cann-src-third-party/rdma-core/releases/download/v42.7-h2/rdma-core-42.7.tar.gz")
set(RDMA_CORE_PATCH_URL "https://gitcode.com/cann-src-third-party/rdma-core/releases/download/v42.7-h2/rdma-core-42.7.patch")
set(ROOT_BUILD_PATH "${CMAKE_SOURCE_DIR}/third_party")
set(RDMA_CORE_SEARCH_PATHS "${CANN_3RD_LIB_PATH}/${RDMA_CORE_NAME}")
set(RDMA_CORE_ROOT_DIR ${ROOT_BUILD_PATH}/rdma-core)
set(RDMA_CORE_ARCHIVE rdma-core-${RDMA_CORE_VERSION}.tar.gz)
set(RDMA_CORE_PATCH rdma-core-${RDMA_CORE_VERSION}.patch)
set(RDMA_CORE_ARCHIVE_PATH "${ROOT_BUILD_PATH}/${RDMA_CORE_ARCHIVE}")
set(RDMA_CORE_PATCH_PATH "${ROOT_BUILD_PATH}/${RDMA_CORE_PATCH}")
set(RDMA_CORE_SRC_DIR "${ROOT_BUILD_PATH}/rdma-core-${RDMA_CORE_VERSION}")
set(RDMA_CORE_BUILD_DIR "${ROOT_BUILD_PATH}/rdma-core-build")
file(MAKE_DIRECTORY "${ROOT_BUILD_PATH}")

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(DEFINED CANN_3RD_LIB_PATH AND CANN_3RD_LIB_PATH)
    set(RDMA_CORE_ARCHIVE_PATH "${CANN_3RD_LIB_PATH}/${RDMA_CORE_ARCHIVE}")
    set(RDMA_CORE_PATCH_PATH "${CANN_3RD_LIB_PATH}/${RDMA_CORE_PATCH}")
endif()
if(EXISTS ${RDMA_CORE_SEARCH_PATHS})
    set(RDMA_CORE_SRC_DIR ${RDMA_CORE_SEARCH_PATHS})
    message(STATUS "Successfully copied ${RDMA_CORE_SEARCH_PATHS} to ${ROOT_BUILD_PATH}.")
else()
    if(NOT EXISTS "${RDMA_CORE_SRC_DIR}/CMakeLists.txt")
        message(STATUS "rdma-core source not found, starting download and setup...")

        # -------------------------- downloading src --------------------------
        download_file(${RDMA_CORE_URL} ${RDMA_CORE_ARCHIVE_PATH} SHA256=aa935de1fcd07c42f7237b0284b5697b1ace2a64f2bcfca3893185bc91b8c74d)

        # -------------------------- dowloading patch --------------------------
        download_file(${RDMA_CORE_PATCH_URL} ${RDMA_CORE_PATCH_PATH} SHA256=54ca56b3b68bc465a78dd5839cd7110610745c7152a1dc3a72b265deeebb905f)

        # -------------------------- Extracting --------------------------
        message(STATUS "Extracting rdma-core...")
        execute_process(
            COMMAND tar -zxf "${RDMA_CORE_ARCHIVE_PATH}" -C "${ROOT_BUILD_PATH}"
            WORKING_DIRECTORY "${ROOT_BUILD_PATH}"
            RESULT_VARIABLE TAR_RESULT
        )

        if(NOT TAR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract rdma-core archive")
        endif()

        # -------------------------- patching --------------------------
        message(STATUS "Applying rdma-core patch...")
        execute_process(
            COMMAND patch -p1 -i "${RDMA_CORE_PATCH_PATH}"
            WORKING_DIRECTORY "${RDMA_CORE_SRC_DIR}"
            RESULT_VARIABLE PATCH_RESULT
        )
        if(NOT PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply rdma-core patch")
        endif()

    else()
        message(STATUS "rdma-core source already prepared, skipping download/extract/patch")
    endif()
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -S "${RDMA_CORE_SRC_DIR}"
        -B "${RDMA_CORE_BUILD_DIR}"
        -DNO_MAN_PAGES=1
        -DENABLE_RESOLVE_NEIGH=0
        -DCMAKE_SKIP_RPATH=True
        -DNO_PYVERBS=1
)

execute_process(
    COMMAND ${CMAKE_COMMAND}
        --build "${RDMA_CORE_BUILD_DIR}"
        --target kern-abi
)

set(RDMA_CORE_INCLUDE_DIR ${RDMA_CORE_BUILD_DIR}/include)