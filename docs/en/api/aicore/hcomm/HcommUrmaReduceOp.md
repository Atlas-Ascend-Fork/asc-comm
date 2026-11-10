# HcommUrmaReduceOp

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:32:16.479Z pushedAt=2026-11-09T06:47:54.966Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Header file path: `"hcomm/hcomm_common.h"` (also available directly after including `"hcomm/hcomm.h"`).

Defines the reduction operator types for write reduction operations, used as the template parameter of [WriteReduceNbi](./WriteReduceNbi.md): reduces the local element and the corresponding element in the remote `dst` using the specified operator, then writes the result to the remote `dst`.

## Function Prototype

```cpp
enum class HcommUrmaReduceOp : uint32_t {
    MAX = 0x8U,
    MIN = 0x9U,
    SUM = 0xAU
};
```

## Member Description

**Table 1** HcommUrmaReduceOp member description

| Member | Value | Description |
| --- | --- | --- |
| `MAX` | 0x8 | Takes the maximum of the local element and the remote element. |
| `MIN` | 0x9 | Takes the minimum of the local element and the remote element. |
| `SUM` | 0xA | Sums the local element and the remote element. |
