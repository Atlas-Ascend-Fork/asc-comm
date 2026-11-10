# BufDesc

<!-- md-trans-meta sourceCommit=d4022f4798582f07983e8075be980708e26ea16c translatedAt=2026-11-04T09:25:31.087Z pushedAt=2026-11-09T06:47:54.963Z nl=2 -->

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

Header file path: `"hcomm/hcomm_common.h"` (also directly usable after including `"hcomm/hcomm.h"`).

Describes a local GM (Global Memory) segment, used as the discrete memory segment description for batch multi-SGE APIs ([WriteNbi](./WriteNbi.md), [ReadNbi](./ReadNbi.md), [WriteWithNotifyNbi](./WriteWithNotifyNbi.md)): a single call describes multiple local discrete data segments through a `BufDesc` array to aggregate read/write of a contiguous memory segment on the remote end.

## Function Prototype

```cpp
struct BufDesc {
    GM_ADDR addr;
    uint32_t len;
};
```

## Member Description

**Table 1** BufDesc member description

| Member | Type | Description |
| --- | --- | --- |
| `addr` | `GM_ADDR` | Starting GM address of the local memory segment. |
| `len` | `uint32_t` | Length of the memory segment, in bytes. |
