# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# 引入精简桩库（在 CCU_IMPL_INCLUDE_DIRS 定义之后，使桩库能访问这些路径）
# add_subdirectory(../stub stub)  -- 移到后面

# CCU 源文件路径前缀
set(CCU_REP_DIR ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_representation)
set(CCU_MICROCODE_DIR ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/ccu/ccu_microcode)

# 自动收集 hcomm 全部头文件目录（避免逐个补充缺失路径）
file(GLOB_RECURSE _hcomm_headers
    "${ASCCOMM_HCOMM_SOURCE_DIR}/src/*.h"
    "${ASCCOMM_HCOMM_SOURCE_DIR}/src/*.hpp"
    "${ASCCOMM_HCOMM_SOURCE_DIR}/include/*.h"
    "${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/*.h"
)
set(_hcomm_include_dirs)
foreach(_hdr ${_hcomm_headers})
    get_filename_component(_dir "${_hdr}" DIRECTORY)
    list(APPEND _hcomm_include_dirs "${_dir}")
endforeach()
list(REMOVE_DUPLICATES _hcomm_include_dirs)

# CCU 实现编译选项（各测试目标共享的 include 路径和编译定义）
# 关键：ascend910/pub_inc 必须在 ascend950/common 之前（log.h 冲突）
set(CCU_IMPL_INCLUDE_DIRS
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc
    ${_hcomm_include_dirs}
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/pkg_inc/runtime
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/stub/depends/include/mmpa
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
)

enable_testing()

add_subdirectory(../stub stub)

# CCU 数据面子目标
add_subdirectory(ccu_representation/reps/sync)
add_subdirectory(ccu_representation/reps/control)
add_subdirectory(ccu_representation/reps/common)
add_subdirectory(ccu_representation/reps/data)
add_subdirectory(ccu_representation/reps/arithmetic)
add_subdirectory(ccu_representation/reps/loop)
add_subdirectory(ccu_representation/context)
add_subdirectory(ccu_representation/interface)
add_subdirectory(ccu_microcode)
