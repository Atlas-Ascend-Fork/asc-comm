# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_CCU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_CCU_ROOT}/../.. ABSOLUTE)

set(ASCCOMM_CANN_INCLUDE_DIRS)
set(_asccomm_cann_root)
foreach(_cann_root IN ITEMS
    "${ASCEND_CANN_PACKAGE_PATH}"
    "${ASCEND_HOME_PATH}"
    "$ENV{ASCEND_HOME_PATH}"
    "$ENV{ASCEND_TOOLKIT_HOME}")
    if(NOT _asccomm_cann_root AND EXISTS "${_cann_root}/include")
        set(_asccomm_cann_root "${_cann_root}")
    endif()
endforeach()
if(_asccomm_cann_root)
    list(APPEND ASCCOMM_CANN_INCLUDE_DIRS "${_asccomm_cann_root}/include")
    if(EXISTS "${_asccomm_cann_root}/include/driver")
        list(APPEND ASCCOMM_CANN_INCLUDE_DIRS "${_asccomm_cann_root}/include/driver")
    endif()
    if(EXISTS "${_asccomm_cann_root}/pkg_inc")
        list(APPEND ASCCOMM_CANN_INCLUDE_DIRS "${_asccomm_cann_root}/pkg_inc")
        foreach(_pkg_subdir IN ITEMS
            base
            trace
            runtime
            runtime/runtime
            runtime/runtime/rts
            profiling
            aicpu
            aicpu/common
            dump)
            if(EXISTS "${_asccomm_cann_root}/pkg_inc/${_pkg_subdir}")
                list(APPEND ASCCOMM_CANN_INCLUDE_DIRS "${_asccomm_cann_root}/pkg_inc/${_pkg_subdir}")
            endif()
        endforeach()
    endif()
endif()
foreach(_json_root IN ITEMS
    "${CANN_3RD_LIB_PATH}/json/include"
    "${CANN_3RD_LIB_PATH}/json/single_include"
    "${ASCCOMM_ROOT}/../json/include"
    "${ASCCOMM_ROOT}/../json/single_include")
    if(EXISTS "${_json_root}/nlohmann/json.hpp")
        list(APPEND ASCCOMM_CANN_INCLUDE_DIRS "${_json_root}")
    endif()
endforeach()
list(REMOVE_DUPLICATES ASCCOMM_CANN_INCLUDE_DIRS)

if(TARGET c_sec)
    set(ASCCOMM_C_SEC_LIBRARY c_sec)
elseif(EXISTS "${_asccomm_cann_root}/lib64/libc_sec.so")
    set(ASCCOMM_C_SEC_LIBRARY "${_asccomm_cann_root}/lib64/libc_sec.so")
else()
    message(FATAL_ERROR "asc-comm CCU requires libc_sec.so from the CANN package ${_asccomm_cann_root}")
endif()

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

add_library(asccomm_ccu_core_obj OBJECT)
add_library(asccomm_ccu_dataplane_obj OBJECT)
add_library(asccomm_hcomm_obj ALIAS asccomm_ccu_core_obj)
add_library(asccomm_ccu_hcomm ALIAS asccomm_ccu_core_obj)

function(asccomm_configure_ccu_object target_name)
target_include_directories(${target_name} BEFORE PRIVATE
    ${ASCCOMM_CCU_ROOT}/abi
    ${ASCCOMM_CCU_ROOT}/support
    ${ASCCOMM_CCU_ROOT}/provider
)
target_link_libraries(${target_name} PRIVATE
    asccomm_ccu_headers
    $<BUILD_INTERFACE:intf_pub>
    $<BUILD_INTERFACE:error_manager_headers>
    $<BUILD_INTERFACE:acl_rt_headers>
    $<BUILD_INTERFACE:asc_host_headers>
    $<BUILD_INTERFACE:ascend_hal_headers>
    $<BUILD_INTERFACE:kernel_tiling_headers>
    $<BUILD_INTERFACE:atrace_headers>
    $<BUILD_INTERFACE:mmpa_headers>
    $<BUILD_INTERFACE:runtime_headers>
    $<BUILD_INTERFACE:rdma_core_headers>
    $<BUILD_INTERFACE:hccl_legacy_headers>
    $<BUILD_INTERFACE:json>
    $<BUILD_INTERFACE:c_sec_headers>
    $<BUILD_INTERFACE:unified_dlog>
)
target_include_directories(${target_name} PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)
target_compile_definitions(${target_name} PRIVATE
    BUILD_OPEN_PROJECT
    CANN_VERSION_NUM=90100000
    _GLIBCXX_USE_CXX11_ABI=0
)
target_compile_options(${target_name} PRIVATE -w -fPIC -fvisibility=hidden)
target_compile_options(${target_name} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/ccu_dataplane_compat.h>
)
target_include_directories(${target_name} PRIVATE
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/primitives/api_c_adpt/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/pub_inc
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_instance
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/interface
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/sync
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/translator
)
endfunction()

