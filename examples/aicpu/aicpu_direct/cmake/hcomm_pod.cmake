# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# POD数据面依赖解析：AICPU Acquire/Export直接返回实体地址句柄，算子只传递
# ThreadHandle/ChannelHandle；asc-comm在数据面内部将其解释为POD实体。
# 算子和公开asc-comm接口均不暴露Hcomm*Entity类型。

# 校验asccomm_primitives.h是否为当前的句柄签名。
function(_hcomm_pod_check_handle_header header_path out_var)
    set(${out_var} FALSE PARENT_SCOPE)
    if(NOT EXISTS "${header_path}")
        return()
    endif()
    file(STRINGS "${header_path}" _handle_lines
        REGEX "asccomm_local_copy_on_thread[ \t]*\\([ \t]*ThreadHandle")
    if(NOT "${_handle_lines}" STREQUAL "")
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

set(_pod_arch_fatal_hint
    "本样例使用HCOMM控制面Acquire/Export直接生成实体句柄、asc-comm按句柄直取POD实体的架构，需配套源码环境：\n"
    "  1. 准备配套asc-comm仓(句柄签名asccomm_primitives.h)，并构建AICPU数据面：\n"
    "       cd <asc-comm> && bash build.sh            # 产出 build/aicpu-data-plane/libasccomm_aicpu.so\n"
    "  2. 准备配套hcomm仓(Acquire/Export直接返回实体句柄)，并构建设备侧控制面库：\n"
    "       cd <hcomm> && bash build.sh --pkg --full  # 产出 build/device_build*/.../libccl_kernel.so\n"
    "  3. 编译样例时通过环境变量指定配套源码与库：\n"
    "       export ASC_COMM_SOURCE_DIR=/path/to/asc-comm\n"
    "       export HCOMM_SOURCE_DIR=/path/to/hcomm\n"
    "       export ASCCOMM_DEVICE_LIBRARY=/path/to/libasccomm_aicpu.so\n"
    "       export HCOMM_DEVICE_LIBRARY=/path/to/libccl_kernel.so\n"
    "  未指定时亦默认探测样例仓同级目录(../asc-comm、../hcomm)及其构建产物。")

set(ASC_COMM_SOURCE_DIR "$ENV{ASC_COMM_SOURCE_DIR}" CACHE PATH "asc-comm source directory providing AICPU primitives")
if(NOT ASC_COMM_SOURCE_DIR)
    get_filename_component(_default_asc_comm_source_dir "${CMAKE_CURRENT_LIST_DIR}/../../asc-comm" REALPATH)
    if(EXISTS "${_default_asc_comm_source_dir}/include/aicpu/asccomm/asccomm_primitives.h")
        set(ASC_COMM_SOURCE_DIR "${_default_asc_comm_source_dir}" CACHE PATH
            "asc-comm source directory providing AICPU primitives" FORCE)
    endif()
endif()

if(ASC_COMM_SOURCE_DIR)
    _hcomm_pod_check_handle_header("${ASC_COMM_SOURCE_DIR}/include/aicpu/asccomm/asccomm_primitives.h" _asccomm_src_ok)
    if(NOT _asccomm_src_ok)
        message(FATAL_ERROR
            "ASC_COMM_SOURCE_DIR(${ASC_COMM_SOURCE_DIR})的asccomm_primitives.h不是当前的句柄签名，"
            "请同步配套分支后重试。\n${_pod_arch_fatal_hint}")
    endif()
    message(STATUS "POD data plane: use asc-comm source ${ASC_COMM_SOURCE_DIR}")
else()
    # 未指定源码：仅当CANN安装包内头为句柄签名时可用，否则报错引导
    set(_installed_asccomm_header "")
    foreach(_cand
            "${ASCEND_CANN_PACKAGE_PATH}/include/aicpu/asccomm/asccomm_primitives.h"
            "${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/aicpu/asccomm/asccomm_primitives.h")
        _hcomm_pod_check_handle_header("${_cand}" _cand_ok)
        if(_cand_ok)
            set(_installed_asccomm_header "${_cand}")
            break()
        endif()
    endforeach()
    if(_installed_asccomm_header STREQUAL "")
        message(FATAL_ERROR
            "未找到句柄签名asccomm_primitives.h：CANN(${ASCEND_CANN_PACKAGE_PATH})内头文件不兼容"
            "或不存在，且未设置ASC_COMM_SOURCE_DIR。\n${_pod_arch_fatal_hint}")
    endif()
    message(STATUS "POD data plane: use CANN installed header ${_installed_asccomm_header}")
