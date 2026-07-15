cmake_minimum_required(VERSION 3.16)

project(asccomm VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(ASCCOMM_BUILD_UT "Build asc-comm unit tests" OFF)

add_library(asccomm_headers INTERFACE)
add_library(asccomm::headers ALIAS asccomm_headers)

# Public headers live under include/aicore, while the current public API also
# depends on implementation headers under src/aicore.
target_include_directories(asccomm_headers
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/aicore>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/aicore>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/asccomm/include/aicore>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/asccomm/src/aicore>
)

target_compile_features(asccomm_headers INTERFACE cxx_std_17)

set_target_properties(asccomm_headers PROPERTIES
    EXPORT_NAME headers
)

install(TARGETS asccomm_headers
    EXPORT asccommTargets
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/aicore/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/asccomm/include/aicore
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/aicore/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/asccomm/src/aicore
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/asccommConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/asccommConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asccomm
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/asccommConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT asccommTargets
    FILE asccommTargets.cmake
    NAMESPACE asccomm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asccomm
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/asccommConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/asccommConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asccomm
)

if(ASCCOMM_BUILD_UT)
    add_subdirectory(tests/ut)
endif()
