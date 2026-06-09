# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
target_sources(asc_ccl_kernel PRIVATE aicpu_kfc_interface.cc)

if(TARGET mc2_server)
    add_library(mc2_server_kernel_entry OBJECT mc2_server_kernel_entry.cc)
    set_target_properties(mc2_server_kernel_entry PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(mc2_server_kernel_entry PRIVATE
        $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_DEFINITIONS>
        MC2_SERVER_ONLY
    )
    target_compile_options(mc2_server_kernel_entry PRIVATE
        $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_OPTIONS>
    )
    target_include_directories(mc2_server_kernel_entry PRIVATE
        $<TARGET_PROPERTY:asc_ccl_kernel,INCLUDE_DIRECTORIES>
    )
    target_link_libraries(mc2_server_kernel_entry PRIVATE
        $<BUILD_INTERFACE:acl_rt_headers>
        $<BUILD_INTERFACE:ascend_hal_headers>
        $<BUILD_INTERFACE:runtime_headers>
    )
    target_sources(mc2_server PRIVATE
        $<TARGET_OBJECTS:mc2_server_kernel_entry>
    )
endif()

add_subdirectory(algorithm)
add_subdirectory(common)
add_subdirectory(dfx)
add_subdirectory(framework)
add_subdirectory(decoupler)
