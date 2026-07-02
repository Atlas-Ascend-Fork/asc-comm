# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
set(DEFAULT_BUILD_TYPE "Release")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the build type: Release/Debug" FORCE)
endif()

if(ASCEND_CANN_PACKAGE_PATH)
    set(ASCEND_CANN_PACKAGE_PATH  ${ASCEND_CANN_PACKAGE_PATH})
elseif(CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(ASCEND_CANN_PACKAGE_PATH  ${CUSTOM_ASCEND_CANN_PACKAGE_PATH})
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(ASCEND_CANN_PACKAGE_PATH  $ENV{ASCEND_HOME_PATH})
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
else()
    set(ASCEND_CANN_PACKAGE_PATH  "/usr/local/Ascend/ascend-toolkit/latest")
endif()
get_filename_component(ASCEND_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}" ABSOLUTE)
get_filename_component(ASCEND_CANN_PACKAGE_PATH_NAME "${ASCEND_CANN_PACKAGE_PATH}" NAME)
if(ASCEND_CANN_PACKAGE_PATH_NAME MATCHES "^(aarch64|arm64|x86_64)-linux$")
    get_filename_component(ASCEND_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}" DIRECTORY)
endif()
set(ASCEND_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "Ascend CANN package path" FORCE)
if(NOT ASCEND_INSTALL_PATH)
    set(ASCEND_INSTALL_PATH "${ASCEND_CANN_PACKAGE_PATH}")
endif()
get_filename_component(ASCEND_INSTALL_PATH "${ASCEND_INSTALL_PATH}" ABSOLUTE)
get_filename_component(ASCEND_INSTALL_PATH_NAME "${ASCEND_INSTALL_PATH}" NAME)
if(ASCEND_INSTALL_PATH_NAME MATCHES "^(aarch64|arm64|x86_64)-linux$")
    get_filename_component(ASCEND_INSTALL_PATH "${ASCEND_INSTALL_PATH}" DIRECTORY)
endif()
set(ASCEND_INSTALL_PATH "${ASCEND_INSTALL_PATH}" CACHE PATH "Ascend install path" FORCE)

function(_hcomm_append_cann_prefix_path cann_root)
    if(NOT cann_root)
        return()
    endif()

    get_filename_component(cann_root_abs "${cann_root}" ABSOLUTE)
    set(cann_search_roots "${cann_root_abs}")
    get_filename_component(cann_root_name "${cann_root_abs}" NAME)
    if(cann_root_name MATCHES "^(aarch64|arm64|x86_64)-linux$")
        get_filename_component(cann_parent_root "${cann_root_abs}" DIRECTORY)
        list(APPEND cann_search_roots "${cann_parent_root}")
    endif()

    if(PRODUCT_SIDE STREQUAL "device")
        set(cann_arch_candidates aarch64 arm64 ${CMAKE_SYSTEM_PROCESSOR} ${CMAKE_HOST_SYSTEM_PROCESSOR})
    else()
        set(cann_arch_candidates ${CMAKE_SYSTEM_PROCESSOR} ${CMAKE_HOST_SYSTEM_PROCESSOR} x86_64 aarch64 arm64)
    endif()
    foreach(cann_search_root ${cann_search_roots})
        foreach(cann_arch ${cann_arch_candidates})
            if(cann_arch)
                list(APPEND cann_prefix_candidates "${cann_search_root}/${cann_arch}-linux")
            endif()
        endforeach()
        list(APPEND cann_prefix_candidates "${cann_search_root}")
    endforeach()

    set(cann_prefix_paths)
    foreach(cann_prefix ${cann_prefix_candidates})
        if(IS_DIRECTORY "${cann_prefix}")
            list(APPEND cann_prefix_paths "${cann_prefix}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES cann_prefix_paths)

    if(cann_prefix_paths)
        list(PREPEND CMAKE_PREFIX_PATH ${cann_prefix_paths})
        list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    endif()
endfunction()

_hcomm_append_cann_prefix_path("${ASCEND_INSTALL_PATH}")
_hcomm_append_cann_prefix_path("${ASCEND_CANN_PACKAGE_PATH}")

function(_hcomm_resolve_acl_rt)
    set(acl_rt_include_candidates)
    set(acl_rt_library_candidates)

    foreach(cann_prefix ${CMAKE_PREFIX_PATH})
        list(APPEND acl_rt_include_candidates
            "${cann_prefix}/include"
            "${cann_prefix}/runtime/include"
            "${cann_prefix}/acllib/include"
            "${cann_prefix}/fwkacllib/include"
        )
        if(PRODUCT_SIDE STREQUAL "device")
            list(APPEND acl_rt_library_candidates
                "${cann_prefix}/lib64/libacl_rt.so"
                "${cann_prefix}/runtime/lib64/libacl_rt.so"
                "${cann_prefix}/devlib/linux/aarch64/libacl_rt.so"
                "${cann_prefix}/devlib/libacl_rt.so"
            )
        else()
            list(APPEND acl_rt_library_candidates
                "${cann_prefix}/devlib/linux/${CMAKE_HOST_SYSTEM_PROCESSOR}/libacl_rt.so"
                "${cann_prefix}/devlib/linux/x86_64/libacl_rt.so"
                "${cann_prefix}/devlib/libacl_rt.so"
            )
        endif()
        list(APPEND acl_rt_library_candidates
            "${cann_prefix}/lib64/stub/libacl_rt.so"
            "${cann_prefix}/runtime/lib64/stub/libacl_rt.so"
            "${cann_prefix}/acllib/lib64/libacl_rt.so"
            "${cann_prefix}/fwkacllib/lib64/libacl_rt.so"
            "${cann_prefix}/lib64/libacl_rt.so"
            "${cann_prefix}/runtime/lib64/libacl_rt.so"
        )
    endforeach()

    foreach(acl_rt_include ${acl_rt_include_candidates})
        if(EXISTS "${acl_rt_include}/acl/acl.h" OR EXISTS "${acl_rt_include}/acl/acl_rt.h")
            set(_CANN_ACL_RT_INCLUDE_DIR "${acl_rt_include}" CACHE PATH "CANN acl_rt include directory" FORCE)
            break()
        endif()
    endforeach()

    foreach(acl_rt_library ${acl_rt_library_candidates})
        if(EXISTS "${acl_rt_library}")
            set(_CANN_acl_rt_SHARED_LIBRARY "${acl_rt_library}" CACHE FILEPATH "CANN acl_rt shared library" FORCE)
            break()
        endif()
    endforeach()

    if(_CANN_ACL_RT_INCLUDE_DIR OR _CANN_acl_rt_SHARED_LIBRARY)
        message(STATUS "Resolved acl_rt include: ${_CANN_ACL_RT_INCLUDE_DIR}")
        message(STATUS "Resolved acl_rt library: ${_CANN_acl_rt_SHARED_LIBRARY}")
    endif()
endfunction()

_hcomm_resolve_acl_rt()

set(ASCEND_MOCKCPP_PACKAGE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_INSTALL_PREFIX STREQUAL /usr/local)
    set(CMAKE_INSTALL_PREFIX     "${CMAKE_CURRENT_SOURCE_DIR}/output"  CACHE STRING "path for install()" FORCE)
endif ()

set(HI_PYTHON                     "python3"                       CACHE   STRING   "python executor")

message(STATUS "config.cmake BUILD_OPEN_PROJECT=${BUILD_OPEN_PROJECT}")
message(STATUS "config.cmake PRODUCT=${PRODUCT} PRODUCT_SIDE=${PRODUCT_SIDE}")
message(STATUS "config.cmake ASCEND_CANN_PACKAGE_PATH=${ASCEND_CANN_PACKAGE_PATH}")
message(STATUS "config.cmake ASCEND_INSTALL_PATH=${ASCEND_INSTALL_PATH}")

# Device 构建、安装目录
set(HCOMM_DEVICE_BUILD_PATH   ${CMAKE_BINARY_DIR}/device_build)
set(HCOMM_DEVICE_INSTALL_PATH ${CMAKE_BINARY_DIR}/device_install)

set(INSTALL_LIBRARY_DIR        ${CMAKE_SYSTEM_PROCESSOR}-linux/lib64)
set(INSTALL_INCLUDE_DIR        ${CMAKE_SYSTEM_PROCESSOR}-linux/include)
set(INSTALL_PKG_INCLUDE_DIR    ${CMAKE_SYSTEM_PROCESSOR}-linux/pkg_inc)
set(INSTALL_CCL_KERNEL_JSON_DIR opp/built-in/op_impl/aicpu)
set(INSTALL_DPU_KERNEL_JSON_DIR opp/built-in/op_impl/dpu)
set(INSTALL_DEVICE_TAR_DIR compat)

if(ENABLE_BUILD_AARCH)
    set(INSTALL_DEVICE_LIBRARY_DIR ${CMAKE_SYSTEM_PROCESSOR}-linux/devlib/device)
else()
    set(INSTALL_DEVICE_LIBRARY_DIR ${CMAKE_HOST_SYSTEM_PROCESSOR}-linux/devlib/device)
endif()

set(CMAKE_SKIP_RPATH TRUE)
