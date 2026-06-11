# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
set(HCCL_UT_PREPARE_ENV_SRC_LIST 
    main.cc
)
set(HCCL_UT_PREPARE_ENV_TARGET asccomm_ut_mc2_cc_prepare_env)
add_executable(${HCCL_UT_PREPARE_ENV_TARGET}  ${HCCL_UT_PREPARE_ENV_SRC_LIST})

target_include_directories(${HCCL_UT_PREPARE_ENV_TARGET}  PRIVATE
    ${UT_COMMON_INCLUDE_LIST}
)

target_compile_options(${HCCL_UT_PREPARE_ENV_TARGET}   PRIVATE 
    -U_FORTIFY_SOURCE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++14>
)

target_compile_options(${HCCL_UT_PREPARE_ENV_TARGET}   PRIVATE 
    -U_FORTIFY_SOURCE
    -g --coverage -fprofile-arcs -ftest-coverage -fno-access-control
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++14>
)

target_link_libraries(${HCCL_UT_PREPARE_ENV_TARGET}  PRIVATE
    $<BUILD_INTERFACE:intf_llt_pub_basic>
    -lpthread
    -Wl,--whole-archive
    -Wl,--no-whole-archive
    -lgcov
    -lrt
    -lpthread
)

run_llt_test(
    TARGET  ${HCCL_UT_PREPARE_ENV_TARGET}
    TASK_NUM 1
)
