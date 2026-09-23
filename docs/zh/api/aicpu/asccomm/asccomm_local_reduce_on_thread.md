# asccomm_local_reduce_on_thread

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
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->
<!-- npu="310p" id6 -->
- Atlas 推理系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"hcomm/asccomm_primitives.h"`。

提供本地归约操作，将src指向的长度为count\*sizeof\(dataType\)的内存数据，与dst所指向的相同长度的内存数据进行reduceOp操作，并将结果输出到dst中。

## 函数原型

```cpp
int32_t asccomm_local_reduce_on_thread(ThreadHandle thread, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为通过`HcclThreadAcquire`接口（声明于`"hccl/hccl_res.h"`）获取到的threads。<br>ThreadHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| dst | 输出 | 目的地址，Device内存。 |
| src | 输入 | 源地址，Device内存。 |
| count | 输入 | 元素个数。 |
| dataType | 输入 | 数据类型，支持：int8、int16、int32、float16、float32、bfp16。<br>HcommDataType类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| reduceOp | 输入 | 归约操作类型，支持：sum、prod、max、min。<br>HcommReduceOp类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

dst、src内存需要为device内存。

<!-- npu="950" id7 -->
在Ascend 950PR/Ascend 950DT系列产品上，仅支持AICPU_TS模式下、在Device侧调用该接口。
<!-- end id7 -->

## 调用示例

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_CPU_TS;
aclrtStream stream;
aclrtCreateStream(&stream);
ThreadHandle thread;
HcclResult result = HcclThreadAcquireWithStream(comm, engine, stream, 2, &thread);

// 申请device内存
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
// 执行Device侧的Reduce操作
uint64_t count = memSize / SIZE_TABLE[HCOMM_DATA_TYPE_FP32];
asccomm_local_reduce_on_thread(thread, outputMem, inputMem, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM);
```

<!-- npu="950" id8 -->
在Ascend 950PR/Ascend 950DT系列产品上，该函数需要编译到Device侧使用：

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_AICPU_TS;

void *cclBufferAddr = nullptr;
uint64_t cclBufferSize = 0;
HcclGetHcclBuffer(comm, &cclBufferAddr, &cclBufferSize);
ThreadHandle thread;
HcclThreadAcquire(comm, engine, 1, 1, &thread);

// 其余资源申请
// 拷贝参数并Launch Kernel

// Device侧算法编排
uint64_t len = 256;
void *src = param.userIn;
void *dst = param.cclBuf;
uint64_t sizeOfFP32 = 4;
uint64_t count = len / sizeOfFP32;
asccomm_local_reduce_on_thread(thread, dst, src, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM);
```

<!-- end id8 -->
