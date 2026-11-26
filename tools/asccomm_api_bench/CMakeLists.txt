# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(asccomm_api_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MC2_BENCH_QUERY_ONLY "Build the query-only backend without operator packages" OFF)
option(MC2_BENCH_NATIVE_QUERY "Build the adapter against an installed formal MC2 SDK" OFF)
option(MC2_BENCH_MPI "Build the MPI coordinator adapter" OFF)
option(MC2_BENCH_HCCL "Build communicator bootstrap against an installed Host HCCL SDK" OFF)
option(MC2_BENCH_DEVICE_BACKEND "Build the real device backend" OFF)
option(MC2_BENCH_DEVICE_PROFILING "Collect native device profiling and submission identities" OFF)
option(MC2_BENCH_CCU_BACKEND "Select the isolated CCU operator APIs for the device backend" OFF)
set(MC2_BENCH_SDK_ROOT "" CACHE PATH "Installed formal MC2 SDK root")
set(MC2_BENCH_CXX11_ABI "0" CACHE STRING "libstdc++ ABI matching the formal MC2 library (0 or 1)")
if(NOT MC2_BENCH_CXX11_ABI MATCHES "^[01]$")
    message(FATAL_ERROR "MC2_BENCH_CXX11_ABI must be 0 or 1")
endif()

add_library(asccomm_api_bench_core STATIC
    src/options.cc src/case_shape.cc src/plan.cc src/alg_query.cc
    src/session.cc src/test_record.cc src/rank_context.cc src/comm_bootstrap.cc
    src/alltoallv_write.cc src/mock_query.cc)
