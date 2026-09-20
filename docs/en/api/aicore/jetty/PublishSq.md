# PublishSq

## Overview

Writes the 128B DWQE window to publish the queue; part of the SIMT deferred path.

PublishSq writes a pair of NOP WQEs into the SQ and publishes the queue by writing the DWQE window (the 128B before the doorbell address). Unlike AdvanceSq, PublishSq makes the hardware fetch immediately.

## Prototype

SIMD path:

```cpp
__aicore__ inline void PublishSq(...);
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
