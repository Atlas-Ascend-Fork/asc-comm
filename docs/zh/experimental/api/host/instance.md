# Instance与资源

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

描述CCU运行时使用的实例对象（`ccu_instance_t`）和内存描述（`ccu_buffer_view_t`）。
`ccu_buffer_view_t`用于在Host侧向kernel任务传递通信内存的地址（address）、大小（size）和令牌（token）。

## Instance

```cpp
typedef uint64_t ccu_instance_t;
```

`ccu_instance_t`是HCOMM提供的实例句柄，表示通信域绑定的CCU实例。instance由HCOMM接口
查询得到，具体接口以当前开发环境的HCOMM文档为准。示例如下：

```cpp
// instance为通过HCOMM接口查询得到的CCU实例句柄。
ccu_instance_t instance = /* HCOMM接口查询到的CCU实例句柄 */;
if (instance == 0) {
  // 获取失败，按应用错误处理流程释放已创建的资源。
}
```

## 内存描述

```cpp
typedef struct ccu_buffer_view_t {
  uint64_t address;
  uint64_t size;
  uint64_t token;
} ccu_buffer_view_t;
```

`address`、`size`和`token`必须来自同一段通信内存。

## 约束说明

- 通信域、channel、通信内存和instance由HCOMM接口创建。
- instance在launch和synchronize完成前不能销毁或替换。
