include(${CMAKE_CURRENT_LIST_DIR}/aicpu_hcomm_primitives.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/aicpu_hcomm_resource.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/aicpu_hcomm_common.cmake)

set(ASC_COMM_AICPU_DATA_PLANE_SOURCES
    ${ASC_COMM_AICPU_HCOMM_PRIMITIVE_SOURCES}
    ${ASC_COMM_AICPU_HCOMM_RESOURCE_SOURCES}
    ${ASC_COMM_AICPU_HCOMM_COMMON_SOURCES}
)
