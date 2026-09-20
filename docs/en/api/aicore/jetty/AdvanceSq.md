# AdvanceSq

## Overview

Advances the SQ head and publishes it to the device-side head address; part of the deferred submission path.

AdvanceSq advances the SQ head by the given number of WQEBBs and stores the packed head to GM. On the SIMT path it also marks the final WQE (FINAL flags and the necessary NOP padding). AdvanceSq does not publish the queue; follow it with PublishSq (SIMT) or RingDoorbell (SIMD).

## Prototype

SIMD path:

```cpp
__aicore__ inline void AdvanceSq(...);
```

See the class definition in [Jetty](./Jetty.md).

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| - | - | See the class definition in [Jetty](./Jetty.md). |

## Return Value

None (`void` interface).

## Constraints

- Only the `COMM_PROTOCOL_UB_CTP` path on Ascend 950 is supported.
- See the usage constraints in [Jetty](./Jetty.md).
