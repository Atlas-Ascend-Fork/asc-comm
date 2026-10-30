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

头文件路径：`"hcomm/hcomm_common.h"`（包含`"hcomm/hcomm.h"`后亦可直接使用）。

定义通信协议类型枚举。用于指定`AscendC::Hcomm`模板的通信协议，当前仅`COMM_PROTOCOL_ROCE`与`COMM_PROTOCOL_UB_CTP`可用于该模板（参见[Hcomm](./Hcomm.md)），传入其余取值时，Init、WriteNbi、ReadNbi等普通接口在运行期返回`-1`，批量、原子与锁类接口将导致编译报错。

## 定义原型

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
