# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# This file is a part of the CANN Open Software.
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See LICENSE in the root of
# the software repository for the full text of the License.
# ----------------------------------------------------------------------------

include_guard(GLOBAL)

unset(protobuf_FOUND CACHE)
unset(PROTOBUF_INCLUDE_DIR CACHE)
unset(PROTOBUF_LIBRARY CACHE)
unset(PROTOBUF_PROTOC_EXECUTABLE CACHE)

set(PROTOBUF_NAME "protobuf")
set(PROTOBUF_FILE "protobuf-25.1.tar.gz")
set(PROTOBUF_URL "https://gitcode.com/cann-src-third-party/protobuf/releases/download/v25.1/${PROTOBUF_FILE}")
set(PROTOBUF_PKG_PATH ${CANN_3RD_LIB_PATH}/${PROTOBUF_FILE})
set(PROTOBUF_INSTALL_PATH ${CANN_3RD_LIB_PATH}/protobuf)

# 查找目录下是否已经安装，避免重复编译安装
message(STATUS "[ThirdParty] PROTOBUF_INSTALL_PATH=${PROTOBUF_INSTALL_PATH}")
find_path(PROTOBUF_INCLUDE_DIR
    NAMES google/protobuf/message.h
    PATH_SUFFIXES include
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH
    PATHS ${PROTOBUF_INSTALL_PATH}
)
find_library(PROTOBUF_LIBRARY
    NAMES protobuf libprotobuf
    PATH_SUFFIXES lib lib64
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH
    PATHS ${PROTOBUF_INSTALL_PATH}
)
find_program(PROTOBUF_PROTOC_EXECUTABLE
    NAMES protoc
    PATH_SUFFIXES bin
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH
    PATHS ${PROTOBUF_INSTALL_PATH}
)

# 是否全部找到 protobuf 的头文件、链接库
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(protobuf
    FOUND_VAR
    protobuf_FOUND
    REQUIRED_VARS
    PROTOBUF_INCLUDE_DIR
    PROTOBUF_LIBRARY
    PROTOBUF_PROTOC_EXECUTABLE
)
message(STATUS "[ThirdParty] Found Protobuf: ${protobuf_FOUND}")

if(protobuf_FOUND AND NOT FORCE_REBUILD_CANN_3RD)
    message(STATUS "[ThirdParty] Protobuf found in ${PROTOBUF_INSTALL_PATH}, and not force rebuild cann third_party")
else()
    # 1. 下载 abseil-cpp 包
    include(${CMAKE_CURRENT_LIST_DIR}/abseil-cpp.cmake)

    # 2. 编译安装 protobuf
    if(EXISTS ${PROTOBUF_PKG_PATH})
        # 离线编译场景，优先使用已下载的包
        message(STATUS "[ThirdParty] Found local protobuf package: ${PROTOBUF_PKG_PATH}")
        set(PROTOBUF_PROJECT_URL ${PROTOBUF_PKG_PATH})
    else()
        # 下载 protobuf 包
        message(STATUS "[ThirdParty] Downloading protobuf from ${PROTOBUF_URL}")
        set(PROTOBUF_PROJECT_URL ${PROTOBUF_URL})
    endif()

    set(PROTOBUF_OPTS
        -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0"
        -DCMAKE_CXX_STANDARD=14
        -DCMAKE_CXX_STANDARD_REQUIRED=ON
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${PROTOBUF_INSTALL_PATH}
        -DCMAKE_INSTALL_LIBDIR=lib64
        -DBUILD_SHARED_LIBS=ON
        -Dprotobuf_WITH_ZLIB=OFF
        -Dprotobuf_BUILD_TESTS=OFF
    )

    include(ExternalProject)
    ExternalProject_Add(third_party_protobuf
        URL ${PROTOBUF_PROJECT_URL}
        URL_HASH SHA256=9bd87b8280ef720d3240514f884e56a712f2218f0d693b48050c836028940a42
        DOWNLOAD_DIR ${CANN_3RD_LIB_PATH}
        DOWNLOAD_NO_PROGRESS TRUE
        PATCH_COMMAND tar -zxf ${ABSEIL_PKG_PATH} --strip-components 1 -C <SOURCE_DIR>/third_party/abseil-cpp
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} ${PROTOBUF_OPTS} <SOURCE_DIR>
        BUILD_COMMAND $(MAKE)
        INSTALL_COMMAND $(MAKE) install
        DEPENDS third_party_abseil_cpp  # 依赖 abseil-cpp 库
    )
endif()

# 创建导入的库目标
add_library(protobuf SHARED IMPORTED)
add_dependencies(protobuf third_party_protobuf)

if(NOT EXISTS ${PROTOBUF_INSTALL_PATH}/include)
    file(MAKE_DIRECTORY "${PROTOBUF_INSTALL_PATH}/include")
endif()

set_target_properties(protobuf PROPERTIES
    IMPORTED_LOCATION ${PROTOBUF_INSTALL_PATH}/lib64/libprotobuf.so
    INTERFACE_INCLUDE_DIRECTORIES ${PROTOBUF_INSTALL_PATH}/include
)
