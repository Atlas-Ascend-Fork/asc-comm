cmake_minimum_required(VERSION 3.16)

project(asc_comm_pod_data_plane LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    else()
        set(ASCEND_CANN_PACKAGE_PATH "/usr/local/Ascend/ascend-toolkit/latest")
    endif()
endif()

set(POD_HCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/aicpu/hcomm")

set(POD_SOURCES
    src/aicpu/hcomm/primitives/api_c_adpt/aicpu_ts_pod_primitives_c_adpt.cc
    src/aicpu/hcomm/primitives/launch/hcomm_pod_launch.cc
    src/aicpu/hcomm/resource/common/hcomm_pod_runtime.cc
    src/aicpu/hcomm/resource/common/hcomm_pod_transport.cc
    src/aicpu/hcomm/resource/connection/ub_conn_lite.cc
    src/aicpu/hcomm/resource/stream/sqe_build_a5.cc
)

add_library(aicpu_pod_data_plane SHARED ${POD_SOURCES})

target_compile_definitions(aicpu_pod_data_plane PRIVATE
    HCCD
    CCL_KERNEL_AICPU
    OPEN_BUILD_PROJECT
    _GLIBCXX_USE_CXX11_ABI=1
)

target_compile_options(aicpu_pod_data_plane PRIVATE
    -Werror
    -Wfloat-equal
    -Wall
    -fno-common
    -fstack-protector-strong
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -pipe
    -O3
)

target_include_directories(aicpu_pod_data_plane PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu
    ${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu/hcomm
    ${POD_HCOMM_ROOT}/primitives/api_c_adpt
    ${POD_HCOMM_ROOT}/primitives/launch
    ${POD_HCOMM_ROOT}/common/compat/hcomm
    ${POD_HCOMM_ROOT}/common/compat/hcomm/hccl
    ${POD_HCOMM_ROOT}/common/exception
    ${POD_HCOMM_ROOT}/common/utils
    ${POD_HCOMM_ROOT}/resource/buffer
    ${POD_HCOMM_ROOT}/resource/common
    ${POD_HCOMM_ROOT}/resource/connection
    ${POD_HCOMM_ROOT}/resource/stream
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/dfx
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/acl
    ${ASCEND_CANN_PACKAGE_PATH}/include/driver
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/base
)

find_library(POD_ASCEND_HAL ascend_hal
    HINTS
        ${ASCEND_CANN_PACKAGE_PATH}/devlib/device
        ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/devlib/device
    REQUIRED
    NO_DEFAULT_PATH
)
find_library(POD_SECUREC c_sec
    HINTS
        ${ASCEND_CANN_PACKAGE_PATH}/lib64/device/lib64
        ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/lib64/device/lib64
    REQUIRED
    NO_DEFAULT_PATH
)

target_link_libraries(aicpu_pod_data_plane PRIVATE
    ${POD_ASCEND_HAL}
    ${POD_SECUREC}
    dl
    pthread
)

target_link_options(aicpu_pod_data_plane PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--gc-sections
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/pod_exports.map
)

set_target_properties(aicpu_pod_data_plane PROPERTIES SKIP_BUILD_RPATH TRUE)
