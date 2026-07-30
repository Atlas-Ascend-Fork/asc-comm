# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_CCU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_CCU_ROOT}/../.. ABSOLUTE)

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    ${ASCCOMM_ROOT}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/runtime/include
    ${ASCCOMM_CANN_ARCH_DIR}/include
    ${ASCCOMM_CANN_ARCH_DIR}/include/hcomm/ccu
    ${ASCCOMM_CANN_ARCH_DIR}/include/hccl
)

add_library(asccomm_ccu_dataplane SHARED
    ${ASCCOMM_CCU_ROOT}/ccu_launch_kernel.cc
)
set_target_properties(asccomm_ccu_dataplane PROPERTIES
    OUTPUT_NAME asccomm_ccu_dataplane
)
target_link_libraries(asccomm_ccu_dataplane PRIVATE asccomm_ccu_headers)
target_compile_features(asccomm_ccu_dataplane PUBLIC cxx_std_17)
target_compile_options(asccomm_ccu_dataplane PRIVATE
    -fPIC
    -Wall
    -Werror
)

add_custom_target(asccomm_ccu_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_compile_check DEPENDS asccomm_ccu_dataplane)
