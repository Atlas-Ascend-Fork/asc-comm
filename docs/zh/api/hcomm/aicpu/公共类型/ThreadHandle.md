# ThreadHandle

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

定义通信线程句柄类型，用于向Hcomm数据面接口指定执行通信任务的线程资源。

## 定义原型

```cpp
typedef uint64_t ThreadHandle;
```

## 约束说明

- 除接口明确说明`thread`参数无作用且可传入`0`的场景外，`0`不是有效的ThreadHandle。

- 必须使用[HcommThreadAlloc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommThreadAlloc.md)、[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)或相关线程资源获取接口成功返回的句柄，不允许自行构造或修改句柄值。

- 对于[HcommThreadAlloc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommThreadAlloc.md)返回的句柄，使用完成后需要通过[HcommThreadFree](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommThreadFree.md)释放；[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)及相关接口返回的句柄由库内管理，调用者不得释放。

- 将句柄传入数据面接口时，句柄关联的通信引擎必须符合该接口的约束。
