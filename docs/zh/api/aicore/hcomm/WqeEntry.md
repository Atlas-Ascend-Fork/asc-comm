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

WQE（Work Queue Element）控制配置结构，作为Hcomm数据面接口的`config`模板参数，控制任务的下发顺序、fence、完成记录生成与数据内联行为。

## 定义原型

```cpp
#pragma pack(push, 1)
struct WqeEntry {
    uint32_t odr : 3;      // 任务下发顺序强度：0为乱序，5为强顺序
    uint32_t fence : 1;    // 置1表示当前WQE等待之前所有WQE完成后才执行
    uint32_t se : 1;       // 置1表示WQE完成后触发远端事件通知
    uint32_t cqe : 1;      // 置1表示WQE完成后生成完成记录（CQE）
    uint32_t inlineEn : 1; // 置1表示数据内联在WQE中，而非通过SGE引用
} __attribute__((packed));
#pragma pack(pop)
```

## 成员说明

**表1** WqeEntry成员说明

| 成员 | 位宽 | 描述 |
| --- | --- | --- |
| `odr` | 3 | 任务下发顺序（ordering）强度：`0`为乱序，`5`为强顺序。 |
| `fence` | 1 | 置`1`表示当前WQE等待之前所有WQE完成后才执行。 |
| `se` | 1 | 置`1`表示WQE完成后触发远端事件通知（solicited event）。 |
| `cqe` | 1 | 置`1`表示WQE完成后生成完成记录（CQE）。 |
| `inlineEn` | 1 | 置`1`表示数据内联在WQE中，而非通过SGE引用。 |

## 约束说明

- 结构为1字节packed位域布局，各字段取值不能超出位宽范围（如`odr`不超过`7`）。
- RoCE路径仅`cqe`与`fence`字段生效，其余字段忽略。
- 批量接口要求`inlineEn`为`0`，`cqe`只能为`0`或`1`。
- 自定义`config`须为编译期常量，并以模板实参形式传入；字段取值需同时满足所用接口文档中的约束说明。

## 调用示例

```cpp
// 自定义config：强顺序、fence使能、不生成完成记录，可用于批量提交的中间任务
constexpr AscendC::UrmaWqeEntry CFG_NO_CQE = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
};

// 以模板实参形式传入config
ret = hcomm.WriteNbi<false, CFG_NO_CQE>(channel, dst, src, len);
```
