# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_AICPU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_AICPU_ROOT}/../.. ABSOLUTE)
set(ASCCOMM_DFX_SRC_DIR "${ASCCOMM_ROOT}/src/common/dfx/logging")


set(ASCCOMM_CPU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_CPU_ROOT}/../.. ABSOLUTE)

add_library(asccomm_cpu_ts SHARED asccomm_cpu_primitives.cc)

target_compile_features(asccomm_cpu_ts PRIVATE cxx_std_17)
target_compile_definitions(asccomm_cpu_ts PRIVATE
    OPEN_BUILD_PROJECT
    _GLIBCXX_USE_CXX11_ABI=1
)
target_compile_options(asccomm_cpu_ts PRIVATE
    -Wfloat-equal
    -Wall
    -Wextra
    -fno-common
    -fstack-protector-strong
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -pipe
    -O3
)
target_include_directories(asccomm_cpu_ts PRIVATE
    ${ASCCOMM_ROOT}/include/cpu
    ${ASCCOMM_ROOT}/include/aicpu/hcomm
)

# CPU库只动态解析runtime notify API，不在链接期绑定hcomm或runtime符号。
target_link_libraries(asccomm_cpu_ts PRIVATE
    dl
    pthread
)

target_link_options(asccomm_cpu_ts PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--gc-sections
)

set_target_properties(asccomm_cpu_ts PROPERTIES
    OUTPUT_NAME asccomm_cpu
    SKIP_BUILD_RPATH TRUE
)

# install规则仅在asc-comm根工程(独立/自包含构建)下注册；第三方超构建由宿主自行install/打包。
if("${PROJECT_SOURCE_DIR}" STREQUAL "${ASCCOMM_ROOT}")
    if(NOT INSTALL_LIBRARY_DIR)
        set(INSTALL_LIBRARY_DIR lib64)
    endif()
    if(NOT INSTALL_PKG_INCLUDE_DIR)
        set(INSTALL_PKG_INCLUDE_DIR pkg_inc)
    endif()
    install(TARGETS asccomm_cpu_ts
        LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
        COMPONENT asccomm)
    install(DIRECTORY ${ASCCOMM_ROOT}/include/cpu/
        DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/asccomm/cpu
        COMPONENT asccomm
        FILES_MATCHING PATTERN "*.h")
endif()

# Keep an explicit aggregate target, matching the AICPU/CCU CMake integration.
add_custom_target(asccomm_cpu_compile_check DEPENDS asccomm_cpu_ts)
