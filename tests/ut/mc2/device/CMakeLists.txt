set(MC2_DEVICE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_ROOT}
    ${ASCCOMM_ROOT}/include/aicore
    ${ASCCOMM_ROOT}/include/aicore/mc2
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal/hcomm
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal/hcomm/hccl
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal/hcomm/pkg_inc
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal/hcomm/pkg_inc/hccl
    ${ASCCOMM_ROOT}/include/aicore/mc2/internal/hcomm/pkg_inc/hcomm/ccu
    ${ASCCOMM_ROOT}/include/aicore/hcomm
    ${ASCCOMM_TIKICPU_INCLUDE_DIR}
    ${ASCCOMM_CANN_INCLUDE_DIRS}
)

foreach(product_type ${PRODUCT_TYPE_LIST})
    set(src_list)
    if(product_type STREQUAL "ascend910B1_AIC")
        set(tikicpu_product_dir Ascend910B1)
        list(APPEND src_list
            hccl/test_hccl.cpp
            hcomm/test_hcomm.cpp
        )
    elseif(product_type STREQUAL "ascend950pr_9599_AIV")
        set(tikicpu_product_dir Ascend950PR_9599)
        list(APPEND src_list
            hccl/test_hccl_aicpu_950.cpp
            hcomm/test_hcomm_roce_950.cpp
            hcomm/test_hcomm_urma_950.cpp
        )
    endif()

    if(NOT src_list)
        continue()
    endif()

    set(target_name asccomm_ut_mc2_device_${product_type})
    add_executable(${target_name}
        ${ASCCOMM_UT_ROOT}/main_global.cpp
        k3_pvwrap.cpp
        ${src_list}
    )

    asccomm_mc2_add_product_definitions(${target_name} ${product_type})
    target_compile_definitions(${target_name} PRIVATE ASCENDC_CPU_DEBUG=1)
    target_include_directories(${target_name} PRIVATE ${MC2_DEVICE_INCLUDE_DIRS})
    target_compile_options(${target_name} PRIVATE
        -fno-access-control
        -Werror
        -UASCENDC_DEBUG
    )
    target_link_directories(${target_name} PRIVATE
        ${ASCEND_CANN_PACKAGE_PATH}/lib64
        ${ASCEND_CANN_ARCH_DIR}/lib64
        ${ASCCOMM_TIKICPU_DIR}
        ${ASCCOMM_TIKICPU_DIR}/${tikicpu_product_dir}
        ${ASCEND_CANN_PACKAGE_PATH}/tools/simulator/${tikicpu_product_dir}/lib
    )
    target_link_libraries(${target_name} PRIVATE
        intf_llt_pub_basic
        kernel_tiling
        -Wl,--no-as-needed
        tikcpp_debug
        cpudebug
        pem_davinci
        cpudebug_stubreg
        cpudebug_cceprint
        cpudebug_npuchk
        error_manager
        mmpa
        c_sec
        -Wl,--as-needed
    )

    if(TARGET tikicpulib::${product_type})
        target_link_libraries(${target_name} PRIVATE tikicpulib::${product_type})
    endif()

    run_llt_test(
        TARGET ${target_name}
        TASK_NUM 1
    )
endforeach()
