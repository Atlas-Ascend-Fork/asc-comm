# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
#### CPACK to package run #####

# download makeself package
add_cann_third_party(makeself-fetch)

function(pack_custom)
  message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
      message(STATUS "Detected architecture: x86_64")
      set(ARCH x86_64)
  elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
      message(STATUS "Detected architecture: ARM64")
      set(ARCH aarch64)
  else ()
      message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
  endif ()

  install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/custom/
      DESTINATION ${CUSTOM_OPS_OPP_SCRIPTS_PATH}
      FILE_PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE  # 文件权限
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
      DIRECTORY_PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE  # 目录权限
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
      COMPONENT hccl
  )

  # ============= CPack =============
  set(CPACK_INSTALL_PREFIX "/")

  set(CPACK_CMAKE_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
  set(CPACK_CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}")
  set(CPACK_CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
  set(CPACK_CMAKE_CURRENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(CPACK_ARCH "${ARCH}")
  set(CPACK_SET_DESTDIR ON)
  set(CPACK_GENERATOR External)
  set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/makeself_custom.cmake")
  set(CPACK_EXTERNAL_ENABLE_STAGING true)
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
  set(CPACK_3RD_LIB_PATH "${CANN_3RD_LIB_PATH}")

  set(CPACK_CUSTOM_OPS_NAME "${CUSTOM_OPS_NAME}")
  set(CPACK_CUSTOM_OPS_PATH "${CUSTOM_OPS_PATH}")
  set(CPACK_CUSTOM_OPS_VENDOR "${CUSTOM_OPS_VENDOR}")
  set(CPACK_CUSTOM_OPS_OPP_SCRIPTS_PATH "${CUSTOM_OPS_OPP_SCRIPTS_PATH}")

  include(CPack)
endfunction()
