target_sources(asccomm_aicpu PRIVATE
    primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
    primitives/launch/asccomm_launch.cc
    resource/common/asccomm_runtime.cc
    resource/common/asccomm_transport.cc
    resource/connection/ub_conn_lite.cc
    resource/stream/sqe_build_a5.cc
)

set_source_files_properties(
    primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc
    primitives/launch/asccomm_launch.cc
    resource/common/asccomm_runtime.cc
    resource/common/asccomm_transport.cc
    resource/connection/ub_conn_lite.cc
    resource/stream/sqe_build_a5.cc
    PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-fdata-sections;-fvisibility=hidden;-fvisibility-inlines-hidden"
)
