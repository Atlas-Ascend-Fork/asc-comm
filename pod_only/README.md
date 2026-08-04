# HCOMM POD data plane

This directory is the source boundary for the POD-based HCOMM primitive APIs.
It contains only the public POD API and resource ABI headers plus the POD C
adapter, RTSQ runtime, and UB/RoCE transport implementations.

All asc-comm source and header dependencies used by this module are contained
under this directory and keep the same `include/`, `src/`, and `cmake/`
structure as the parent repository. The only external dependencies are the C++
toolchain and the CANN SDK (HAL, securec, driver headers, and logging ABI).

Public consumers that only need the POD API can include:

```c
#include "hcomm_pod_primitives.h"
#include "hcomm_pod_resource.h"
```

The original `hcomm_primitives.h` declarations remain available for source
compatibility. Both headers can be included in the same translation unit.

`pod_only` is an independent CANN CMake project at the same build level as
`asc-comm`. It uses its own copy of the CANN CMake bootstrap and toolchain files,
and does not include build files or source files from the parent directory.

Standalone build:

```bash
export TOOLCHAIN_DIR="$ASCEND_HOME_PATH/toolkit/toolchain/hcc"
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-hcc-toolchain.cmake \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH"
cmake --build build -j
```
