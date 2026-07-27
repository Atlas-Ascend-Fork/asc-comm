cmake_minimum_required(VERSION 3.16)

project(asc_comm LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASCCOMM_BUILD_CCU "Build migrated CCU compile-check targets" ON)

if(CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CANN_PACKAGE_PATH "${CUSTOM_ASCEND_CANN_PACKAGE_PATH}")
elseif(ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
    set(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
endif()
set(ASCEND_CANN_PACKAGE_PATH "${_ASCCOMM_CANN_PACKAGE_PATH}" CACHE PATH "CANN package path")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(_ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(_ASCCOMM_CANN_ARCH "x86_64-linux")
endif()
set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}" CACHE PATH
    "Architecture-specific CANN package path")
if(ASCEND_CANN_PACKAGE_PATH AND NOT EXISTS "${ASCCOMM_CANN_ARCH_DIR}")
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux" CACHE PATH
        "Architecture-specific CANN package path" FORCE)
endif()

if(NOT ASCOMM_HCOMM_SOURCE_DIR)
    foreach(_candidate
        "${CMAKE_CURRENT_SOURCE_DIR}/../hcomm"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../hcomm"
        "${CANN_3RD_LIB_PATH}/hcomm")
        if(EXISTS "${_candidate}/CMakeLists.txt")
            get_filename_component(ASCCOMM_HCOMM_SOURCE_DIR "${_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()
set(ASCCOMM_HCOMM_SOURCE_DIR "${ASCCOMM_HCOMM_SOURCE_DIR}" CACHE PATH "Original hcomm source tree")

if(ASCCOMM_BUILD_CCU)
    add_subdirectory(src/ccu)
endif()
