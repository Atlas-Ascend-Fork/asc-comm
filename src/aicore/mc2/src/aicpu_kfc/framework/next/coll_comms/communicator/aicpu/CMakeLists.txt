# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set(src_list
    aicpu_indop_process.cc
    coll_comm_aicpu_mgr.cc
    coll_comm_aicpu.cc
    hccl_aicpu_hdc_handler.cc
    coll_comm_aicpu_destroy_func.cc
)

if(NOT BUILD_OPEN_PROJECT OR (BUILD_OPEN_PROJECT AND KERNEL_MODE))
    target_include_directories(asc_ccl_kernel PRIVATE
        ${HCCL_BASE_DIR}/framework/next/coll_comms/communicator/ns_recovery/aicpu
    )
    target_sources(asc_ccl_kernel PRIVATE
        ${src_list}
    )
endif()

if(TARGET mc2_server)
    add_mc2_server_ccl_object(mc2_server_next_comm_aicpu
        ${CMAKE_CURRENT_SOURCE_DIR}/coll_comm_aicpu_mgr.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/coll_comm_aicpu.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/hccl_aicpu_hdc_handler.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/coll_comm_aicpu_destroy_func.cc
    )
    target_include_directories(mc2_server_next_comm_aicpu PRIVATE
        ${HCCL_BASE_DIR}/framework/next/coll_comms/communicator/ns_recovery/aicpu
    )
endif()
