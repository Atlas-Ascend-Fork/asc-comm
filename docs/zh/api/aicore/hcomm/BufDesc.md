# BufDesc

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

描述一个本端GM（Global Memory，全局内存）内存段，用作批量多SGE接口（[WriteNbi](./WriteNbi.md)、[ReadNbi](./ReadNbi.md)、[WriteWithNotifyNbi](./WriteWithNotifyNbi.md)）的离散内存段描述：单次调用通过`BufDesc`数组描述多个本端离散数据段，对远端一段连续内存聚合读写。

## 定义原型

```cpp
struct BufDesc {
    GM_ADDR addr;
    uint32_t len;
};
```

## 成员说明

**表1** BufDesc成员说明

| 成员 | 类型 | 描述 |
| --- | --- | --- |
| `addr` | `GM_ADDR` | 本端内存段起始GM地址。 |
| `len` | `uint32_t` | 内存段长度，单位为字节。 |