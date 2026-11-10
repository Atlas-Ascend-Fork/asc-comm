# CommProtocol

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:27:42.876Z pushedAt=2026-11-09T06:47:54.964Z nl=2 -->

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

Defines the communication protocol type enumeration. It is used to specify the communication protocol for the `AscendC::Hcomm` template. Currently, only `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UB_CTP` can be used with this template (see [Hcomm](./Hcomm.md)). When other values are passed, common APIs such as **Init**, **WriteNbi**, and **ReadNbi** return `-1` at runtime, while batch, atomic, and lock APIs cause compilation errors.

## Function Prototype

```cpp
typedef enum {
    COMM_PROTOCOL_RESERVED = -1,
    COMM_PROTOCOL_HCCS = 0,
    COMM_PROTOCOL_ROCE = 1,
    COMM_PROTOCOL_PCIE = 2,
    COMM_PROTOCOL_SIO = 3,
    COMM_PROTOCOL_UB_CTP = 4,
    COMM_PROTOCOL_UBC_CTP = COMM_PROTOCOL_UB_CTP,
    COMM_PROTOCOL_UBC_TP = 5,
    COMM_PROTOCOL_UB_MEM = 6,
    COMM_PROTOCOL_UBOE = 7,
    COMM_PROTOCOL_HCCS_ONLY = 8,
    COMM_PROTOCOL_UB_RTP = 9,
    COMM_PROTOCOL_UBG = COMM_PROTOCOL_UB_RTP,
} CommProtocol;
```

## Member Description

**Table 1** CommProtocol member description

| Member | Value | Description |
| --- | --- | --- |
| `COMM_PROTOCOL_RESERVED` | -1 | Reserved protocol type. |
| `COMM_PROTOCOL_HCCS` | 0 | HCCS protocol. |
| `COMM_PROTOCOL_ROCE` | 1 | RDMA over Converged Ethernet protocol. |
| `COMM_PROTOCOL_PCIE` | 2 | PCIE protocol. |
| `COMM_PROTOCOL_SIO` | 3 | SIO protocol. |
| `COMM_PROTOCOL_UB_CTP` | 4 | Huawei Unified Bus UB_CTP protocol. |
| `COMM_PROTOCOL_UBC_CTP` | 4 | Alias of `COMM_PROTOCOL_UB_CTP`, kept for compatibility with the legacy protocol name. |
| `COMM_PROTOCOL_UBC_TP` | 5 | Legacy protocol for compatibility; not recommended for new development. |
| `COMM_PROTOCOL_UB_MEM` | 6 | UB_MEM protocol. |
| `COMM_PROTOCOL_UBOE` | 7 | UBoE protocol. |
| `COMM_PROTOCOL_HCCS_ONLY` | 8 | HCCS used for a single device with two dies. |
| `COMM_PROTOCOL_UB_RTP` | 9 | UB_RTP protocol. |
| `COMM_PROTOCOL_UBG` | 9 | Alias of `COMM_PROTOCOL_UB_RTP`, kept for compatibility with the legacy protocol name. |
