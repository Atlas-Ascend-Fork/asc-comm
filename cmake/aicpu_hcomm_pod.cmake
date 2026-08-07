set(ASC_COMM_AICPU_HCOMM_POD_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/primitives/api_c_adpt/aicpu_ts_pod_primitives_c_adpt.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/primitives/launch/hcomm_pod_launch.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/common/hcomm_pod_runtime.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/common/hcomm_pod_transport.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/connection/ub_conn_lite.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/stream/sqe_build_a5.cc
)

set(ASC_COMM_AICPU_HCOMM_POD_INCLUDE_DIRS
    ${ASC_COMM_AICPU_INCLUDE_ROOT_DIR}
    ${ASC_COMM_AICPU_INCLUDE_DIR}
    ${ASC_COMM_POD_SRC_DIR}/primitives/api_c_adpt
    ${ASC_COMM_POD_SRC_DIR}/primitives/launch
    ${ASC_COMM_POD_SRC_DIR}/common/compat/hcomm
    ${ASC_COMM_POD_SRC_DIR}/common/compat/hcomm/hccl
    ${ASC_COMM_POD_SRC_DIR}/common/exception
    ${ASC_COMM_POD_SRC_DIR}/common/utils
    ${ASC_COMM_POD_SRC_DIR}/resource/buffer
    ${ASC_COMM_POD_SRC_DIR}/resource/common
    ${ASC_COMM_POD_SRC_DIR}/resource/connection
    ${ASC_COMM_POD_SRC_DIR}/resource/stream
    ${ASC_COMM_POD_DFX_SRC_DIR}
)

set_source_files_properties(${ASC_COMM_AICPU_HCOMM_POD_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-ffunction-sections;-fdata-sections;-fvisibility=hidden;-fvisibility-inlines-hidden"
    INCLUDE_DIRECTORIES "${ASC_COMM_AICPU_HCOMM_POD_INCLUDE_DIRS}"
)
