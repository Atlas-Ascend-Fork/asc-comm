# AinBarrierSession

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

`AscendC::AinBarrierSession`是AI Core侧的通信组team内同步会话。构造时绑定`Ain`实例、通信组team和barrier资源索引；调用`Sync`时实现通信组team内的屏障同步。

## 函数原型

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class AinBarrierSession;
```

构造函数：

```cpp
__aicore__ inline AinBarrierSession(
    AscendC::Ain<CommEngineMask>* ain,
    AscendC::HcommTeamHandle team,
    uint32_t index);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `CommEngineMask` | 通信引擎选择掩码，默认值为`AIN_MASK_DEFAULT`，对应通信引擎为AIV。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `ain` | 输入 | `Ain`实例指针。 |
| `team` | 输入 | 通信组team句柄。 |
| `index` | 输入 | barrier资源索引，不同索引用于隔离不同barrier会话。 |

## 约束说明

- 调用前Host侧需完成team创建，并准备barrier使用的同步内存。
- `ain`不能为nullptr；`team`需要是有效的device侧句柄。
- `index`需要对应可用的barrier资源，不同并发barrier应使用不同`index`隔离。
- `AinBarrierSession`构造时的`index`必须与入参`ain`的`contextIndex`保持一致，确保barrier会话使用对应的通信上下文资源。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议路径。

## 调用示例

AinBarrierSession接口的调用示例如下。

```cpp
// 初始化UBuf工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(__gm__ void* team)
{
    // 准备UBuf工作区
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};

    // Ain和AinBarrierSession的资源索引
    uint32_t index = 0U;
    // 创建Ain对象
    AscendC::Ain ain(index);
    // 创建AinBarrierSession对象
    AscendC::AinBarrierSession barrierSession(&ain, team, index);
    // 执行team内barrier同步
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
}
```
