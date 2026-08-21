set(ASCCOMM_AICPU_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
    ${CMAKE_CURRENT_LIST_DIR}/primitives/launch/asccomm_launch.cc
    ${CMAKE_CURRENT_LIST_DIR}/resource/common/asccomm_runtime.cc
    ${CMAKE_CURRENT_LIST_DIR}/resource/common/asccomm_transport.cc
    ${CMAKE_CURRENT_LIST_DIR}/resource/connection/ub_conn_lite.cc
    ${CMAKE_CURRENT_LIST_DIR}/resource/stream/sqe_build_a5.cc
)

list(LENGTH ASCCOMM_AICPU_SOURCES _aicpu_src_count)
message(STATUS "asccomm_aicpu: attach ${_aicpu_src_count} AICPU source files")
target_sources(asccomm_aicpu PRIVATE ${ASCCOMM_AICPU_SOURCES})
