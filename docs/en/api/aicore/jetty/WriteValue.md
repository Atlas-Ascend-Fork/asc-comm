# WriteValue

## Overview

Submits an inline immediate-value write WQE that carries `value` inside the WQE.

WriteValue carries the immediate value inline in the WQE and requires no local source buffer.

## Prototype

SIMD path:

```cpp
__aicore__ inline void WriteValue(...);
```

See the class definition in [Jetty](./Jetty.md).

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `value` | Input | The immediate value written to the remote side; the size of T must fit the spare space of one WQEBB. |

## Return Value

None (`void` interface).

## Constraints

- Only the `COMM_PROTOCOL_UB_CTP` path on Ascend 950 is supported.
- See the usage constraints in [Jetty](./Jetty.md).