endif()

set(HCOMM_SOURCE_DIR "$ENV{HCOMM_SOURCE_DIR}" CACHE PATH "HCOMM source directory providing the AICPU library")
if(NOT HCOMM_SOURCE_DIR)
    get_filename_component(_default_hcomm_source_dir "${CMAKE_CURRENT_LIST_DIR}/../../hcomm" REALPATH)
    if(EXISTS "${_default_hcomm_source_dir}/include/hcomm_res.h")
        set(HCOMM_SOURCE_DIR "${_default_hcomm_source_dir}" CACHE PATH
            "HCOMM source directory providing the AICPU library" FORCE)
    endif()
endif()

if(HCOMM_SOURCE_DIR)
    if(NOT EXISTS "${HCOMM_SOURCE_DIR}/include/hcomm_res.h")
        message(FATAL_ERROR
            "HCOMM_SOURCE_DIR(${HCOMM_SOURCE_DIR})未找到include/hcomm_res.h，请检查配套hcomm源码路径。\n${_pod_arch_fatal_hint}")
    endif()
    message(STATUS "POD control plane: use hcomm source ${HCOMM_SOURCE_DIR}")
else()
    set(_installed_hcomm_res "")
    foreach(_cand "${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/hcomm_res.h"
            "${ASCEND_CANN_PACKAGE_PATH}/include/hccl/hcomm_res.h")
        if(EXISTS "${_cand}")
            set(_installed_hcomm_res "${_cand}")
            break()
        endif()
    endforeach()
    if(_installed_hcomm_res STREQUAL "")
        message(FATAL_ERROR
            "未找到hcomm_res.h：CANN(${ASCEND_CANN_PACKAGE_PATH})内头文件不存在，且未设置HCOMM_SOURCE_DIR。\n${_pod_arch_fatal_hint}")
    endif()
    # 安装包路径兜底：记录homm_res.h所在目录，待HCOMM_POD_INCLUDE_DIRS初始化后追加
    get_filename_component(HCOMM_POD_INSTALLED_HCOMM_INC "${_installed_hcomm_res}" DIRECTORY)
    message(STATUS "POD control plane: use CANN installed header ${_installed_hcomm_res}")
endif()

set(HCOMM_DEVICE_LIBRARY "$ENV{HCOMM_DEVICE_LIBRARY}" CACHE FILEPATH "HCOMM AICPU control-plane library")
if(NOT HCOMM_DEVICE_LIBRARY AND HCOMM_SOURCE_DIR)
    foreach(_hcomm_build_dir device_build_pod_split device_build)
        set(_default_hcomm_device_library
            "${HCOMM_SOURCE_DIR}/build/${_hcomm_build_dir}/_pack_aicpu_hcomm_stage/aicpu_kernels_device/libccl_kernel.so")
        if(EXISTS "${_default_hcomm_device_library}")
            set(HCOMM_DEVICE_LIBRARY "${_default_hcomm_device_library}" CACHE FILEPATH
                "HCOMM AICPU control-plane library" FORCE)
            break()
        endif()
    endforeach()
endif()

set(ASCCOMM_DEVICE_LIBRARY "$ENV{ASCCOMM_DEVICE_LIBRARY}" CACHE FILEPATH
    "asc-comm AICPU data-plane library")
# runtime PR 4842 owns the asc-comm delivery.  Prefer the runtime-installed
# device library; HCOMM's aicpu_hcomm.tar.gz intentionally contains only the
# control-plane kernel libraries.
if(NOT ASCCOMM_DEVICE_LIBRARY AND ASCEND_CANN_PACKAGE_PATH)
    foreach(_runtime_asccomm_library
            "${ASCEND_CANN_PACKAGE_PATH}/lib64/device/lib64/libasccomm_aicpu.so"
            "${ASCEND_CANN_PACKAGE_PATH}/devlib/device/libasccomm_aicpu.so")
        if(EXISTS "${_runtime_asccomm_library}")
            set(ASCCOMM_DEVICE_LIBRARY "${_runtime_asccomm_library}" CACHE FILEPATH
                "asc-comm AICPU data-plane library" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT ASCCOMM_DEVICE_LIBRARY AND ASC_COMM_SOURCE_DIR)
    foreach(_default_asccomm_source_library
            "${ASC_COMM_SOURCE_DIR}/build/aicpu-data-plane/libasccomm_aicpu.so"
            "${ASC_COMM_SOURCE_DIR}/build/aicpu-data-plane/src/aicpu/libasccomm_aicpu.so")
        if(EXISTS "${_default_asccomm_source_library}")
            set(ASCCOMM_DEVICE_LIBRARY "${_default_asccomm_source_library}" CACHE FILEPATH
                "asc-comm AICPU data-plane library" FORCE)
            break()
        endif()
    endforeach()
