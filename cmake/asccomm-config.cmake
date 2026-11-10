# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You should not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See the LICENSE in the root of the software for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

if(_ASCCOMM_CCU_HEADERS_MODULE_LOADED)
    return()
endif()

# The target may already exist when the consumer builds inside the asc-comm source tree.
if(TARGET asccomm_ccu_headers)
    set(_ASCCOMM_CCU_HEADERS_MODULE_LOADED TRUE)
    return()
endif()

if(DEFINED ASCEND_CANN_PACKAGE_PATH AND ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CCU_HEADERS_HOME "${ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_ASCCOMM_CCU_HEADERS_HOME "$ENV{ASCEND_HOME_PATH}")
else()
    message(FATAL_ERROR "ERROR: ASCEND_HOME_PATH or ASCEND_CANN_PACKAGE_PATH must be set! source set_env.sh in cann install directory first.")
endif()

if(NOT EXISTS "${_ASCCOMM_CCU_HEADERS_HOME}")
    message(FATAL_ERROR "ERROR: ASCEND CANN package directory does not exist: ${_ASCCOMM_CCU_HEADERS_HOME}")
endif()

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    "${_ASCCOMM_CCU_HEADERS_HOME}/asc/impl/comm_api"
)

set(_ASCCOMM_CCU_HEADERS_MODULE_LOADED TRUE)
unset(_ASCCOMM_CCU_HEADERS_HOME)
