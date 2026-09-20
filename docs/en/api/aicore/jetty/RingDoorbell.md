# RingDoorbell

## Overview

Rings the SQ doorbell to publish the enqueued WQEs, supporting the SIMT-build/SIMD-publish mixed mode.

RingDoorbell stores the current SQ head to the doorbell address. In the mixed mode the SIMD side calls this after the SIMT side finishes its WQE writes; since a doorbell write cannot order writes issued by another core, the SIMT writes must be visible beforehand.

## Prototype

SIMD path:

```cpp
__aicore__ inline void RingDoorbell(...);
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
