cmake_minimum_required(VERSION 3.16)
project(asccomm_send_with_imm_e2e LANGUAGES C CXX)

set(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(HCOMM_ROOT "${ASCCOMM_ROOT}/../hcomm" CACHE PATH "hcomm source tree")

if(NOT DEFINED ENV{ASCEND_HOME_PATH})
    message(FATAL_ERROR "ASCEND_HOME_PATH must be set")
endif()
set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(send_with_imm_server server/main.cpp)
target_include_directories(send_with_imm_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${HCOMM_ROOT}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include
)
target_link_directories(send_with_imm_server PRIVATE
    ${HCOMM_ROOT}/build/src/legacy/ascend910/framework
    ${HCOMM_ROOT}/build/src/legacy/ascend910/platform
    ${HCOMM_ROOT}/build/src/legacy/ascend910/algorithm
    ${HCOMM_ROOT}/build/src/legacy/ascend950
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)
target_link_libraries(send_with_imm_server PRIVATE hcomm hccl_plf hccl_alg c_sec ascendcl acl_rt pthread)

add_executable(send_with_imm_client client/main.cpp)
target_include_directories(send_with_imm_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${HCOMM_ROOT}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include
)
target_link_directories(send_with_imm_client PRIVATE
    ${HCOMM_ROOT}/build/src/legacy/ascend910/framework
    ${HCOMM_ROOT}/build/src/legacy/ascend910/platform
    ${HCOMM_ROOT}/build/src/legacy/ascend910/algorithm
    ${HCOMM_ROOT}/build/src/legacy/ascend950
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)
target_link_libraries(send_with_imm_client PRIVATE hcomm hccl_plf hccl_alg c_sec ascendcl acl_rt pthread)

# Device-side kernel wrapper. Link asccomm_aicpu so the final SO contains
# AsccommSendWithImmKernel and asccomm_send_with_imm_on_thread.
if(NOT TARGET asccomm_aicpu)
    add_subdirectory("${ASCCOMM_ROOT}/src/aicpu" "${CMAKE_BINARY_DIR}/asccomm_aicpu")
endif()

add_library(send_with_imm_aicpu SHARED kernel/send_with_imm_kernel.cc)
target_include_directories(send_with_imm_aicpu PRIVATE
    "${ASCCOMM_ROOT}/include/aicpu/hcomm"
    "${HCOMM_ROOT}/include"
    "${ASCEND_CANN_PACKAGE_PATH}/include"
)
target_link_libraries(send_with_imm_aicpu PRIVATE asccomm_aicpu)

set(SEND_WITH_IMM_AICPU_JSON "${CMAKE_CURRENT_BINARY_DIR}/send_with_imm_aicpu.json")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/send_with_imm_aicpu.json.in"
    "${SEND_WITH_IMM_AICPU_JSON}"
    COPYONLY
)
