cmake_minimum_required(VERSION 3.16)

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture")
if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "one_multi_path only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(one_multi_path LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{ASCEND_HOME_PATH})
    message(FATAL_ERROR "ASCEND_HOME_PATH is not set. Source the CANN environment (set_env.sh) first.")
endif()

set_source_files_properties(ubmem_data_copy.asc PROPERTIES LANGUAGE ASC)

add_executable(one_multi_path
    main.cpp
    ubmem_data_copy.asc
)

# main.cpp 是宿主侧 C++，ubmem_data_copy.asc 是 ASC 源文件；混合目标必须用 ASC 链接器，
# 否则 ASC 运行时桩符号（AscendLaunchKernelWithHostArgs 等）无人解析。
set_target_properties(one_multi_path PROPERTIES
    LINKER_LANGUAGE ASC
)



target_include_directories(one_multi_path PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    $ENV{ASCEND_HOME_PATH}/include
)

target_link_directories(one_multi_path PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(one_multi_path PRIVATE
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

target_compile_options(one_multi_path PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

set_target_properties(one_multi_path PROPERTIES
    BUILD_RPATH "$ENV{ASCEND_HOME_PATH}/lib64;$ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64"
)
