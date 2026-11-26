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

set(_ASCCOMM_CCU_RUNTIME_SOURCES
    ccu_runtime.cpp
    ccu_runtime_backend.cpp
    ccu_aot_adapter.cpp
    ccu_runtime_asccomm_backend.cpp
)

add_library(ccu_runtime STATIC ${_ASCCOMM_CCU_RUNTIME_SOURCES})
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
set_target_properties(ccu_runtime PROPERTIES
    OUTPUT_NAME ccu_runtime
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_compile_definitions(ccu_runtime PRIVATE ASCCOMM_CCU_LOCAL_LOGGING)
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

if("${PROJECT_SOURCE_DIR}" STREQUAL "${ASCCOMM_ROOT}")
    if(NOT INSTALL_PKG_INCLUDE_DIR)
        set(INSTALL_PKG_INCLUDE_DIR pkg_inc)
    endif()
    install(TARGETS ccu_runtime
        ARCHIVE DESTINATION lib64
        COMPONENT asccomm)
    install(FILES ${ASCCOMM_ROOT}/include/ccu/experimental/ccu_runtime.h
        DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/ccu/experimental
        COMPONENT asccomm)
endif()
