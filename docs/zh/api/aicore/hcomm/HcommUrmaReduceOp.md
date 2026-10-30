# HcommUrmaReduceOp

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

定义写归约操作的归约算子类型，用作[WriteReduceNbi](./WriteReduceNbi.md)的模板参数：将本端元素与远端`dst`中的对应元素按指定算子归约后写入远端`dst`。

## 定义原型

```cpp
enum class HcommUrmaReduceOp : uint32_t {
    MAX = 0x8U,
    MIN = 0x9U,
    SUM = 0xAU
};
```

## 成员说明

**表1** HcommUrmaReduceOp成员说明

| 成员 | 取值 | 描述 |
| --- | --- | --- |
| `MAX` | 0x8 | 取本端元素与远端元素中的最大值。 |
| `MIN` | 0x9 | 取本端元素与远端元素中的最小值。 |
| `SUM` | 0xA | 本端元素与远端元素求和。 |
