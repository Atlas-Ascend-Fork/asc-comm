# Hcomm Usage Guide

## Overview
Hcomm is the AICore-side point-to-point communication interface provided by asc-comm. Users select communication protocols via the `AscendC::Hcomm` template and specify communication channels using `ChannelHandle`. This repository mainly hosts AIV direct-driven implementations covering two paths: RoCE and UBC_CTP/URMA.

## Basic Workflow
1. Include the header file.
```cpp
#include "hcomm/hcomm.h"
```

2. Instantiate the Hcomm object.
```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
```

3. Call `Init` to initialize the temporary workspace.
```cpp
int32_t ret = hcomm.Init(tmpBuf, tmpLen);
```

4. Submit communication tasks.
```cpp
ret = hcomm.WriteNbi(channel, dst, src, len);
ret = hcomm.ReadNbi(channel, dst, src, len);
ret = hcomm.WriteWithNotifyNbi(channel, dst, src, len, notifyAddr, notifyVal);
ret = hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchAddr, addVal);
ret = hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchAddr, compareVal, swapVal);
```

5. If `commit = false` is set during task submission, explicitly invoke `Commit`.
```cpp
ret = hcomm.WriteNbi<false>(channel, dst, src, len);
ret = hcomm.Commit(channel);
```

6. Call `Drain` to wait for task completion.
```cpp
ret = hcomm.Drain(channel);
```

## Protocol Description
| Protocol | Description |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | RoCE point-to-point communication path. Supports `ReadNbi`, `WriteNbi`, `Commit`, `Drain`. `WriteWithNotifyNbi` is not supported. |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA path. Supports `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, `AtomicCAS`, `Commit`, `Drain`. |

## Notes
- Both `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP` paths require a temporary workspace allocated via `Init`. The minimum workspace size is currently 512 bytes.
- When initialized with `__ubuf__ uint8_t*`, the implementation aligns the start address of the temporary workspace to 32 bytes. When initialized with `LocalTensor`, the caller must ensure the tensor capacity meets workspace requirements.
- The caller is responsible for initializing and maintaining the channel entity referenced by `ChannelHandle`.
- Source addresses, destination addresses and transfer lengths must comply with underlying protocol and hardware constraints.
- `WriteWithNotifyNbi`, `AtomicFAA` and `AtomicCAS` are only available for the `COMM_PROTOCOL_UBC_CTP` path.
- Supported data types for atomic operations are limited to `int32_t`, `uint32_t`, `int64_t` and `uint64_t`.
- For the `COMM_PROTOCOL_UBC_CTP` path: a single `WriteWithNotifyNbi`, `AtomicFAA` or `AtomicCAS` task occupies 2 WQE blocks; regular `ReadNbi` / `WriteNbi` tasks occupy 1 WQE block.
- Return value `0` indicates success; `-1` indicates failure.

## Sample
Refer to [hcomm_write_read_nbi](../../../examples/hcomm_write_read_nbi/README_en.md) to learn about AIV Kernel-side API invocation and Host-side communication resource creation workflow. This sample uses `COMM_ENGINE_AIV` and `COMM_PROTOCOL_UBC_CTP` exclusively and does not cover the RoCE path.

This sample executes symmetric `WriteNbi` and `ReadNbi` in a two-card scenario:
```cpp
hcomm.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
hcomm.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);
hcomm.Drain(channel);
```

The sample requires Ascend 950PR / Ascend 950DT and at least two NPUs for runtime verification. Single-NPU environments only support compilation checks.
