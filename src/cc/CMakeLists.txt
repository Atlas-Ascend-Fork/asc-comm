# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# MC2 communication client (migrated from asc-devkit impl/adv_api/detail/hccl/cc) host-mode skeleton.
# 本文件是 MC2 迁移的构建接线点（迁移系列计划跟踪于 Issue #66）：首期只接线骨架，源列表随后续模块迁入逐步填充。

set(HCCL_CC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ASCCOMM_MC2_PRODUCT_SIDE "host" CACHE STRING
    "MC2 build side: host (default) | device (reserved for later migration stages)")
option(MC2_CLIENT_ENABLE_CCU "Build CCU algorithm implementations into mc2_client" ON)

# devkit 开源构建开关 BUILD_OPEN_PROJECT 对应开放工程，恒 ON，启用 hcomm stub 链接机制
set(BUILD_OPEN_PROJECT ON)

# 本仓 include 目录：按 devkit cc/src/CMakeLists.txt:13-123 平移。
# 骨架期多数迁移目录尚不存在，全列后按存在性过滤；后续模块目录迁入即自动生效。
set(_ASCCOMM_MC2_REPO_DIRS
    ${HCCL_CC_DIR}
    ${HCCL_CC_DIR}/impl
    ${HCCL_CC_DIR}/common
    ${HCCL_CC_DIR}/ccu
    ${HCCL_CC_DIR}/src/common
    ${HCCL_CC_DIR}/src/common/hcomm_dlsym
    ${HCCL_CC_DIR}/src/common/hcomm_dlsym/ccu

    ${HCCL_CC_DIR}/src/ops
    ${HCCL_CC_DIR}/src/ops/op_common
    ${HCCL_CC_DIR}/src/ops/op_common/inc
    ${HCCL_CC_DIR}/src/ops/op_common/executor
    ${HCCL_CC_DIR}/src/ops/op_common/executor/channel
    ${HCCL_CC_DIR}/src/ops/op_common/executor/registry
    ${HCCL_CC_DIR}/src/ops/op_common/external_alg
    ${HCCL_CC_DIR}/src/ops/op_common/selector
    ${HCCL_CC_DIR}/src/ops/op_common/template
    ${HCCL_CC_DIR}/src/ops/op_common/template/registry
    ${HCCL_CC_DIR}/src/ops/op_common/template/aicpu
    ${HCCL_CC_DIR}/src/ops/op_common/template/wrapper
    ${HCCL_CC_DIR}/src/ops/op_common/template/aiv
    ${HCCL_CC_DIR}/src/ops/op_common/template/ccu
    ${HCCL_CC_DIR}/src/ops/op_common/template/ccu/kernel
    ${HCCL_CC_DIR}/src/ops/op_common/topo
    ${HCCL_CC_DIR}/src/ops/interface_graph_mode

    ${HCCL_CC_DIR}/src/ops/reduce_scatter
    ${HCCL_CC_DIR}/src/ops/reduce_scatter/executor
    ${HCCL_CC_DIR}/src/ops/reduce_scatter/selector
    ${HCCL_CC_DIR}/src/ops/reduce_scatter/template
    ${HCCL_CC_DIR}/src/ops/reduce_scatter/template/aicpu
    ${HCCL_CC_DIR}/src/ops/reduce_scatter/template/ccu
    ${HCCL_CC_DIR}/src/ops/reduce_scatter/template/ccu/kernel

    ${HCCL_CC_DIR}/src/ops/all_gather
    ${HCCL_CC_DIR}/src/ops/all_gather/executor
    ${HCCL_CC_DIR}/src/ops/all_gather/selector
    ${HCCL_CC_DIR}/src/ops/all_gather/template
    ${HCCL_CC_DIR}/src/ops/all_gather/template/aicpu
    ${HCCL_CC_DIR}/src/ops/all_gather/template/ccu
    ${HCCL_CC_DIR}/src/ops/all_gather/template/ccu/kernel

    ${HCCL_CC_DIR}/src/ops/all_to_all_v
    ${HCCL_CC_DIR}/src/ops/all_to_all_v/executor
    ${HCCL_CC_DIR}/src/ops/all_to_all_v/selector
    ${HCCL_CC_DIR}/src/ops/all_to_all_v/template
    ${HCCL_CC_DIR}/src/ops/all_to_all_v/template/aicpu
    ${HCCL_CC_DIR}/src/ops/all_to_all_v/template/ccu
    ${HCCL_CC_DIR}/src/ops/all_to_all_v/template/ccu/kernel

    ${HCCL_CC_DIR}/src/ops/all_reduce
    ${HCCL_CC_DIR}/src/ops/all_reduce/executor
    ${HCCL_CC_DIR}/src/ops/all_reduce/selector
    ${HCCL_CC_DIR}/src/ops/all_reduce/template
    ${HCCL_CC_DIR}/src/ops/all_reduce/template/aicpu
    ${HCCL_CC_DIR}/src/ops/all_reduce/template/ccu
    ${HCCL_CC_DIR}/src/ops/all_reduce/template/ccu/kernel

    ${HCCL_CC_DIR}/src/ops/kfc_server
    ${HCCL_CC_DIR}/src/ops/kfc_server/executor
    ${HCCL_CC_DIR}/src/ops/kfc_server/selector
    ${HCCL_CC_DIR}/src/ops/kfc_server/template
    ${HCCL_CC_DIR}/src/ops/kfc_server/template/ccu
    ${HCCL_CC_DIR}/src/ops/kfc_server/template/ccu/kernel

    # include/cc 承接 devkit include/adv_api/hccl 的公开与 internal 头。internal 头按
    # devkit cc/src/CMakeLists.txt:14-22 挂载其子目录
    ${PROJECT_SOURCE_DIR}/include/cc
    ${PROJECT_SOURCE_DIR}/include/cc/internal
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm/types
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm/hccl
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm/pkg_inc/hccl
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm/ccu
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm/pkg_inc/hcomm/ccu
    ${PROJECT_SOURCE_DIR}/include/cc/internal/hcomm/pkg_inc
    # 仓根：供 include/cc/... 路径形 include 解析（对齐 devkit ${ASCENDC_DIR}）
    ${PROJECT_SOURCE_DIR}
)