target_include_directories(asccomm_api_bench_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(asccomm_api_bench_core PUBLIC _GLIBCXX_USE_CXX11_ABI=${MC2_BENCH_CXX11_ABI})
target_compile_options(asccomm_api_bench_core PRIVATE -Wall -Wextra -Werror -fstack-protector-strong)

if(MC2_BENCH_NATIVE_QUERY)
    set(MC2_BENCH_SDK_ROOT "" CACHE PATH "Installed formal MC2 SDK root")
    set(MC2_BENCH_QUERY_LIBRARY "" CACHE FILEPATH "Formal library exporting the MC2 C ABI JSON query API")
    set(MC2_BENCH_SDK_EXTRA_INCLUDES "" CACHE STRING "Additional formal SDK include directories")
    set(MC2_BENCH_QUERY_EXTRA_LIBRARIES "" CACHE STRING "Additional formal SDK link dependencies")
    set(MC2_BENCH_QUERY_EXTRA_INCLUDES "" CACHE STRING "Additional formal query include directories")
    set(MC2_BENCH_QUERY_INCLUDE_DIR "" CACHE PATH "Selected hccl_mc2.h directory under the SDK root")
    if(NOT IS_DIRECTORY "${MC2_BENCH_SDK_ROOT}" OR NOT EXISTS "${MC2_BENCH_QUERY_LIBRARY}")
        message(FATAL_ERROR "Set MC2_BENCH_SDK_ROOT and MC2_BENCH_QUERY_LIBRARY to the installed formal SDK and query library")
    endif()
    get_filename_component(_mc2_query_root "${MC2_BENCH_SDK_ROOT}" ABSOLUTE)
    set(_mc2_query_candidates)
    foreach(_suffix asc/include/adv_api/hccl asc/include/hccl asc/include
                    include/adv_api/hccl include/hccl include)
        list(APPEND _mc2_query_candidates "${_mc2_query_root}/${_suffix}")
    endforeach()
    unset(MC2_BENCH_MC2_INCLUDE CACHE)
    unset(MC2_BENCH_MC2_INCLUDE)
    if(MC2_BENCH_QUERY_INCLUDE_DIR)
        get_filename_component(_mc2_query_selected "${MC2_BENCH_QUERY_INCLUDE_DIR}" ABSOLUTE)
        list(FIND _mc2_query_candidates "${_mc2_query_selected}" _mc2_query_index)
        if(_mc2_query_index LESS 0 OR NOT EXISTS "${_mc2_query_selected}/hccl_mc2.h")
            message(FATAL_ERROR "MC2_BENCH_QUERY_INCLUDE_DIR must contain hccl_mc2.h in a supported include directory under the selected SDK root")
        endif()
        set(MC2_BENCH_MC2_INCLUDE "${_mc2_query_selected}")
    else()
        find_path(MC2_BENCH_MC2_INCLUDE hccl_mc2.h PATHS ${_mc2_query_candidates} NO_DEFAULT_PATH)
    endif()
    if(NOT MC2_BENCH_MC2_INCLUDE)
        message(FATAL_ERROR "Formal hccl_mc2.h was not found under MC2_BENCH_SDK_ROOT")
    endif()
    # Public CCU APIs, internal devkit types and HCOMM resource definitions can
    # be installed in different roots, independently of hccl_mc2.h's location.
    # Keep this ordered list aligned with build_backend.py's SDK receipt.
    set(_mc2_sdk_includes "${MC2_BENCH_MC2_INCLUDE}")
    set(_mc2_sdk_suffixes asc/include asc/include/comm_api include include/hcomm)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        list(APPEND _mc2_sdk_suffixes aarch64-linux/pkg_inc)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        list(APPEND _mc2_sdk_suffixes x86_64-linux/pkg_inc)
    endif()
    list(APPEND _mc2_sdk_suffixes pkg_inc)
    foreach(_suffix IN LISTS _mc2_sdk_suffixes)
        if(IS_DIRECTORY "${_mc2_query_root}/${_suffix}")
            list(APPEND _mc2_sdk_includes "${_mc2_query_root}/${_suffix}")
        endif()
    endforeach()
    list(APPEND _mc2_sdk_includes ${MC2_BENCH_QUERY_EXTRA_INCLUDES} ${MC2_BENCH_SDK_EXTRA_INCLUDES})
    list(REMOVE_DUPLICATES _mc2_sdk_includes)
    # Compile and link the actual adapter. This verifies the declared signature,
    # all public enum/range-setter dependencies, and the C ABI JSON query symbol.
    # No replacement declaration, C-symbol lookup, or source-workspace fallback.
    set(_mc2_probe "${CMAKE_CURRENT_BINARY_DIR}/query_probe.cc")
    file(WRITE "${_mc2_probe}" "
        #include \"native_query.h\"
        int main() {
            auto api = asccomm_api_bench::MakeNativeQueryApi();
            return api.get_algorithms && api.set_data_len ? 0 : 1;
        }")
    unset(MC2_BENCH_QUERY_LINKS CACHE)
    try_compile(MC2_BENCH_QUERY_LINKS "${CMAKE_CURRENT_BINARY_DIR}/query_probe"
        SOURCES "${_mc2_probe}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/native_query.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/options.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/case_shape.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/alg_query.cc"
        CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=11" "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_CURRENT_SOURCE_DIR}/src;${_mc2_sdk_includes}"
        COMPILE_DEFINITIONS -D_GLIBCXX_USE_CXX11_ABI=${MC2_BENCH_CXX11_ABI}
        LINK_LIBRARIES "${MC2_BENCH_QUERY_LIBRARY}" ${MC2_BENCH_QUERY_EXTRA_LIBRARIES}
        OUTPUT_VARIABLE _mc2_query_probe_output)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/query_probe.log" "${_mc2_query_probe_output}")
    if(NOT MC2_BENCH_QUERY_LINKS)
        message(FATAL_ERROR "Formal MC2 query adapter did not compile/link. Check the five-parameter C ABI JSON Mc2GetSupportAlgList, Mc2SetCcDataLenRange and SDK dependencies. See query_probe.log. Runtime/device integration remains a separate requirement.")
    endif()
    add_library(asccomm_api_bench_native_query STATIC src/native_query.cc)
    target_include_directories(asccomm_api_bench_native_query PRIVATE ${_mc2_sdk_includes})
    target_compile_definitions(asccomm_api_bench_native_query PUBLIC MC2_BENCH_NATIVE_QUERY=1)
    target_link_libraries(asccomm_api_bench_native_query PUBLIC asccomm_api_bench_core
        "${MC2_BENCH_QUERY_LIBRARY}" ${MC2_BENCH_QUERY_EXTRA_LIBRARIES})
    target_compile_options(asccomm_api_bench_native_query PRIVATE -Wall -Wextra -Werror)
endif()

if(MC2_BENCH_HCCL)
    set(MC2_BENCH_HCCL_ROOT "" CACHE PATH "Installed Host HCCL SDK root")
    set(MC2_BENCH_HCCL_LIBRARY "" CACHE FILEPATH "Installed Host HCCL library")
    set(MC2_BENCH_HCCL_EXTRA_INCLUDES "" CACHE STRING "Additional Host HCCL SDK include directories")
    set(MC2_BENCH_HCCL_EXTRA_LIBRARIES "" CACHE STRING "Additional Host HCCL link dependencies")
    if(NOT IS_DIRECTORY "${MC2_BENCH_HCCL_ROOT}" OR NOT EXISTS "${MC2_BENCH_HCCL_LIBRARY}")
        message(FATAL_ERROR "Set MC2_BENCH_HCCL_ROOT and MC2_BENCH_HCCL_LIBRARY to the installed Host HCCL SDK/library")
    endif()
    # The CCU backend includes window access, whose Host API is supplied by
    # libhcomm in SDKs that split it from libhccl. An explicitly empty value
    # disables discovery; the Python builder supplies its recorded selection.
    if(NOT DEFINED MC2_BENCH_DEVICE_HCOMM_LIBRARY)
        set(_mc2_hcomm_default "")
        if(MC2_BENCH_CCU_BACKEND AND EXISTS "${MC2_BENCH_HCCL_ROOT}/lib64/libhcomm.so")
            set(_mc2_hcomm_default "${MC2_BENCH_HCCL_ROOT}/lib64/libhcomm.so")
        endif()
        set(MC2_BENCH_DEVICE_HCOMM_LIBRARY "${_mc2_hcomm_default}" CACHE FILEPATH "CCU Host communication window library")
    endif()
    set(_mc2_hccl_libraries "${MC2_BENCH_HCCL_LIBRARY}" ${MC2_BENCH_HCCL_EXTRA_LIBRARIES})
    if(MC2_BENCH_CCU_BACKEND AND MC2_BENCH_DEVICE_HCOMM_LIBRARY)
        if(NOT EXISTS "${MC2_BENCH_DEVICE_HCOMM_LIBRARY}" OR IS_DIRECTORY "${MC2_BENCH_DEVICE_HCOMM_LIBRARY}")
            message(FATAL_ERROR "MC2_BENCH_DEVICE_HCOMM_LIBRARY does not name a library file")
        endif()
        list(APPEND _mc2_hccl_libraries "${MC2_BENCH_DEVICE_HCOMM_LIBRARY}")
    endif()
    list(REMOVE_DUPLICATES _mc2_hccl_libraries)
    unset(MC2_BENCH_HCCL_INCLUDE CACHE)
    find_path(MC2_BENCH_HCCL_INCLUDE hccl/hccl.h
        PATHS "${MC2_BENCH_HCCL_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH)
    if(NOT MC2_BENCH_HCCL_INCLUDE)
        message(FATAL_ERROR "Formal hccl/hccl.h was not found under MC2_BENCH_HCCL_ROOT")
    endif()
    set(_mc2_hccl_includes "${MC2_BENCH_HCCL_INCLUDE}" ${MC2_BENCH_HCCL_EXTRA_INCLUDES})
    set(_mc2_comm_probe "${CMAKE_CURRENT_BINARY_DIR}/comm_probe.cc")
    file(WRITE "${_mc2_comm_probe}" "
        #include \"native_comm.h\"
        int main() {
            auto api = asccomm_api_bench::MakeNativeCommApi();
            return api.get_root_info && api.create && api.get_name && api.destroy ? 0 : 1;
        }")
    unset(MC2_BENCH_HCCL_LINKS CACHE)
    try_compile(MC2_BENCH_HCCL_LINKS "${CMAKE_CURRENT_BINARY_DIR}/comm_probe"
        SOURCES "${_mc2_comm_probe}" "${CMAKE_CURRENT_SOURCE_DIR}/src/native_comm.cc"
        CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=11" "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_CURRENT_SOURCE_DIR}/src;${_mc2_hccl_includes}"
        COMPILE_DEFINITIONS -D_GLIBCXX_USE_CXX11_ABI=${MC2_BENCH_CXX11_ABI}
        LINK_LIBRARIES ${_mc2_hccl_libraries}
        OUTPUT_VARIABLE _mc2_comm_probe_output)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/comm_probe.log" "${_mc2_comm_probe_output}")
    if(NOT MC2_BENCH_HCCL_LINKS)
        message(FATAL_ERROR "Host HCCL bootstrap did not compile/link. Check root-info/config/name/destroy APIs and ABI. See comm_probe.log. This does not replace device or multi-node validation.")
    endif()
    add_library(asccomm_api_bench_native_comm STATIC src/native_comm.cc)
    target_include_directories(asccomm_api_bench_native_comm PRIVATE ${_mc2_hccl_includes})
    target_link_libraries(asccomm_api_bench_native_comm PUBLIC asccomm_api_bench_core ${_mc2_hccl_libraries})
    target_compile_options(asccomm_api_bench_native_comm PRIVATE -Wall -Wextra -Werror)
endif()

if(MC2_BENCH_MPI)
    find_package(MPI 3.0 REQUIRED COMPONENTS CXX)
    add_library(asccomm_api_bench_mpi STATIC src/mpi_coordinator.cc src/mpi_context.cc)
    target_link_libraries(asccomm_api_bench_mpi PUBLIC asccomm_api_bench_core MPI::MPI_CXX)
    target_compile_options(asccomm_api_bench_mpi PRIVATE -Wall -Wextra -Werror)
    add_executable(asccomm_api_bench_mpi_check src/mpi_context_check.cc)
    target_link_libraries(asccomm_api_bench_mpi_check PRIVATE asccomm_api_bench_mpi)
    target_compile_options(asccomm_api_bench_mpi_check PRIVATE -Wall -Wextra -Werror)
endif()

if(MC2_BENCH_DEVICE_BACKEND)
    if(NOT MC2_BENCH_MPI OR NOT MC2_BENCH_HCCL)
        message(FATAL_ERROR "The device backend requires MC2_BENCH_MPI=ON and MC2_BENCH_HCCL=ON")
    endif()
    set(MC2_BENCH_DEVICE_OP_INCLUDE "" CACHE PATH "Installed benchmark operator op_api/include directory")
    set(MC2_BENCH_DEVICE_OP_LIBRARY "" CACHE FILEPATH "Installed benchmark libcust_opapi.so")
    set(MC2_BENCH_DEVICE_ACL_LIBRARY "" CACHE FILEPATH "Installed libascendcl.so")
    set(MC2_BENCH_DEVICE_PROFILER_LIBRARY "" CACHE FILEPATH "Installed libmsprofiler.so")
    set(MC2_BENCH_DEVICE_EXTRA_LIBRARIES "" CACHE STRING "Additional native device link libraries")
    if(NOT IS_DIRECTORY "${MC2_BENCH_SDK_ROOT}" OR
       NOT IS_DIRECTORY "${MC2_BENCH_DEVICE_OP_INCLUDE}" OR NOT EXISTS "${MC2_BENCH_DEVICE_OP_LIBRARY}")
        message(FATAL_ERROR "Set MC2_BENCH_SDK_ROOT, MC2_BENCH_DEVICE_OP_INCLUDE and MC2_BENCH_DEVICE_OP_LIBRARY to the installed SDK and benchmark package")
    endif()
    if(NOT MC2_BENCH_DEVICE_ACL_LIBRARY)
        find_library(_mc2_device_acl NAMES ascendcl PATHS "${MC2_BENCH_SDK_ROOT}/lib64" NO_DEFAULT_PATH)
        if(_mc2_device_acl)
            set(MC2_BENCH_DEVICE_ACL_LIBRARY "${_mc2_device_acl}" CACHE FILEPATH "Installed libascendcl.so" FORCE)
        endif()
    endif()
    if(NOT EXISTS "${MC2_BENCH_DEVICE_ACL_LIBRARY}")
        message(FATAL_ERROR "Set MC2_BENCH_DEVICE_ACL_LIBRARY to the installed libascendcl.so")
    endif()
    set(_mc2_device_includes ${_mc2_hccl_includes} "${MC2_BENCH_SDK_ROOT}/include"
        "${MC2_BENCH_DEVICE_OP_INCLUDE}" ${MC2_BENCH_SDK_EXTRA_INCLUDES})
    set(_mc2_device_libraries "${MC2_BENCH_DEVICE_OP_LIBRARY}" "${MC2_BENCH_DEVICE_ACL_LIBRARY}"
        ${MC2_BENCH_DEVICE_EXTRA_LIBRARIES})
    if(MC2_BENCH_DEVICE_PROFILING)
        if(NOT MC2_BENCH_DEVICE_PROFILER_LIBRARY)
            find_library(_mc2_device_profiler NAMES msprofiler PATHS "${MC2_BENCH_SDK_ROOT}/lib64" NO_DEFAULT_PATH)
            if(_mc2_device_profiler)
                set(MC2_BENCH_DEVICE_PROFILER_LIBRARY "${_mc2_device_profiler}" CACHE FILEPATH "Installed libmsprofiler.so" FORCE)
            endif()
        endif()
        if(NOT EXISTS "${MC2_BENCH_DEVICE_PROFILER_LIBRARY}")
            message(FATAL_ERROR "Native profiling requires libmsprofiler.so from the selected CANN SDK")
        endif()
        list(APPEND _mc2_device_libraries "${MC2_BENCH_DEVICE_PROFILER_LIBRARY}")
        set(_mc2_profile_probe "${CMAKE_CURRENT_BINARY_DIR}/profiling_probe.cc")
        file(WRITE "${_mc2_profile_probe}" "
            #include <acl/acl.h>
            #include <acl/acl_prof.h>
            int main() {
                uint32_t device = 0, task = 0;
                int32_t stream = 0;
                aclrtGetSocName();
                aclrtGetThreadLastTaskId(&task);
                aclrtStreamGetId(nullptr, &stream);
                aclprofInit(\".\", 1);
                auto config = aclprofCreateConfig(&device, 1, ACL_AICORE_PIPE_UTILIZATION, nullptr,
                    ACL_PROF_ACL_API | ACL_PROF_TASK_TIME | ACL_PROF_AICORE_METRICS | ACL_PROF_AICPU |
                    ACL_PROF_HCCL_TRACE | ACL_PROF_MSPROFTX | ACL_PROF_RUNTIME_API);
                aclprofStart(config);
                aclprofStop(config);
                aclprofDestroyConfig(config);
                return aclprofFinalize();
            }")
        unset(MC2_BENCH_PROFILING_LINKS CACHE)
        try_compile(MC2_BENCH_PROFILING_LINKS "${CMAKE_CURRENT_BINARY_DIR}/profiling_probe"
            SOURCES "${_mc2_profile_probe}"
            CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=11" "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
                "-DINCLUDE_DIRECTORIES:STRING=${_mc2_device_includes}"
            LINK_LIBRARIES "${MC2_BENCH_DEVICE_ACL_LIBRARY}" "${MC2_BENCH_DEVICE_PROFILER_LIBRARY}"
            OUTPUT_VARIABLE _mc2_profile_probe_output)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/profiling_probe.log" "${_mc2_profile_probe_output}")
        if(NOT MC2_BENCH_PROFILING_LINKS)
            message(FATAL_ERROR "Native profiling ACL APIs did not compile/link. See profiling_probe.log; --no-profiling selects ACL Event-only diagnostics.")
        endif()
    endif()
    find_library(_mc2_device_nnopbase NAMES nnopbase PATHS "${MC2_BENCH_SDK_ROOT}/lib64" NO_DEFAULT_PATH)
    if(_mc2_device_nnopbase)
        list(APPEND _mc2_device_libraries "${_mc2_device_nnopbase}")
    endif()
    add_library(asccomm_api_bench_native_device STATIC
        src/native_device.cc src/legacy/runtime.cc
        src/legacy/operators/hccl_allreduce_test.cc
        src/legacy/operators/hccl_allgather_test.cc
        src/legacy/operators/hccl_reducescatter_test.cc
        src/legacy/operators/hccl_alltoall_test.cc
        src/legacy/operators/hccl_alltoallv_test.cc)
    if(MC2_BENCH_DEVICE_PROFILING)
        target_sources(asccomm_api_bench_native_device PRIVATE src/native_profile.cc src/legacy/profiling.cc)
        target_compile_definitions(asccomm_api_bench_native_device PUBLIC MC2_BENCH_NATIVE_PROFILING=1)
    endif()
    if(MC2_BENCH_CCU_BACKEND)
        target_sources(asccomm_api_bench_native_device PRIVATE src/native_write.cc src/legacy/operators/hccl_alltoallv_write_test.cc)
        target_compile_definitions(asccomm_api_bench_native_device PUBLIC MC2_BENCH_CCU_BACKEND=1)
        target_link_libraries(asccomm_api_bench_native_device PUBLIC asccomm_api_bench_native_comm)
    endif()
    target_include_directories(asccomm_api_bench_native_device PUBLIC ${_mc2_device_includes}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy" "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy/operators")
    target_link_libraries(asccomm_api_bench_native_device PUBLIC asccomm_api_bench_core asccomm_api_bench_mpi ${_mc2_device_libraries})
    target_compile_options(asccomm_api_bench_native_device PRIVATE -Wall -Wextra -Werror)
    add_executable(asccomm_api_bench_backend src/native_backend.cc)
    target_link_libraries(asccomm_api_bench_backend PRIVATE asccomm_api_bench_native_device
        asccomm_api_bench_native_comm asccomm_api_bench_mpi)
    if(MC2_BENCH_NATIVE_QUERY)
        target_link_libraries(asccomm_api_bench_backend PRIVATE asccomm_api_bench_native_query)
    endif()
    target_compile_options(asccomm_api_bench_backend PRIVATE -Wall -Wextra -Werror)
endif()

if(MC2_BENCH_QUERY_ONLY)
    if(NOT MC2_BENCH_MPI OR NOT MC2_BENCH_HCCL OR MC2_BENCH_DEVICE_BACKEND OR MC2_BENCH_DEVICE_PROFILING)
        message(FATAL_ERROR "Query-only requires MPI/HCCL and excludes device execution/profiling targets")
    endif()
    set(MC2_BENCH_DEVICE_ACL_LIBRARY "" CACHE FILEPATH "Installed libascendcl.so")
    if(NOT MC2_BENCH_DEVICE_ACL_LIBRARY)
        find_library(_mc2_query_acl NAMES ascendcl PATHS "${MC2_BENCH_SDK_ROOT}/lib64" NO_DEFAULT_PATH)
        set(MC2_BENCH_DEVICE_ACL_LIBRARY "${_mc2_query_acl}")
    endif()
    if(NOT EXISTS "${MC2_BENCH_DEVICE_ACL_LIBRARY}")
        message(FATAL_ERROR "Query-only requires libascendcl.so from the selected SDK")
    endif()
    add_executable(asccomm_api_bench_query src/native_backend.cc)
    target_compile_definitions(asccomm_api_bench_query PRIVATE MC2_BENCH_QUERY_ONLY=1)
    target_include_directories(asccomm_api_bench_query PRIVATE "${MC2_BENCH_SDK_ROOT}/include" ${MC2_BENCH_SDK_EXTRA_INCLUDES})
    target_link_libraries(asccomm_api_bench_query PRIVATE asccomm_api_bench_native_comm asccomm_api_bench_mpi
        "${MC2_BENCH_DEVICE_ACL_LIBRARY}" ${MC2_BENCH_DEVICE_EXTRA_LIBRARIES})
    if(MC2_BENCH_NATIVE_QUERY)
        target_link_libraries(asccomm_api_bench_query PRIVATE asccomm_api_bench_native_query)
    endif()
    target_compile_options(asccomm_api_bench_query PRIVATE -Wall -Wextra -Werror)
endif()

option(BUILD_TESTING "Build local regression tests" OFF)
if(BUILD_TESTING)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        message(FATAL_ERROR "Local regression tests are not present; configure with BUILD_TESTING=OFF")
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
