# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_CCU_RUNTIME_ROOT ${CMAKE_CURRENT_LIST_DIR})

option(ASCCOMM_BUILD_CCU_RUNTIME_HCOMM_BACKEND
    "Build the HComm-backed compiler-facing CCU runtime" OFF)

set(_ASCCOMM_CCU_RUNTIME_SOURCES
    ccu_runtime.cpp
    ccu_runtime_backend.cpp
)

add_library(ccu_runtime SHARED ${_ASCCOMM_CCU_RUNTIME_SOURCES})
target_compile_features(ccu_runtime PRIVATE cxx_std_17)
target_compile_options(ccu_runtime PRIVATE -Wall -Wextra -Werror)
target_include_directories(ccu_runtime PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/pkg_inc
    ${ASCCOMM_CCU_RUNTIME_ROOT}
)
target_include_directories(ccu_runtime SYSTEM PRIVATE
    ${ASCCOMM_CANN_BASE_INCLUDE_DIRS}
    ${ASCCOMM_CANN_HEADER_DIRS}
)
target_link_options(ccu_runtime PRIVATE
    "-Wl,--version-script=${ASCCOMM_CCU_RUNTIME_ROOT}/ccu_runtime.map")
set_target_properties(ccu_runtime PROPERTIES
    OUTPUT_NAME ccu_runtime
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    # Runtime dependencies are supplied by the target CANN deployment. Do not
    # embed the build machine's absolute CANN lib64 path in the package.
    SKIP_BUILD_RPATH ON
)
set_property(TARGET ccu_runtime APPEND PROPERTY LINK_DEPENDS
    ${ASCCOMM_CCU_RUNTIME_ROOT}/ccu_runtime.map)

if(ASCCOMM_BUILD_CCU_RUNTIME_HCOMM_BACKEND)
    target_sources(ccu_runtime PRIVATE
        ccu_aot_adapter.cpp
        ccu_runtime_hcomm_backend.cpp)
    target_compile_definitions(ccu_runtime PRIVATE
        CCU_TRANSLATOR_HAS_HCOMM_BACKEND
        ASCCOMM_CCU_LOCAL_LOGGING)
    target_link_directories(ccu_runtime PRIVATE
        ${ASCEND_CANN_PACKAGE_PATH}/lib64
        ${ASCCOMM_CANN_ARCH_DIR}/lib64)
    target_link_libraries(ccu_runtime PRIVATE
        $<$<TARGET_EXISTS:acl_rt_headers>:acl_rt_headers>
        $<$<TARGET_EXISTS:runtime_headers>:runtime_headers>
        acl_rt
        runtime
        c_sec
        pthread)
endif()

if("${PROJECT_SOURCE_DIR}" STREQUAL "${ASCCOMM_ROOT}")
    if(NOT INSTALL_LIBRARY_DIR)
        set(INSTALL_LIBRARY_DIR lib64)
    endif()
    if(NOT INSTALL_PKG_INCLUDE_DIR)
        set(INSTALL_PKG_INCLUDE_DIR pkg_inc)
    endif()
    install(TARGETS ccu_runtime
        LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
        COMPONENT asccomm)
    install(FILES ${ASCCOMM_ROOT}/include/ccu/experimental/ccu_runtime.h
        DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/ccu/experimental
        COMPONENT asccomm)
endif()
