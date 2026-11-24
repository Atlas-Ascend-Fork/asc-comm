# wqe_entry

## 功能说明

头文件路径：`"asccomm/asccomm_common.h"`（包含`"asccomm/asccomm.h"`后亦可直接使用）。

WQE（Work Queue Element）控制配置结构，作为CommRW数据面接口的`config`模板参数，控制任务的下发顺序、fence、完成记录生成与数据内联行为。`COMM_PROTOCOL_ROCE`下仅`cqe`与`fence`字段生效，其余字段忽略。

## 定义原型

```cpp
#pragma pack(push, 1)
struct wqe_entry {
    uint32_t odr : 3;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inline_en : 1;
} __attribute__((packed));
#pragma pack(pop)
```

## 成员说明

**表1** wqe_entry成员说明

| 成员 | 位宽 | 描述 |
| --- | --- | --- |
| `odr` | 3 | 顺序属性：bit[1:0]控制执行序：`00`为无序（No Order），`01`为弱序（Relax Order），`10`为强序（Strong Order）；bit[2]控制完成序：`0`为无序，`1`为有序。 |
| `fence` | 1 | 置`1`表示当前WQE等待之前所有WQE完成后才执行。 |
| `se` | 1 | 置`1`表示WQE完成后触发远端事件通知（solicited event）。 |
| `cqe` | 1 | 置`1`表示WQE完成后生成完成记录（CQE）。 |
| `inline_en` | 1 | 置`1`表示数据内联在WQE中，而非通过SGE引用。 |
