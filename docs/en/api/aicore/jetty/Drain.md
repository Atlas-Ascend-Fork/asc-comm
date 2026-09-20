# Drain

## Overview

Polls the CQ until all submitted WQEs complete.

Drain keeps consuming CQEs until every submitted WQE completes, and advances the completion queue through the CQ doorbell.

## Prototype

SIMD path:

```cpp
__aicore__ inline void Drain(...);
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
