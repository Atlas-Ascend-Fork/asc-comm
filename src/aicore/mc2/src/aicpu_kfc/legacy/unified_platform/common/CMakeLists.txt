# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
if(NOT BUILD_OPEN_PROJECT OR (BUILD_OPEN_PROJECT AND KERNEL_MODE))
target_sources(asc_ccl_kernel PRIVATE
        dev_capability.cc
        dlhal_function_v2.cc
        orion_adapter_hal.cc
)
endif()

if(TARGET mc2_server)
add_mc2_server_ccl_object(mc2_server_legacy_unified_common
        ${CMAKE_CURRENT_SOURCE_DIR}/dev_capability.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/dlhal_function_v2.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/orion_adapter_hal.cc
)
endif()
