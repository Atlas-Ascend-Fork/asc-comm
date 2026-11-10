# WqeEntry

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:55:38.202Z pushedAt=2026-11-09T06:47:54.979Z nl=2 -->

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

WQE (Work Queue Element) control configuration structure, used as the `config` template parameter of the Hcomm data-plane API to control task submission order, fence, completion record generation, and data inline behavior. Under `COMM_PROTOCOL_ROCE`, only the `cqe` and `fence` fields take effect; the remaining fields are ignored.

## Function Prototype

```cpp
#pragma pack(push, 1)
struct WqeEntry {
    uint32_t odr : 3;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inlineEn : 1;
} __attribute__((packed));
#pragma pack(pop)
```

## Member Description

**Table 1** WqeEntry member description

| Member | Bit Width | Description |
| --- | --- | --- |
| `odr` | 3 | Ordering attribute: bit[1:0] controls the execution order: `00` for **No Order**, `01` for **Relax Order**, and `10` for **Strong Order**; bit[2] controls the completion order: `0` for no order and `1` for ordered. |
| `fence` | 1 | Set to `1` to indicate that the current WQE is executed only after all previous WQEs complete. |
| `se` | 1 | Set to `1` to indicate that a remote event notification (solicited event) is triggered after the WQE completes. |
| `cqe` | 1 | Set to `1` to indicate that a completion record (CQE) is generated after the WQE completes. |
| `inlineEn` | 1 | Set to `1` to indicate that the data is inlined in the WQE instead of being referenced through an SGE. |
