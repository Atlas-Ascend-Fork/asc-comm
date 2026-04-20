# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

unset(hcomm_utils_FOUND CACHE)
unset(TLS_ADP_LIBRARY CACHE)

if (AARCH_MODE)
    set(HCOMM_UTILS_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(HCOMM_UTILS_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

set(HCOMM_UTILS_VERSION "9.0.0")
set(HCOMM_UTILS_FILE "cann-hcomm-utils_${HCOMM_UTILS_VERSION}_linux-${HCOMM_UTILS_ARCH}.tar.gz")
set(HCOMM_UTILS_URL "https://ascend-cann.obs.cn-north-4.myhuaweicloud.com/CANN/20260330_newest/${HCOMM_UTILS_FILE}")
set(HCOMM_UTILS_PKG_PATH ${CANN_3RD_LIB_PATH}/${HCOMM_UTILS_FILE})
set(HCOMM_UTILS_INSTALL_PATH ${CANN_3RD_LIB_PATH}/hcomm_utils)
set(INSTALL_LIBRARY_DIR hcomm/lib64)

# 查找目录下是否已经安装，避免重复编译安装
message(STATUS "[ThirdParty] HCOMM_UTILS_INSTALL_PATH=${HCOMM_UTILS_INSTALL_PATH}")
find_library(TLS_ADP_LIBRARY
    NAMES libtls_adp.so
    PATH_SUFFIXES lib
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH
    PATHS ${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}
)

# 是否找到 hcomm_legacy 的库文件
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hcomm_utils
    FOUND_VAR
    hcomm_utils_FOUND 
    REQUIRED_VARS
    TLS_ADP_LIBRARY
)
message(STATUS "[ThirdParty] Found hcomm_utils: ${hcomm_utils_FOUND}")

if(hcomm_utils_FOUND AND NOT FORCE_REBUILD_CANN_3RD)
    message(STATUS "[ThirdParty] hcomm_utils found in ${HCOMM_UTILS_INSTALL_PATH}, and not force rebuild cann third_party")
else()
    file(GLOB HCOMM_UTILS_PKG
        LIST_DIRECTORIES True
        ${CANN_UTILS_LIB_PATH}/cann-hcomm-utils_*_linux-${HCOMM_UTILS_ARCH}.tar.gz
    )
    if(EXISTS ${HCOMM_UTILS_PKG})
        # 离线编译场景，优先使用已下载的包（忽略版本号）
        message(STATUS "[ThirdParty] Found local hcomm_utils package: ${HCOMM_UTILS_PKG}")
        set(HCOMM_UTILS_PKG_PATH ${HCOMM_UTILS_PKG})
    endif()

    if(EXISTS ${HCOMM_UTILS_PKG_PATH})
        # 离线编译场景，优先使用已下载的包
        message(STATUS "[ThirdParty] Found local hcomm_utils package: ${HCOMM_UTILS_PKG_PATH}")
        set(HCOMM_UTILS_PROJECT_URL ${HCOMM_UTILS_PKG_PATH})
    else()
        # 下载并解压
        message(STATUS "[ThirdParty] Downloading hcomm_utils from ${HCOMM_UTILS_URL}")
        set(HCOMM_UTILS_PROJECT_URL ${HCOMM_UTILS_URL})
    endif()

    if(EXISTS ${HCOMM_UTILS_PKG})
        # 忽略版本号，不校验哈希值
        set(HCOMM_UTILS_URL_HASH "")
    elseif(HCOMM_UTILS_ARCH MATCHES "aarch64|ARM64|arm64")
        set(HCOMM_UTILS_URL_HASH "SHA256=bf36523b855842f7d391eeaaa23ed7ffa066deb591e531e8ec6fa0c35d422aff")
    else()
        set(HCOMM_UTILS_URL_HASH "SHA256=7b5eea6297b733922b0d6805f02190a6a5fe9f0e2ff969b76375875a7db57746")
    endif()

    include(ExternalProject)
    ExternalProject_Add(hcomm_utils
        URL ${HCOMM_UTILS_PROJECT_URL}
        URL_HASH ${HCOMM_UTILS_URL_HASH}
        TLS_VERIFY OFF
        DOWNLOAD_NO_EXTRACT TRUE
        DOWNLOAD_NO_PROGRESS TRUE
        DOWNLOAD_DIR ${CANN_3RD_LIB_PATH}
        SOURCE_DIR ${HCOMM_UTILS_INSTALL_PATH}
        CONFIGURE_COMMAND tar -xf ${HCOMM_UTILS_PKG_PATH} --overwrite --strip-components=2 -C <SOURCE_DIR>
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
    )
endif()

if(NOT EXISTS ${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/include)
    file(MAKE_DIRECTORY "${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/include")
endif()

# 创建导入的目标
add_library(ascend_kms SHARED IMPORTED)
add_dependencies(ascend_kms hcomm_utils)

set_target_properties(ascend_kms PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/include"
    IMPORTED_LOCATION "${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/lib/libascend_kms.so"
)

if(${PRODUCT_SIDE} STREQUAL "device")
    install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/lib/libascend_kms.so
        DESTINATION ${INSTALL_LIBRARY_DIR} OPTIONAL
    )
endif()

add_library(tls_adp SHARED IMPORTED)
add_dependencies(tls_adp hcomm_utils)

set_target_properties(tls_adp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/include"
    IMPORTED_LOCATION "${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/lib/libtls_adp.so"
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/lib/libtls_adp.so
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

add_library(hccl_legacy SHARED IMPORTED)
add_dependencies(hccl_legacy hcomm_utils)

set_target_properties(hccl_legacy PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${HCOMM_UTILS_INSTALL_PATH}/${PRODUCT_SIDE}/include"
    IMPORTED_LOCATION "${HCOMM_UTILS_INSTALL_PATH}/host/lib/libhccl_legacy.so"  # 该动态库只有 host 有
)

# 安装库文件到指定目录
install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/libhccl_legacy.so
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/MemSet_dynamic_AtomicAddrClean_1_ascend310p3.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/MemSet_dynamic_AtomicAddrClean_1_ascend910.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/MemSet_dynamic_AtomicAddrClean_1_ascend910b.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_float16_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_float16_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_float32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_int32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_int32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_int64_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_int64_v81.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_int8_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_add_int8_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_float16_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_float16_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_float32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_float32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_int32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_int32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_int64_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_int64_v81.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_int8_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_maximum_int8_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_float16_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_float16_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_float32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_float32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_int32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_int32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_int64_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_int64_v81.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_int8_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_minimum_int8_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float16_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float16_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float16_v81_910B1.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float16_v81_910B2.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float16_v81_910B3.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float16_v81_910B4.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float32_v81_910B1.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float32_v81_910B2.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float32_v81_910B3.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_float32_v81_910B4.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int32_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int32_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int32_v81_910B1.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int32_v81_910B2.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int32_v81_910B3.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int32_v81_910B4.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int64_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int64_v81.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int8_v51.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int8_v80.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int8_v81_910B1.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int8_v81_910B2.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int8_v81_910B3.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)

install(FILES  ${HCOMM_UTILS_INSTALL_PATH}/host/lib/dynamic_mul_int8_v81_910B4.o
    DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)
