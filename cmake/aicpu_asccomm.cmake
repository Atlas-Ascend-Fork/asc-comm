include_guard(GLOBAL)

set(ASCCOMM_AICPU_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/primitives/launch/asccomm_launch.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/common/asccomm_runtime.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/common/asccomm_transport.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/connection/ub_conn_lite.cc
    ${CMAKE_CURRENT_LIST_DIR}/../src/aicpu/hcomm/resource/stream/sqe_build_a5.cc
)

set_source_files_properties(${ASCCOMM_AICPU_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-ffunction-sections;-fdata-sections;-fvisibility=hidden;-fvisibility-inlines-hidden"
)
