cmake_minimum_required(VERSION 3.16)

project(asc_comm LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASCCOMM_BUILD_CCU "Build migrated CCU compile-check targets" ON)

if(ASCCOMM_BUILD_CCU)
    add_subdirectory(src/ccu)
endif()
