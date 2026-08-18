# HCOMM POD data plane

This directory is the source boundary for the POD-based HCOMM data plane. It
implements the distinct `Hcomm*Pod` primitive symbols so it can be packaged
with the existing HCOMM data plane without symbol conflicts.

HCOMM owns the control plane and resources. The POD implementation consumes
the resource ABI and lookup contract from HCOMM `pkg_inc`; callers keep using
the handles returned by the existing HCOMM control-plane APIs and do not call
a resource converter.

Public consumers that only need the POD API can include:

```c
#include "hcomm/hcomm_pod_primitives.h"
```

The original `hcomm_primitives.h` declarations remain available for source
compatibility. Both headers can be included in the same translation unit.

The HCOMM device packaging flow includes `cmake/aicpu_hcomm_pod.cmake` and
compiles these sources into its AICPU kernel package. The standalone target is
kept for compile verification and requires `HCOMM_SOURCE_DIR` to point at the
HCOMM source tree.

Standalone build:

```bash
export TOOLCHAIN_DIR="$ASCEND_HOME_PATH/toolkit/toolchain/hcc"
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-hcc-toolchain.cmake \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DHCOMM_SOURCE_DIR=/path/to/hcomm
cmake --build build -j
```
