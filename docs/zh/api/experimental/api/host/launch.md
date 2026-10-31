# Kernel启动

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

通过生成的wrapper以chevron语法提交CCU kernel。提交是异步的；返回成功只表示提交流程成功，不表示设备任务已经完成。

## 调用形式

```cpp
status = kernel<<<schd, instance, stream>>>(task_arg0, task_arg1);
```

## 调度描述符

```cpp
HcomCcuSchd schd = {};
schd.missionNum = 1;
schd.missionStart = 0;
schd.rankId = rank;
schd.rankSize = world;
```

| 字段 | 说明 |
| --- | --- |
| `missionNum` | mission数量；当前runtime保持单mission提交 |
| `missionStart` | mission起始编号 |
| `rankId` | 当前rank ID |
| `rankSize` | 当前通信域rank数 |
| `cacheTag` | 预留字段，当前填`0` |
| `channelSize` | 显式传入channel时使用；未显式传入时填`0` |
| `channelList` | 显式传入channel时使用；未显式传入时填空指针 |

## 调度描述符类型

```cpp
typedef struct HcomCcuSchd {
  uint32_t missionNum;
  uint32_t missionStart;
  uint32_t rankId;
  uint32_t rankSize;
  uint64_t cacheTag;
  uint64_t channelSize;
  void *channelList;
} HcomCcuSchd;
```

## 参数说明

| 项 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `schd` | 输入 | `HcomCcuSchd` | 调度描述符 |
| `instance` | 输入 | `ccu_instance_t` | 部署环境提供的instance句柄 |
| `stream` | 输入 | `void *` | user stream |
| task args | 输入 | kernel参数布局定义的类型 | 必须与编译生成的参数布局一致 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `CCU_STATUS_SUCCESS` | 任务已进入提交流程；设备执行尚未确认完成 |
| `CCU_STATUS_INVALID_ARGUMENT` | 参数非法 |
| `CCU_STATUS_SCHEMA_MISMATCH` | rank size、task args或specialization不匹配 |
| `CCU_STATUS_KERNEL_NOT_FOUND` | 找不到kernel specialization |
| `CCU_STATUS_CONTEXT_POISONED` | context已进入终止状态 |
| `CCU_STATUS_RUNTIME_ERROR` | 运行时提交失败 |

## 约束说明

- `rankSize`必须匹配kernel通过`__ccu_rank_sizes__`声明的specialization。
- 一个context同一时刻只允许一个mission在飞。
- 不要在Host侧手工构造descriptor、channel或instruction ID。
- `channelSize`/`channelList`只有在应用显式传递channel时才设置。
- 普通样例使用生成的wrapper；不要直接调用编译器生成的内部submit函数。

## 调用示例

```cpp
// 1. 构造调度描述符。
HcomCcuSchd schd{};
schd.missionNum = 1;
schd.missionStart = 0;
schd.rankId = rank;
schd.rankSize = world;

// 2. 异步提交 kernel。
ccu_status_t status = my_kernel<<<schd, instance, stream>>>(
    input.address, output.address, input.token, bytes);

// 3. 提交成功后，在 user stream 上等待完成。
if (status == CCU_STATUS_SUCCESS)
  status = ccu_synchronize(context, stream, 30000);
```
