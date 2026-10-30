# WqeEntry

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

WQE（Work Queue Element）控制配置结构，作为Hcomm数据面接口的`config`模板参数，控制任务的下发顺序、fence、完成记录生成与数据内联行为。`COMM_PROTOCOL_ROCE`下仅`cqe`与`fence`字段生效，其余字段忽略。

## 定义原型

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

## 成员说明

**表1** WqeEntry成员说明

| 成员 | 位宽 | 描述 |
| --- | --- | --- |
| `odr` | 3 | 顺序属性：bit[1:0]控制执行序：`00`为无序（No Order），`01`为弱序（Relax Order），`10`为强序（Strong Order）；bit[2]控制完成序：`0`为无序，`1`为有序。 |
| `fence` | 1 | 置`1`表示当前WQE等待之前所有WQE完成后才执行。 |
| `se` | 1 | 置`1`表示WQE完成后触发远端事件通知（solicited event）。 |
| `cqe` | 1 | 置`1`表示WQE完成后生成完成记录（CQE）。 |
| `inlineEn` | 1 | 置`1`表示数据内联在WQE中，而非通过SGE引用。 |
