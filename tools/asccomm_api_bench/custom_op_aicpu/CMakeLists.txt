# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.19.0)
project(opp)

include(cmake/config.cmake)
if(ASCEND_PACK_SHARED_LIBRARY)
    message(FATAL_ERROR "asccomm_api_bench supports .run operator packages only")
endif()
include(cmake/func.cmake)
include(cmake/intf.cmake)

# Use the installed packaging tool instead of vendoring its sources.
find_program(MC2_BENCH_MAKESELF NAMES makeself makeself.sh)
if(NOT MC2_BENCH_MAKESELF OR NOT EXISTS "${MC2_BENCH_MAKESELF}")
    message(FATAL_ERROR "Install makeself or set MC2_BENCH_MAKESELF to its executable")
endif()

# These two original SDK helpers are build-time external dependencies.
set(MC2_BENCH_ASCENDC_UTIL_DIR "" CACHE PATH "Installed AscendC helper directory")
if(NOT MC2_BENCH_ASCENDC_UTIL_DIR)
    foreach(helper_prefix tools toolkit/tools)
        set(helper_candidate "${ASCEND_CANN_PACKAGE_PATH}/${helper_prefix}/op_project_templates/ascendc/customize/cmake/util")
        if(EXISTS "${helper_candidate}/ascendc_gen_options.py" AND EXISTS "${helper_candidate}/parse_ini_to_json.py")
            set(MC2_BENCH_ASCENDC_UTIL_DIR "${helper_candidate}")
            break()
        endif()
    endforeach()
endif()
foreach(helper ascendc_gen_options.py parse_ini_to_json.py)
    if(NOT EXISTS "${MC2_BENCH_ASCENDC_UTIL_DIR}/${helper}")
        message(FATAL_ERROR "Missing installed SDK helper ${helper}; set MC2_BENCH_ASCENDC_UTIL_DIR")
    endif()
endforeach()

option(MC2_BENCH_COMM_ONLY "Delay Commit for communication-only diagnosis" OFF)
set(MC2_BENCH_COMM_ONLY_VALUE 0)
set(MC2_BENCH_MEASUREMENT_MODE full)
set(MC2_BENCH_DELAY_NOPS 0)
if(MC2_BENCH_COMM_ONLY)
    set(MC2_BENCH_COMM_ONLY_VALUE 1)
    set(MC2_BENCH_MEASUREMENT_MODE comm_only)
    set(MC2_BENCH_DELAY_NOPS 5000000)
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/asccomm_api_bench_package.info.in
    ${CMAKE_CURRENT_BINARY_DIR}/asccomm_api_bench_package.info
    @ONLY
)

if(ENABLE_CROSS_COMPILE)
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL x86_64)
        set(CROSS_COMPILE_PLATFORM aarch64)
    else()
        set(CROSS_COMPILE_PLATFORM x86_64)
    endif()
    set(PLATFORM ${CMAKE_SYSTEM_PROCESSOR})
    set(CMAKE_COMPILE_COMPILER_LIBRARY ${ASCEND_CANN_PACKAGE_PATH}/${PLATFORM}-linux/devlib/linux/${CROSS_COMPILE_PLATFORM}/)
    set(CMAKE_COMPILE_RUNTIME_LIBRARY ${ASCEND_CANN_PACKAGE_PATH}/${PLATFORM}-linux/devlib/${CROSS_COMPILE_PLATFORM}/)
    if(CMAKE_CROSS_LIBRARY_PATH)
        set(CMAKE_COMPILE_COMPILER_LIBRARY ${CMAKE_CROSS_LIBRARY_PATH})
        set(CMAKE_COMPILE_RUNTIME_LIBRARY ${CMAKE_CROSS_LIBRARY_PATH})
    endif()
    set(CMAKE_SYSTEM_PROCESSOR ${CROSS_COMPILE_PLATFORM})
    set(CMAKE_COMPILE ${CMAKE_CXX_COMPILER})
    set(CMAKE_CXX_COMPILER ${CMAKE_CROSS_PLATFORM_COMPILER})
else()
    set(CMAKE_COMPILE ${CMAKE_CXX_COMPILER})
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/framework)
    add_subdirectory(framework)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/op_host)
    add_subdirectory(op_host)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel)
    add_subdirectory(op_kernel)
endif()
if(ENABLE_TEST AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/testcases)
    add_subdirectory(testcases)
endif()

# modify vendor_name in install.sh and upgrade.sh
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/scripts/install.sh ${CMAKE_BINARY_DIR}/scripts/upgrade.sh
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/scripts
    COMMAND cp -r ${CMAKE_SOURCE_DIR}/scripts/* ${CMAKE_BINARY_DIR}/scripts/
    COMMAND sed -i "s/vendor_name=customize/vendor_name=${vendor_name}/g" ${CMAKE_BINARY_DIR}/scripts/*
)
add_custom_target(modify_vendor ALL DEPENDS ${CMAKE_BINARY_DIR}/scripts/install.sh ${CMAKE_BINARY_DIR}/scripts/upgrade.sh)

get_system_info(SYSTEM_INFO)

# gen version.info
add_custom_target(gen_version_info ALL
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cmake/util/gen_version_info.sh ${ASCEND_CANN_PACKAGE_PATH} ${CMAKE_CURRENT_BINARY_DIR}
)

install(DIRECTORY ${CMAKE_BINARY_DIR}/scripts/ DESTINATION . FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_READ)

install(FILES ${CMAKE_SOURCE_DIR}/custom.proto DESTINATION packages OPTIONAL)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/version.info
        DESTINATION packages/vendors/${vendor_name}/)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/asccomm_api_bench_package.info
        DESTINATION packages/vendors/${vendor_name}/)

# CPack config
set(CPACK_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${CMAKE_PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "CPack opp project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CPack opp project")
set(CPACK_PACKAGE_DIRECTORY ${CMAKE_INSTALL_PREFIX})
set(CPACK_PACKAGE_FILE_NAME "custom_opp_${SYSTEM_INFO}.run")
set(CPACK_GENERATOR External)
set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
set(CPACK_EXTERNAL_PACKAGE_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/makeself.cmake)
set(CPACK_MC2_BENCH_MAKESELF "${MC2_BENCH_MAKESELF}")
set(CPACK_EXTERNAL_BUILT_PACKAGES ${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Linux/External/${CPACK_PACKAGE_FILE_NAME}/${CPACK_PACKAGE_FILE_NAME})
include(CPack)
