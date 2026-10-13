# CommProtocol

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

定义通信协议类型枚举。用于指定`AscendC::Hcomm`模板的通信协议。

## 定义原型

```cpp
typedef enum {
    COMM_PROTOCOL_RESERVED = -1,                  // 保留协议类型
    COMM_PROTOCOL_HCCS = 0,                       // HCCS协议
    COMM_PROTOCOL_ROCE = 1,                       // RDMA over Converged Ethernet
    COMM_PROTOCOL_PCIE = 2,                       // PCIE协议
    COMM_PROTOCOL_SIO = 3,                        // SIO协议
    COMM_PROTOCOL_UB_CTP = 4,                     // 华为统一总线UB_CTP
    COMM_PROTOCOL_UBC_CTP = COMM_PROTOCOL_UB_CTP, // 兼容旧协议名UBC_CTP
    COMM_PROTOCOL_UBC_TP = 5,                     // 历史兼容协议UBC_TP，不建议新开发使用
    COMM_PROTOCOL_UB_MEM = 6,                     // UB_MEM
    COMM_PROTOCOL_UBOE = 7,                       // UBoE
    COMM_PROTOCOL_HCCS_ONLY = 8,                  // 一卡双DIE使用HCCS
    COMM_PROTOCOL_UB_RTP = 9,                     // UB_RTP
    COMM_PROTOCOL_UBG = COMM_PROTOCOL_UB_RTP,     // 兼容旧协议名UBG
} CommProtocol;
```

## 成员说明

**表1** CommProtocol成员说明

| 成员 | 取值 | 描述 |
| --- | --- | --- |
| `COMM_PROTOCOL_RESERVED` | -1 | 保留协议类型。 |
| `COMM_PROTOCOL_HCCS` | 0 | HCCS协议。 |
| `COMM_PROTOCOL_ROCE` | 1 | RDMA over Converged Ethernet协议。 |
| `COMM_PROTOCOL_PCIE` | 2 | PCIE协议。 |
| `COMM_PROTOCOL_SIO` | 3 | SIO协议。 |
| `COMM_PROTOCOL_UB_CTP` | 4 | 华为统一总线UB_CTP协议。 |
| `COMM_PROTOCOL_UBC_CTP` | 4 | `COMM_PROTOCOL_UB_CTP`的别名，兼容旧协议名。 |
| `COMM_PROTOCOL_UBC_TP` | 5 | 历史兼容协议，不建议新开发使用。 |
| `COMM_PROTOCOL_UB_MEM` | 6 | UB_MEM协议。 |
| `COMM_PROTOCOL_UBOE` | 7 | UBoE协议。 |
| `COMM_PROTOCOL_HCCS_ONLY` | 8 | 一卡双DIE使用HCCS。 |
| `COMM_PROTOCOL_UB_RTP` | 9 | UB_RTP协议。 |
| `COMM_PROTOCOL_UBG` | 9 | `COMM_PROTOCOL_UB_RTP`的别名，兼容旧协议名。 |

## 约束说明

- AICore侧`AscendC::Hcomm`模板支持`COMM_PROTOCOL_UB_CTP`（含别名`COMM_PROTOCOL_UBC_CTP`）与`COMM_PROTOCOL_ROCE`两种协议，默认值为`COMM_PROTOCOL_UB_CTP`；SIMT接口当前仅支持`COMM_PROTOCOL_UB_CTP`。
- Host侧通道协议（`HcclChannelDesc.channelProtocol`）必须与Kernel侧`AscendC::Hcomm`模板协议一致，且须与`HcclRankGraphGetLinks`返回链路的`linkAttr.linkProtocol`匹配。
- 接口的协议支持存在差异：`WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS`与`WriteReduceNbi`仅支持`COMM_PROTOCOL_UB_CTP`协议路径。
