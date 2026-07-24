# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# CCU 源文件路径前缀
set(CCU_REP_DIR ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation)
set(CCU_MICROCODE_DIR ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode)

# CCU UT 公共头文件目录：仅限本仓、测试兼容层和 CANN 安装包。
set(UT_COMMON_INCLUDE_LIST
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${CCU_REP_DIR}
    ${CCU_REP_DIR}/context
    ${CCU_REP_DIR}/interface
    ${CCU_REP_DIR}/reps/arithmetic
    ${CCU_REP_DIR}/reps/common
    ${CCU_REP_DIR}/reps/control
    ${CCU_REP_DIR}/reps/data
    ${CCU_REP_DIR}/reps/loop
    ${CCU_REP_DIR}/reps/sync
    ${CCU_REP_DIR}/reps/translator
    ${CCU_MICROCODE_DIR}
    ${ASCCOMM_ROOT}/src/ccu/dataplane/hcomm/base_comm/resources/ccu/pub_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCCOMM_UT_ROOT}/../ccu/stub
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/include
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

enable_testing()

add_library(asccomm_ccu_hcomm_compat INTERFACE)
target_include_directories(asccomm_ccu_hcomm_compat INTERFACE
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/include
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/source/base_comm/resources/ccu/ccu_representation/context
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/source/base_comm/resources/ccu/ccu_representation/reps/data
    ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/source/base_comm/resources/ccu/ccu_representation/reps/sync
)
target_compile_options(asccomm_ccu_hcomm_compat INTERFACE
    -include ${ASCCOMM_UT_ROOT}/../ccu/hcomm_compat/include/ccu_ut_legacy_compat.h
)

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
