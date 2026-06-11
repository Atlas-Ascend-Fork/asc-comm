set(MC2_HOST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_ROOT}/include/aicore
    ${ASCCOMM_ROOT}/include/aicore/mc2
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal
    ${ASCCOMM_ROOT}/include/aicore/hcomm
    ${ASCCOMM_ROOT}/src/aicore/mc2/host
    ${ASCCOMM_ROOT}/src/aicore
    ${ASCCOMM_ROOT}/src/aicore/detail
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

foreach(product_type ${PRODUCT_TYPE_LIST})
    set(target_name asccomm_ut_mc2_host_tiling_${product_type})
    add_executable(${target_name}
        ${ASCCOMM_UT_ROOT}/main.cpp
        platform_stub.cpp
        ascendc_platform_stub.cpp
        test_hccl_tiling.cpp
        ${ASCCOMM_ROOT}/src/aicore/mc2/host/hccl_tiling.cpp
        ${ASCCOMM_ROOT}/src/aicore/detail/host_log.cpp
    )

    asccomm_mc2_add_product_definitions(${target_name} ${product_type})
    target_include_directories(${target_name} PRIVATE ${MC2_HOST_INCLUDE_DIRS})
    target_compile_options(${target_name} PRIVATE
        -fno-access-control
        -Werror
        -UASCENDC_DEBUG
    )
    target_link_directories(${target_name} PRIVATE
        ${ASCEND_CANN_PACKAGE_PATH}/lib64
        ${ASCEND_CANN_ARCH_DIR}/lib64
    )
    target_link_libraries(${target_name} PRIVATE
        intf_llt_pub_basic
        kernel_tiling
        register
        platform
        graph
        graph_base
        unified_dlog
        ascend_protobuf
        error_manager
        mmpa
        c_sec
        dl
    )

    run_llt_test(
        TARGET ${target_name}
        TASK_NUM 1
    )
endforeach()
