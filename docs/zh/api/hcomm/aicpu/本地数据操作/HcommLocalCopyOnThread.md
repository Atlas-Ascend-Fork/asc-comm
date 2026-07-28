# HcommLocalCopyOnThread

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：支持
<!-- end id3 -->
<!-- npu="310p" id4 -->
- Atlas 推理系列产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件：[`#include "hcomm_primitives.h"`](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_primitives.h)。

提供本地内存拷贝功能，将源地址`src`中长度为`len`的数据拷贝到目的地址`dst`中。

## 函数原型

```cpp
int32_t HcommLocalCopyOnThread(ThreadHandle thread, void *dst, const void *src, uint64_t len)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>ThreadHandle类型的定义请参见[ThreadHandle](../公共类型/ThreadHandle.md)。 |
| dst | 输出 | 目的内存地址，操作地址范围必须位于有效Device内存范围内。 |
| src | 输入 | 源内存地址，操作地址范围必须位于有效Device内存范围内。 |
| len | 输入 | 待拷贝的数据长度。单位：字节。有效范围由源内存和目的内存的可用长度决定，不得超过两者中较小的可用长度。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- `src`和`dst`必须为已申请的Device内存地址。

<!-- npu="950" id6 -->
- 针对Ascend 950PR/Ascend 950DT，仅支持AICPU_TS模式下在Device侧调用该接口。
<!-- end id6 -->

## 调用示例

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_CPU_TS;
aclrtStream stream;
aclrtCreateStream(&stream);
ThreadHandle thread;
HcclResult result = HcclThreadAcquireWithStream(comm, engine, stream, 2, &thread);

// 申请device内存。
uint64_t memSize = 256;
s32 policy = static_cast<int>(ACL_MEM_TYPE_HIGH_BAND_WIDTH) | static_cast<int>(ACL_MEM_MALLOC_HUGE_FIRST);
aclrtMallocAttrValue moduleIdValue;
moduleIdValue.moduleId = HCCL;
aclrtMallocAttribute attrs{.attr = ACL_RT_MEM_ATTR_MODULE_ID, .value = moduleIdValue};
aclrtMallocConfig cfg{.attrs = &attrs, .numAttrs = 1};

void* inputMem;
void* outputMem;
aclrtMallocWithCfg(&inputMem, memSize, static_cast<aclrtMemMallocPolicy>(policy), &cfg);
aclrtMallocWithCfg(&outputMem, memSize, static_cast<aclrtMemMallocPolicy>(policy), &cfg);
// 执行D2D拷贝。
HcommLocalCopyOnThread(thread, outputMem, inputMem, memSize);
```

在Ascend 950PR/Ascend 950DT上，该函数需要编译到Device侧使用：

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_AICPU_TS;

void *cclBufferAddr = nullptr;
uint64_t cclBufferSize = 0;
HcclGetHcclBuffer(comm, &cclBufferAddr, &cclBufferSize);
ThreadHandle thread;
HcclThreadAcquire(comm, engine, 1, 1, &thread);

// 其余资源申请。
// 拷贝参数并Launch Kernel。

// Device侧算法编排。
uint64_t len = 256;
void *src = param.userIn;
void *dst = param.cclBuf;
// 执行D2D拷贝。
HcommLocalCopyOnThread(thread, dst, src, len);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
