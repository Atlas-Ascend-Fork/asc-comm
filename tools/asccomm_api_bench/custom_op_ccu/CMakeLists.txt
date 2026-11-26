# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

cmake_minimum_required(VERSION 3.19)
project(asccomm_api_bench_ccu LANGUAGES C CXX)
set(vendor_name asccomm_api_bench_ccu CACHE STRING "Isolated CCU benchmark vendor" FORCE)
include(cmake/config.cmake)
include(cmake/func.cmake)
include(cmake/intf.cmake)

find_program(MC2_BENCH_MAKESELF NAMES makeself makeself.sh)
if(NOT MC2_BENCH_MAKESELF)
    message(FATAL_ERROR "Install makeself or configure MC2_BENCH_MAKESELF")
endif()
set(MC2_BENCH_ASCENDC_UTIL_DIR "" CACHE PATH "Installed SDK helper directory")
foreach(helper ascendc_gen_options.py parse_ini_to_json.py)
    if(NOT EXISTS "${MC2_BENCH_ASCENDC_UTIL_DIR}/${helper}")
        message(FATAL_ERROR "Missing installed SDK helper ${helper}")
    endif()
endforeach()
if(ASCEND_PACK_SHARED_LIBRARY OR ENABLE_CROSS_COMPILE)
    message(FATAL_ERROR "The isolated CCU profile currently requires native .run packaging")
endif()
set(CMAKE_COMPILE "${CMAKE_CXX_COMPILER}")
configure_file(asccomm_api_bench_ccu_package.info.in asccomm_api_bench_ccu_package.info @ONLY)

add_subdirectory(op_host)
set_target_properties(cust_opapi PROPERTIES OUTPUT_NAME asccomm_api_bench_ccu_opapi)
add_subdirectory(op_kernel)

add_custom_target(modify_vendor ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/scripts" "${CMAKE_BINARY_DIR}/scripts")
get_system_info(SYSTEM_INFO)
add_custom_target(gen_version_info ALL
    COMMAND bash "${CMAKE_SOURCE_DIR}/cmake/util/gen_version_info.sh" "${ASCEND_CANN_PACKAGE_PATH}" "${CMAKE_BINARY_DIR}")
install(DIRECTORY "${CMAKE_BINARY_DIR}/scripts/" DESTINATION .
    FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_READ)
install(FILES "${CMAKE_BINARY_DIR}/version.info" "${CMAKE_BINARY_DIR}/asccomm_api_bench_ccu_package.info"
    DESTINATION "packages/vendors/${vendor_name}")

set(CPACK_PACKAGE_NAME asccomm_api_bench_ccu)
set(CPACK_PACKAGE_DESCRIPTION "MC2 CCU collective benchmark operators")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
set(CPACK_PACKAGE_FILE_NAME "custom_opp_${SYSTEM_INFO}.run")
set(CPACK_GENERATOR External)
set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/makeself.cmake")
set(CPACK_MC2_BENCH_MAKESELF "${MC2_BENCH_MAKESELF}")
set(CPACK_EXTERNAL_BUILT_PACKAGES "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/Linux/External/${CPACK_PACKAGE_FILE_NAME}/${CPACK_PACKAGE_FILE_NAME}")
include(CPack)
