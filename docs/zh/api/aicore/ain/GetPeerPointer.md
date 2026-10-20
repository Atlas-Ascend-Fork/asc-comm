# GetPeerPointer

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

获取对端rank在对称window中指定偏移处的内存地址。

`GetPeerPointer`根据对端rank编号`peer`和字节偏移`offset`获取远端对称内存地址，用于直接访问对端window内存。

## 函数原型

```cpp
AIN_DEVICE HcommMemHandle GetPeerPointer(uint32_t peer, HcclCommSymWindow window, size_t offset);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `peer` | 输入 | 对端rank编号，即team内成员的rank id。 |
| `window` | 输入 | 对称window句柄，类型为`HcclCommSymWindow`，由Host侧注册后下发。 |
| `offset` | 输入 | window内的字节偏移量，类型为`size_t`。 |

## 返回值说明

返回`HcommMemHandle`类型的内存地址，指向对端rank在对称window中`offset`偏移处的内存。

## 约束说明

- 调用前Host侧需完成对称window注册，传入的`window`应为有效的device侧句柄。
- `peer`需要是team内的有效rank编号，`offset`不能超过window大小；否则返回的地址无效，通过该地址访问内存属于未定义行为。
- 对称window注销后，返回的内存地址不再有效，不应再通过该地址访问内存。

## 调用示例

GetPeerPointer接口的调用示例如下。

```cpp
extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
    uint32_t rankNum, uint32_t rankId)
{
    // 对端rankId
    uint32_t peer = (rankId + 1) % rankNum;
    // 获取对端在对称window中偏移0处的内存地址
    AscendC::HcommMemHandle peerPtr = AscendC::GetPeerPointer(peer, srcWin, 0U);
    // peerPtr为对端window内存的GM地址，可直接访问
    *reinterpret_cast<__gm__ uint64_t*>(peerPtr) = rankId;
}
```
