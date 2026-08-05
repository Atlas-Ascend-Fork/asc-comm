set(ASC_COMM_AICPU_HCOMM_POD_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/primitives/api_c_adpt/aicpu_ts_pod_primitives_c_adpt.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/primitives/launch/hcomm_pod_launch.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/common/hcomm_pod_runtime.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/common/hcomm_pod_transport.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/connection/ub_conn_lite.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/stream/sqe_build_a5.cc
)

set_source_files_properties(${ASC_COMM_AICPU_HCOMM_POD_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-ffunction-sections;-fdata-sections;-fvisibility=hidden;-fvisibility-inlines-hidden"
)
