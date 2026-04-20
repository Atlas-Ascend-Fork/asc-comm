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

set(ABSEIL_NAME "abseil-cpp")
set(ABSEIL_FILE "abseil-cpp-20250127.0.tar.gz")
set(ABSEIL_URL "https://gitcode.com/cann-src-third-party/abseil-cpp/releases/download/20250127.0/${ABSEIL_FILE}")
set(ABSEIL_PKG_PATH ${CANN_3RD_LIB_PATH}/${ABSEIL_FILE})

if(EXISTS ${ABSEIL_PKG_PATH})
    # 离线编译场景，优先使用已下载的包
    message(STATUS "[ThirdParty] Found local abseil-cpp package: ${ABSEIL_PKG_PATH}")
    set(ABSEIL_PROJECT_URL ${ABSEIL_PKG_PATH})
else()
    # 下载
    message(STATUS "[ThirdParty] Downloading ${ABSEIL_NAME} from ${ABSEIL_URL}")
    set(ABSEIL_PROJECT_URL ${ABSEIL_URL})
endif()

include(ExternalProject)
ExternalProject_Add(third_party_abseil_cpp
    URL ${ABSEIL_PROJECT_URL}
    URL_HASH SHA256=16242f394245627e508ec6bb296b433c90f8d914f73b9c026fddb905e27276e8
    DOWNLOAD_DIR ${CANN_3RD_LIB_PATH}
    DOWNLOAD_NO_PROGRESS TRUE
    DOWNLOAD_NO_EXTRACT TRUE    # 仅下载，不解压
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)