set(ASCCOMM_MC2_INCLUDE_LIST)
foreach(_dir IN LISTS _ASCCOMM_MC2_REPO_DIRS)
    if(EXISTS "${_dir}")
        list(APPEND ASCCOMM_MC2_INCLUDE_LIST "${_dir}")
    endif()
endforeach()

# CANN 包 include（照搬 devkit cc/src/CMakeLists.txt:92-123；目录不存在仅为编译警告）
list(APPEND ASCCOMM_MC2_INCLUDE_LIST
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/acl
    ${ASCEND_CANN_PACKAGE_PATH}/include/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/include/base/
    ${ASCEND_CANN_PACKAGE_PATH}/include/driver/
    ${ASCEND_CANN_PACKAGE_PATH}/include/dump/
    ${ASCEND_CANN_PACKAGE_PATH}/include/external/
    ${ASCEND_CANN_PACKAGE_PATH}/include/platform/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm/ccu
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/runtime/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/runtime/runtime/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/aicpu/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/profiling/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/base/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/dump/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/trace/
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/mmpa/
)

if(ASCCOMM_MC2_PRODUCT_SIDE STREQUAL "host")
    # 源列表自 common 模块迁入起填充（host 侧 .cc）；整体迁移计划见 Issue #66。
    # 源为空（骨架期占位）时用占位 TU 保证 add_library 成立。
    # TODO(#66): hccl_mc2.cc / hccl_alloc_ctx_res.cc / mc2_comm_info_reporter.cc
    # depend on ops/op_common headers (topo_host/alg_param/op_common/load_kernel etc);
    # they join the build when the ops module is migrated
    set(ASCCOMM_MC2_CLIENT_SOURCES
        ${HCCL_CC_DIR}/src/common/adapter_acl.cc
        ${HCCL_CC_DIR}/src/common/alg_env_config.cc
        ${HCCL_CC_DIR}/src/common/param_check.cc
        ${HCCL_CC_DIR}/src/common/utils.cc
        ${HCCL_CC_DIR}/src/common/alg_type.cc
        ${HCCL_CC_DIR}/src/common/adapter_error_manager_pub.cc
        ${HCCL_CC_DIR}/src/common/config_log.cc
        ${HCCL_CC_DIR}/src/common/log.cc
        ${HCCL_CC_DIR}/src/common/sal.cc
        ${HCCL_CC_DIR}/src/common/compat.cc
    )
    if(NOT ASCCOMM_MC2_CLIENT_SOURCES)
        set(_ASCCOMM_MC2_CLIENT_SKELETON_SRC ${CMAKE_CURRENT_BINARY_DIR}/mc2_client_skeleton.cc)
        file(WRITE ${_ASCCOMM_MC2_CLIENT_SKELETON_SRC}
            "// mc2_client skeleton placeholder; real source files are wired in later migration stages\n")
        set(ASCCOMM_MC2_CLIENT_SOURCES ${_ASCCOMM_MC2_CLIENT_SKELETON_SRC})
    endif()
    # __FILE__ 注入：日志只带文件名不带路径（对齐 devkit cc/CMakeLists.txt:34-39）。
    # 仅 Makefiles 生成器可用：$(notdir $(abspath ...)) 是 GNU Make 函数语法，
    # Ninja 的 rules.ninja 不展开该语法（bad $-escape 解析错误）。
    # 其他生成器（Ninja 等）走 CMake 默认编译规则，__FILE__ 为源文件全路径，可构建。
    if(CMAKE_GENERATOR MATCHES "Makefiles")
        set(CMAKE_CXX_COMPILE_OBJECT
            "<CMAKE_CXX_COMPILER> <DEFINES> -D__FILE__='\"$(notdir $(abspath <SOURCE>))\"' -Wno-builtin-macro-redefined <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>"
        )
        set(CMAKE_C_COMPILE_OBJECT
            "<CMAKE_C_COMPILER> <DEFINES> -D__FILE__='\"$(notdir $(abspath <SOURCE>))\"' -Wno-builtin-macro-redefined <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>"
        )
    endif()

    # mc2_compat：devkit 侧定义于 hcomm_dlsym/CMakeLists.txt:71（弱符号兜底兼容层）。
    # 12 个 host 侧 dlsym 实现编译为独立 SHARED，链路为 hcomm-stub → mc2_compat → mc2_client；
    # 其 weak 兜底符号必须不与真实 libhcomm 符号绑定期抢占（01/02 报告），故不得并入 mc2_client。
    add_library(mc2_compat SHARED
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hccl_rank_graph_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hccl_res_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hcomm_primitives_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hcomm_dlsym.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hccl_inner_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hcomm_host_profiling_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/ccu_primitives_impl_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/ccu_res_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hccl_ccu_res_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hccl_res_expt_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/hccl_host_comm_dl.cc
        ${HCCL_CC_DIR}/src/common/hcomm_dlsym/ccu_launch_dl.cc
    )
    target_include_directories(mc2_compat PRIVATE ${ASCCOMM_MC2_INCLUDE_LIST})
    target_compile_definitions(mc2_compat PRIVATE
        OPEN_BUILD_PROJECT
        HOST_COMPILE
        _GLIBCXX_USE_CXX11_ABI=0
    )
    set_target_properties(mc2_compat PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    target_compile_options(mc2_compat PRIVATE
        -Werror
        -fno-common
        -fno-strict-aliasing
        -pipe
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g>
        -fstack-protector-all
    )
    target_link_options(mc2_compat PRIVATE
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
        $<$<CONFIG:Release>:-s>
    )
    target_link_directories(mc2_compat PRIVATE
        ${ASCCOMM_CANN_ARCH_DIR}/lib64
    )
    target_link_libraries(mc2_compat PRIVATE
        -Wl,--no-as-needed
        acl_rt
        unified_dlog
        -Wl,--no-as-needed
    )
    install(TARGETS mc2_compat
        DESTINATION "${CMAKE_SYSTEM_PROCESSOR}-linux/lib64"
    )

    add_library(mc2_client SHARED ${ASCCOMM_MC2_CLIENT_SOURCES})

    # TODO(#66): mc2_server.json 生成（parser_ini.py，devkit cc/src/CMakeLists.txt:248-257）待 ops 模块迁入后接线

    target_compile_definitions(mc2_client PRIVATE
        OPEN_BUILD_PROJECT
        _GLIBCXX_USE_CXX11_ABI=0
    )

    target_include_directories(mc2_client PRIVATE
        ${ASCCOMM_MC2_INCLUDE_LIST}
    )

    target_compile_definitions(mc2_client PRIVATE
        HOST_COMPILE
        MC2_CLIENT_ENABLE_CCU=$<BOOL:${MC2_CLIENT_ENABLE_CCU}>
    )

    set_target_properties(mc2_client PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    target_compile_options(mc2_client PRIVATE
        -Werror
        -fno-common
        -fno-strict-aliasing
        -pipe
        -O3
        $<$<CONFIG:Debug>:-g>
        -fstack-protector-all
    )

    # 与 src/ccu 先例一致：CANN 包链接库经架构目录解析
    target_link_directories(mc2_client PRIVATE
        ${ASCCOMM_CANN_ARCH_DIR}/lib64
    )

    target_link_options(mc2_client PRIVATE
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
        $<$<CONFIG:Release>:-s>
    )

    # mc2_compat 已在 host 分支上方定义（hcomm_dlsym 12 个 host 侧 .cc），
    # 由 add_library 顺序保证依赖关系；此处仅兜底校验
    if(TARGET mc2_compat)
        add_dependencies(mc2_client mc2_compat)
    endif()

    # libhcomm must precede mc2_compat: mc2_compat exports weak fallback wrappers
    # that must not preempt real HCOMM symbols during dynamic symbol binding.
    if(BUILD_OPEN_PROJECT)
        set(MC2_CLIENT_HCOMM_STUB_SRC
            ${CMAKE_CURRENT_BINARY_DIR}/mc2_client_hcomm_stub.cc)
        file(WRITE ${MC2_CLIENT_HCOMM_STUB_SRC}
            "extern \"C\" void __mc2_hcomm_stub(void) {}\n")
        add_library(mc2_client_hcomm_stub SHARED
            ${MC2_CLIENT_HCOMM_STUB_SRC})
        set_target_properties(mc2_client_hcomm_stub PROPERTIES
            OUTPUT_NAME hcomm
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/mc2_client_link_stub)
        target_link_options(mc2_client_hcomm_stub PRIVATE
            -Wl,-soname,libhcomm.so)

        target_link_libraries(mc2_client PRIVATE
            -Wl,--no-as-needed
            mc2_client_hcomm_stub
            $<$<TARGET_EXISTS:mc2_compat>:mc2_compat>
            acl_rt
            c_sec
            unified_dlog
            mmpa
            -Wl,--no-as-needed
        )
    endif()

    install(TARGETS mc2_client
        DESTINATION "${CMAKE_SYSTEM_PROCESSOR}-linux/lib64"
    )
else()
    message(FATAL_ERROR
        "ASCCOMM_MC2_PRODUCT_SIDE=${ASCCOMM_MC2_PRODUCT_SIDE} is not supported yet: "
        "only 'host' is implemented ('device' is reserved for later migration stages)")
endif()

# 后续按迁移进度接子目录（对齐 devkit cc/src/CMakeLists.txt:355-363）
# 注：common（10 源）与 hcomm_dlsym（12 源 → mc2_compat）已在根本文件源列表接线，
# 树内 src/common/CMakeLists.txt 随树保留但暂不引用；aicpu_kfc / ops 模块迁入后
# 再评估子目录接线方式（整体计划与裁决记录见 Issue #66）
