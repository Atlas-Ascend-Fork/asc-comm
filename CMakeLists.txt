cmake_minimum_required(VERSION 3.16)

project(asc_comm LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASCCOMM_BUILD_CCU "Build migrated CCU compile-check targets" ON)

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
