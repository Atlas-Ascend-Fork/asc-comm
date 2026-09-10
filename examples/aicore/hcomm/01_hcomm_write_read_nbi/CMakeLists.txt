cmake_minimum_required(VERSION 3.16)

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture")
if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "hcomm_write_read_nbi only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(hcomm_write_read_nbi LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{ASCEND_HOME_PATH})
    message(FATAL_ERROR "ASCEND_HOME_PATH is not set. Source the CANN environment (set_env.sh) first.")
endif()

set_source_files_properties(hcomm_write_read_nbi_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(hcomm_write_read_nbi
    hcomm_write_read_nbi.asc
    hcomm_write_read_nbi_kernel.cpp
)



target_include_directories(hcomm_write_read_nbi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    $ENV{ASCEND_HOME_PATH}/include
)

target_link_directories(hcomm_write_read_nbi PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(hcomm_write_read_nbi PRIVATE
    hcomm
    hccl
    ascendcl
    acl_rt
    runtime
    c_sec
    stdc++
    dl
    pthread
)

target_compile_options(hcomm_write_read_nbi PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

set_target_properties(hcomm_write_read_nbi PROPERTIES
    BUILD_RPATH "$ENV{ASCEND_HOME_PATH}/lib64;$ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64"
)