endif()

if(NOT ASCCOMM_DEVICE_LIBRARY)
    message(WARNING
        "未找到配套构建的libasccomm_aicpu.so(ASCCOMM_DEVICE_LIBRARY/asc-comm默认构建产物均未命中)，"
        "kernel将链接CANN安装包内的数据面库；若其非配套版本，运行期将因句柄/实体不匹配而失败。"
        "请按配套架构构建asc-comm数据面并通过ASCCOMM_DEVICE_LIBRARY指定。")
endif()

# Prevent an old runtime data-plane library from being linked into the kernel.
# The old library leaves the pre-C-ABI resolver unresolved and only fails when
# the Device scheduler dlopens the kernel, which is too late to diagnose.
if(ASCCOMM_DEVICE_LIBRARY AND EXISTS "${ASCCOMM_DEVICE_LIBRARY}")
    find_program(ASCCOMM_NM_TOOL NAMES llvm-nm nm)
    if(ASCCOMM_NM_TOOL)
        execute_process(
            COMMAND ${ASCCOMM_NM_TOOL} -D --undefined-only "${ASCCOMM_DEVICE_LIBRARY}"
            RESULT_VARIABLE _asccomm_nm_result
            OUTPUT_VARIABLE _asccomm_undefined_symbols
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_asccomm_nm_result EQUAL 0 AND _asccomm_undefined_symbols MATCHES
            "_Z28asccomm_ensure_rtsq_resolvedR17HcommThreadEntity")
            message(FATAL_ERROR
                "ASCCOMM_DEVICE_LIBRARY is incompatible: ${ASCCOMM_DEVICE_LIBRARY} still requires "
                "the old C++ symbol _Z28asccomm_ensure_rtsq_resolvedR17HcommThreadEntity. "
                "Rebuild the runtime asccomm_aicpu library from the current asc-comm source.")
        endif()
    endif()
endif()

set(HCOMM_POD_INCLUDE_DIRS "${ASCEND_CANN_PACKAGE_PATH}/include/aicpu")
if(ASC_COMM_SOURCE_DIR)
    list(PREPEND HCOMM_POD_INCLUDE_DIRS "${ASC_COMM_SOURCE_DIR}/include/aicpu")
endif()
if(ASC_COMM_SOURCE_DIR AND HCOMM_SOURCE_DIR)
    # Use namespaced hcomm public headers from the packaged include directory.
    list(PREPEND HCOMM_POD_INCLUDE_DIRS "${HCOMM_SOURCE_DIR}/pkg_inc")
    # Keep the source include path for headers that are not packaged in pkg_inc/hcomm.
    list(APPEND HCOMM_POD_INCLUDE_DIRS "${HCOMM_SOURCE_DIR}/include")
    set(ASCCOMM_SOURCE_COMPILE_DEFINITION ASCCOMM_SOURCE_BUILD)
endif()
if(HCOMM_POD_INSTALLED_HCOMM_INC)
    list(APPEND HCOMM_POD_INCLUDE_DIRS "${HCOMM_POD_INSTALLED_HCOMM_INC}")
endif()

if(HCOMM_DEVICE_LIBRARY)
    set(HCOMM_POD_DEVICE_LIBRARY "${HCOMM_DEVICE_LIBRARY}")
else()
    set(HCOMM_POD_DEVICE_LIBRARY ccl_kernel)
endif()

if(ASCCOMM_DEVICE_LIBRARY)
    set(ASCCOMM_DATA_PLANE_LIBRARY "${ASCCOMM_DEVICE_LIBRARY}")
else()
    set(ASCCOMM_DATA_PLANE_LIBRARY asccomm_aicpu)
endif()
