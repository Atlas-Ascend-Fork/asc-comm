# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
IF (DEFINED ENV{HCCL_LOCAL_BUILD})
    # 定义后续需要使用的变量
    cmake_minimum_required(VERSION 3.20.5)
    project(hccl)
    set(TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
ENDIF ()

set(HCCL_V2_CODE_ROOT ${ORION_REAL_PATH})

# 指定hccl编译为动态库
# 自动找到项目内所有头文件
FILE(GLOB_RECURSE sources_list LIST_DIRECTORIES TRUE ${HCCL_V2_CODE_ROOT}/*)

foreach (dir ${sources_list})
    IF ((IS_DIRECTORY ${dir} ) AND (${dir} MATCHES "aicpu_kfc/legacy") AND (NOT ${dir} MATCHES "aicpu_kfc/legacy/mdpi"))
    endif ()
endforeach ()

# 添加项目外部头文件
# 每个文件夹单独组织要参与编译的文件
add_subdirectory(common)
add_subdirectory(framework)
add_subdirectory(service)
add_subdirectory(unified_platform)
add_subdirectory(interface)

IF (DEFINED ENV{HCCL_LOCAL_BUILD})
    # add_subdirectory(aicpu) #当前未使用，注释
ENDIF ()

# 编译选项
#target_compile_options(hccl_v2 PRIVATE
#        -std=c++14
#        -Werror
#        -fno-common
#        -fno-strict-aliasing
#        -pipe
#        -O3
 
#        -Wextra
#        -Wno-unused-parameter
#        -Wno-missing-field-initializers
#        -Wfloat-equal
#)
 
# 创建 ccl_dpu 共享动态库
# 指定 ccl_dpu 需要包含的文件搜索路径
# 指定 ccl_dpu 的构建依赖关系
# 指定源文件
set(CCL_DPU_SRC_LIST
    ${HCCL_V2_CODE_ROOT}/framework/communicator/hostdpu/dpu_kernel_entrance.cc
    ${HCCL_V2_CODE_ROOT}/framework/communicator/hostdpu/task_service.cc
    ${HCCL_V2_CODE_ROOT}/common/log.cc
)
# 指定 ccl_dpu 的编译选项
# 为 ccl_dpu 添加编译定义
# 向 ccl_dpu 添加链接器选项
# 将库文件链接到目标ccl_dpu
# 指定 ccl_dpu 构建完成后安装到指定的目标位置
# 需要链接的库，后需增加
IF (NOT DEFINED ENV{HCCL_LOCAL_BUILD})
ENDIF ()

# 将hccl编译出的动态库加入CANN的安装包
IF (NOT DEFINED ENV{HCCL_LOCAL_BUILD})
ENDIF ()
