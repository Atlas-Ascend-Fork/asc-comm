# buf_desc

## 功能说明

头文件路径：`"asccomm/asccomm_common.h"`（包含`"asccomm/asccomm.h"`后亦可直接使用）。

描述一个本端GM（Global Memory，全局内存）内存段，用作批量多SGE接口（[write_nbi](./write_nbi.md)、[read_nbi](./read_nbi.md)、[write_with_notify_nbi](./write_with_notify_nbi.md)）的离散内存段描述：单次调用通过`buf_desc`数组描述多个本端离散数据段，对远端一段连续内存聚合读写。

## 定义原型

```cpp
struct buf_desc {
    GM_ADDR addr;
    uint32_t len;
};
```

## 成员说明

**表1** buf_desc成员说明

| 成员 | 类型 | 描述 |
| --- | --- | --- |
| `addr` | `GM_ADDR` | 本端内存段起始GM地址。 |
| `len` | `uint32_t` | 内存段长度，单位为字节。 |
