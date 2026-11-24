# asccomm_reduce_op

## 功能说明

头文件路径：`"asccomm/asccomm_common.h"`（包含`"asccomm/asccomm.h"`后亦可直接使用）。

定义写归约操作的归约算子类型，用作[write_reduce_nbi](./write_reduce_nbi.md)的模板参数：将本端元素与远端`dst`中的对应元素按指定算子归约后写入远端`dst`。

## 定义原型

```cpp
enum class asccomm_reduce_op : uint32_t {
    MAX = 0x8U,
    MIN = 0x9U,
    SUM = 0xAU
};
```

## 成员说明

**表1** asccomm_reduce_op成员说明

| 成员 | 取值 | 描述 |
| --- | --- | --- |
| `MAX` | 0x8 | 取本端元素与远端元素中的最大值。 |
| `MIN` | 0x9 | 取本端元素与远端元素中的最小值。 |
| `SUM` | 0xA | 本端元素与远端元素求和。 |
