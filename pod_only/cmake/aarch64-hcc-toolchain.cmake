set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT TOOLCHAIN_DIR)
    if(DEFINED ENV{TOOLCHAIN_DIR})
        set(TOOLCHAIN_DIR "$ENV{TOOLCHAIN_DIR}")
    else()
        message(FATAL_ERROR "TOOLCHAIN_DIR is not set")
    endif()
endif()

set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-gcc" CACHE PATH "C compiler")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-g++" CACHE PATH "C++ compiler")
set(CMAKE_AR "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ar" CACHE PATH "Archiver")
set(CMAKE_RANLIB "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ranlib" CACHE PATH "Ranlib")

set(CMAKE_SKIP_RPATH TRUE)
