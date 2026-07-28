# HcommLocalReduceOnThread

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

提供本地归约操作，将源地址`src`中长度为$count \times \operatorname{sizeof}(dataType)$的数据与目的地址`dst`中相同长度的数据进行reduceOp操作，并将结果写入目的地址`dst`。

## 函数原型

```cpp
int32_t HcommLocalReduceOnThread(ThreadHandle thread, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>[ThreadHandle](../公共类型/ThreadHandle.md)类型的定义请参见对应公共类型文档。 |
| dst | 输出 | 目的地址，操作地址范围必须位于有效Device内存范围内。 |
| src | 输入 | 源地址，操作地址范围必须位于有效Device内存范围内。 |
| count | 输入 | 元素个数。有效范围由源内存和目的内存的可用长度决定，$count \times \operatorname{sizeof}(dataType)$不得超过两者中较小的可用长度。 |
| dataType | 输入 | 数据类型。支持的数据类型请参见[数据类型](#section_hcommlocalreduceonthread_datatype)。<br>[HcommDataType](../公共类型/HcommDataType.md)类型的定义请参见对应公共类型文档。 |
| reduceOp | 输入 | 归约操作类型，取值范围：sum、max、min。<br>[HcommReduceOp](../公共类型/HcommReduceOp.md)类型的定义请参见对应公共类型文档。 |

### 数据类型<a id="section_hcommlocalreduceonthread_datatype"></a>

<!-- npu="950" id8 -->
针对Ascend 950PR/Ascend 950DT，`dataType`参数支持的[`HcommDataType`](../公共类型/HcommDataType.md)枚举值见表2。
<!-- end id8 -->
<!-- npu="A3" id9 -->
针对Atlas A3 训练系列产品/Atlas A3 推理系列产品，`dataType`参数支持的[`HcommDataType`](../公共类型/HcommDataType.md)枚举值见表2。
<!-- end id9 -->
<!-- npu="910b" id10 -->
针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，`dataType`参数支持的[`HcommDataType`](../公共类型/HcommDataType.md)枚举值见表2。
<!-- end id10 -->

表2 支持的数据类型

|HcommDataType枚举值 | 数据类型 |
| --- | --- |
| `HCOMM_DATA_TYPE_INT8` | int8 |
| `HCOMM_DATA_TYPE_INT16` | int16 |
| `HCOMM_DATA_TYPE_INT32` | int32 |
| `HCOMM_DATA_TYPE_FP16` | float16 |
| `HCOMM_DATA_TYPE_FP32` | float32 |
| `HCOMM_DATA_TYPE_BFP16` | bfp16 |

## 返回值

表3 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- `src`和`dst`必须为已申请的Device内存地址。

<!-- npu="950" id6 -->
- 在Ascend 950PR/Ascend 950DT上，仅支持AICPU_TS模式下、在Device侧调用该接口。
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
// 执行Device侧的Reduce操作。
uint64_t count = memSize / SIZE_TABLE[HCOMM_DATA_TYPE_FP32];
HcommLocalReduceOnThread(thread, outputMem, inputMem, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM);
```

<!-- npu="950" id7 -->
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
uint64_t sizeOfFP32 = 4;
uint64_t count = len / sizeOfFP32;
HcommLocalReduceOnThread(thread, dst, src, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM);
```

<!-- end id7 -->

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
