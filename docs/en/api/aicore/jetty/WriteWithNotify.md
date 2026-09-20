# WriteWithNotify

## Overview

Submits a URMA write WQE that stores a notify value on the remote side at completion.

WriteWithNotify attaches a completion notification to the ordinary write WQE: once the hardware finishes the write, it stores `notifyValue` to `notifyAddr`.

## Prototype

SIMD path:

```cpp
__aicore__ inline void WriteWithNotify(...);
```

See the class definition in [Jetty](./Jetty.md).

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `notifyAddr` | Output | The remote notify GM address. |
| `notifyValue` | Input | The 64-bit value written to `notifyAddr` at completion. |

## Return Value

None (`void` interface).

## Constraints

- Only the `COMM_PROTOCOL_UB_CTP` path on Ascend 950 is supported.
- See the usage constraints in [Jetty](./Jetty.md).