asccomm_configure_ccu_object(asccomm_ccu_core_obj)
asccomm_configure_ccu_object(asccomm_ccu_dataplane_obj)

target_sources(asccomm_ccu_dataplane_obj PRIVATE
    provider/ccu_provider.cc
    provider/ccu_provider_internal.cc
)

add_subdirectory(dataplane/hcomm/base_comm/primitives/api_c_adpt/ccu)
add_subdirectory(dataplane/hcomm/base_comm/resources/ccu)

add_library(asccomm_ccu_dataplane SHARED
    $<TARGET_OBJECTS:asccomm_ccu_dataplane_obj>
    $<TARGET_OBJECTS:asccomm_ccu_core_obj>
)
set_target_properties(asccomm_ccu_dataplane PROPERTIES
    OUTPUT_NAME asccomm_ccu_dataplane
)
target_link_libraries(asccomm_ccu_dataplane PRIVATE
    asccomm_ccu_headers
    ${ASCCOMM_C_SEC_LIBRARY}
)
if(TARGET runtime)
    target_link_libraries(asccomm_ccu_dataplane PRIVATE runtime)
elseif(EXISTS "${_asccomm_cann_root}/lib64/libruntime.so")
    target_link_libraries(asccomm_ccu_dataplane PRIVATE "${_asccomm_cann_root}/lib64/libruntime.so")
endif()

include(GNUInstallDirs)
if(NOT DEFINED ASCCOMM_CCU_INSTALL_LIBDIR)
    set(ASCCOMM_CCU_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()
if(NOT DEFINED ASCCOMM_CCU_INSTALL_INCLUDEDIR)
    set(ASCCOMM_CCU_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/ccu)
endif()
if(NOT DEFINED ASCCOMM_CCU_INSTALL_COMPONENT)
    set(ASCCOMM_CCU_INSTALL_COMPONENT Unspecified)
endif()

install(TARGETS asccomm_ccu_dataplane
    LIBRARY DESTINATION ${ASCCOMM_CCU_INSTALL_LIBDIR}
    COMPONENT ${ASCCOMM_CCU_INSTALL_COMPONENT}
)
install(DIRECTORY ${ASCCOMM_ROOT}/include/ccu/
    DESTINATION ${ASCCOMM_CCU_INSTALL_INCLUDEDIR}
    COMPONENT ${ASCCOMM_CCU_INSTALL_COMPONENT}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

add_custom_target(asccomm_ccu_hcomm_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_compile_check DEPENDS asccomm_ccu_dataplane)

if(BUILD_TESTING)
    add_executable(asccomm_ccu_rtld_test EXCLUDE_FROM_ALL
        ${ASCCOMM_ROOT}/tests/ccu/provider/ccu_rtld_test.cc
    )
    target_include_directories(asccomm_ccu_rtld_test PRIVATE
        ${ASCCOMM_ROOT}/include/ccu
        ${CMAKE_CURRENT_SOURCE_DIR}/abi
        ${CMAKE_CURRENT_SOURCE_DIR}/provider
        ${CMAKE_CURRENT_SOURCE_DIR}/dataplane/hcomm/base_comm/resources/ccu/pub_inc
    )
    target_link_libraries(asccomm_ccu_rtld_test PRIVATE ${CMAKE_DL_LIBS})
    target_link_options(asccomm_ccu_rtld_test PRIVATE -Wl,--export-dynamic)
    add_test(NAME asccomm_ccu_rtld_test
        COMMAND asccomm_ccu_rtld_test $<TARGET_FILE:asccomm_ccu_dataplane>)

    add_custom_target(asccomm_ccu_tests
        DEPENDS asccomm_ccu_rtld_test
    )
endif()
