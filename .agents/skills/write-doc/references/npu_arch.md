# 概述
本文档主要用于介绍芯片版本、npu-type 与 npu_arch 的映射关系。

在资料中，不同芯片用芯片版本称呼，比如：
1. Atlas A3 训练系列产品/Atlas A3 推理系列产品支持 xxxxx 功能
2. 在文档中使用 `<cann-filter npu-type="xxx">` 标签区分不同产品的能力差异
3. 在代码中，不同芯片版本的代码用版本隔离宏 `__NPU_ARCH__` 区分，比如:
```cpp
#if __NPU_ARCH__ == 3510
// 3510 代码
#endif

#if __NPU_ARCH__ == 2201
// 2201 代码
#endif
```

# 映射关系

| 芯片版本 | npu-type | 版本号 |
|---------|---------|--------|
| Ascend 950PR/Ascend 950DT | 950 | 3510 |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | A3 | 2201 |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | 910b | 2201 |
