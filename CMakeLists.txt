cmake_minimum_required(VERSION 3.16)

project(asccomm_hcomm VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

add_library(asccomm_hcomm INTERFACE)
target_include_directories(asccomm_hcomm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/aicore>
    $<INSTALL_INTERFACE:${CMAKE_SYSTEM_PROCESSOR}-linux/include/aicore>
)

install(TARGETS asccomm_hcomm EXPORT asccomm_hcomm_targets)
install(DIRECTORY include/aicore/hcomm
    DESTINATION ${CMAKE_SYSTEM_PROCESSOR}-linux/include/aicore
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT asccomm_hcomm_targets
    NAMESPACE asccomm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asccomm_hcomm
)

set(CPACK_PACKAGE_NAME "asc-comm-hcomm")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
