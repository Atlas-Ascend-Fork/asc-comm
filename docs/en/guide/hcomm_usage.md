# asccomm Usage Guide

## Overview
asccomm is the AICore-side point-to-point communication interface provided by asc-comm. Users select communication protocols via the `AscendC::asccomm` template and specify communication channels using `channel_handle`. This repository mainly hosts AIV direct-driven implementations covering two paths: RoCE and UBC_CTP/URMA.

## Basic Workflow
1. Include the header file.
```cpp
#include "hcomm/hcomm.h"
```

2. Instantiate the asccomm object.
```cpp
AscendC::asccomm<AscendC::comm_protocol_ubc_ctp> hcomm;
```

3. Call `init` to initialize the temporary workspace.
```cpp
int32_t ret = hcomm.init(tmpBuf, tmpLen);
```

4. Submit communication tasks.
```cpp
ret = hcomm.write_nbi(channel, dst, src, len);
ret = hcomm.read_nbi(channel, dst, src, len);
ret = hcomm.write_with_notify_nbi(channel, dst, src, len, notifyAddr, notifyVal);
ret = hcomm.atomic_faa<uint64_t>(channel, remoteCounter, fetchAddr, addVal);
ret = hcomm.atomic_cas<uint64_t>(channel, remoteValue, fetchAddr, compareVal, swapVal);
```

5. If `commit = false` is set during task submission, explicitly invoke `commit`.
```cpp
ret = hcomm.write_nbi<false>(channel, dst, src, len);
ret = hcomm.commit(channel);
```

6. Call `drain` to wait for task completion.
```cpp
ret = hcomm.drain(channel);
```

## Protocol Description
| Protocol | Description |
| --- | --- |
| `comm_protocol_roce` | RoCE point-to-point communication path. Supports `read_nbi`, `write_nbi`, `commit`, `drain`. `write_with_notify_nbi` is not supported. |
| `comm_protocol_ubc_ctp` | UBC CTP/URMA path. Supports `read_nbi`, `write_nbi`, `write_with_notify_nbi`, `atomic_faa`, `atomic_cas`, `commit`, `drain`. |

## Notes
- Both `comm_protocol_roce` and `comm_protocol_ubc_ctp` paths require a temporary workspace allocated via `init`. The minimum workspace size is currently 512 bytes.
- When initialized with `__ubuf__ uint8_t*`, the implementation aligns the start address of the temporary workspace to 32 bytes. When initialized with `LocalTensor`, the caller must ensure the tensor capacity meets workspace requirements.
- The caller is responsible for initializing and maintaining the channel entity referenced by `channel_handle`.
- Source addresses, destination addresses and transfer lengths must comply with underlying protocol and hardware constraints.
- `write_with_notify_nbi`, `atomic_faa` and `atomic_cas` are only available for the `comm_protocol_ubc_ctp` path.
- Supported data types for atomic operations are limited to `int32_t`, `uint32_t`, `int64_t` and `uint64_t`.
- For the `comm_protocol_ubc_ctp` path: a single `write_with_notify_nbi`, `atomic_faa` or `atomic_cas` task occupies 2 WQE blocks; regular `read_nbi` / `write_nbi` tasks occupy 1 WQE block.
- Return value `0` indicates success; `-1` indicates failure.

## Sample
Refer to [hcomm_write_read_nbi](../../../examples/hcomm_write_read_nbi/README_en.md) to learn about AIV Kernel-side API invocation and Host-side communication resource creation workflow. This sample uses `COMM_ENGINE_AIV` and `comm_protocol_ubc_ctp` exclusively and does not cover the RoCE path.

This sample executes symmetric `write_nbi` and `read_nbi` in a two-card scenario:
```cpp
hcomm.write_nbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
hcomm.read_nbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);
hcomm.drain(channel);
```

The sample requires Ascend 950PR / Ascend 950DT and at least two NPUs for runtime verification. Single-NPU environments only support compilation checks.
