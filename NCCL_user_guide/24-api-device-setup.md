# 24 · API · Device API – Host 侧 setup

> 原文：NCCL API → Device API → Device API – Host-Side Setup

## Host 侧 setup

以下是 **host 代码**中使用的函数和类型：创建/销毁 device communicator、查询属性、需求与属性类型。`ncclDevComm` 结构体随后传给 device 代码。

### ncclDevComm

在 host 侧用 `ncclDevCommCreate()` 创建的 device communicator 结构体，主要在 device 侧使用。

**一般来说该结构体的字段视为内部实现，用户不应访问。** 例外是以下字段，它们**保证跨 NCCL 版本稳定**：

| 字段 | 含义 |
| --- | --- |
| `int rank` | communicator 内的 rank |
| `int nRanks` | communicator 的大小 |
| `int lsaRank` | 本地 LSA team 内的 rank（见 `14` 的 Teams） |
| `int lsaSize` | 本地 LSA team 的大小 |
| `uint8_t ginContextCount` | 支持的 GIN context 数量（见 `26`；NCCL 2.28.7 起） |

### ncclDevCommCreate

```c
ncclResult_t ncclDevCommCreate(ncclComm_t comm,
                               struct ncclDevCommRequirements const* reqs,
                               struct ncclDevComm* outDevComm)
```

创建与给定 host 侧 communicator 对应的新 device communicator。

- 结果返回在 `outDevComm` 中，**该 buffer 需由调用方提供**。
- 调用方还需通过 `reqs` 提供填好的需求列表，函数会分配满足这些需求所需的资源。
- **建议先调 `ncclCommQueryProperties()`**；若指定的需求不被支持，本函数会失败。
- **这是集合调用**，communicator 内每个 rank 都需参与。
- 若在 group 内调用，`outDevComm` 可能要到 `ncclGroupEnd()` 完成后才被填入。

这是 host 侧函数。

### ncclDevCommDestroy

```c
ncclResult_t ncclDevCommDestroy(ncclComm_t comm, struct ncclDevComm const* devComm)
```

销毁此前用 `ncclDevCommCreate()` 创建的 device communicator，释放已分配的资源。

**调用方必须确保调用本函数时，没有任何使用该 device communicator 的 device kernel 可能正在运行。**

这是 host 侧函数。

### ncclDevCommRequirements

创建 device communicator 时指定需求列表的 host 侧结构体。**NCCL 2.29 起，该结构体必须用 `NCCL_DEV_COMM_REQUIREMENTS_INITIALIZER` 初始化。**

| 字段 | 起始版本 | 说明 |
| --- | --- | --- |
| `bool lsaMultimem` | — | 是否要求所有 LSA rank 都支持 multimem |
| `int lsaBarrierCount` | — | 要分配的内存 barrier 数量（见 `25` 的 `ncclLsaBarrierSession`）。写融合 kernel 必须有这些 barrier，`27` 中的构件也可能需要 |
| `int railGinBarrierCount` | 2.28.7 | 要分配的 railed 网络 barrier 数量（见 `26` 的 `ncclGinBarrierSession`） |
| `int barrierCount` | 2.28.7 | 要分配的网络/rail 混合 barrier 数量（见 `ncclBarrierSession`） |
| `int ginSignalCount` | 2.28.7 | 要分配的网络 signal 数量（见 `ncclGinSignal_t`） |
| `int ginCounterCount` | 2.28.7 | 要分配的网络 counter 数量（见 `ncclGinCounter_t`） |
| `bool ginForceEnable` | 2.28.7 起，**2.29.7 起废弃** | 通过自动把 `ginConnectionType` 设为 `NCCL_GIN_CONNECTION_FULL` 来强制启用 GIN。设为 `true` 时会**覆盖** `ginConnectionType`。新代码应直接用 `ginConnectionType` |
| `ncclGinConnectionType_t ginConnectionType` | 2.29.7 | 指定要建立的 GIN 连接类型，详见下文 |
| `ncclDevResourceRequirements_t* resourceRequirementsList` | — | 资源需求列表。**目前最好设为 NULL** |
| `ncclTeamRequirements_t* teamRequirementsList` | — | 特定 team 的需求列表。**目前最好设为 NULL** |
| `int ginTrafficClass` | 2.30.3 | GIN traffic class，详见 `04` 的 QoS 一节 |
| `int worldGinBarrierCount` | 2.30.3 | 要分配的 world 网络 barrier 数量 |
| `bool ginStrongSignalsRequired` | 2.30.5 | 是否需要 GIN strong signal。若使用本 communicator 的 kernel 不用 strong signal 操作（如 `ncclGin_StrongSignalInc`、`ncclGin_StrongVASignalAdd`）则设为 false。**默认 true** |
| `bool ginVaSignalsRequired` | 2.30.5 | 是否需要 GIN VA signal。若 kernel 不用 GIN VA signal（如 `ncclGin_WeakVASignalInc`、`ncclGin_StrongVASignalAdd`）则设为 false。**默认 true** |

`ginConnectionType` 的行为细节：

- 设为 `NCCL_GIN_CONNECTION_FULL`：初始化 GIN，communicator 内**所有 rank 连接到所有其他 rank**。
- 设为 `NCCL_GIN_CONNECTION_RAIL`：初始化 GIN，**每个 rank 连接到同一 rail team 内的其他 rank**。
- **陷阱**：若该字段为 `NCCL_GIN_CONNECTION_NONE`，却通过 `ginSignalCount`、`ginCounterCount`、`barrierCount` 或 `railGinBarrierCount` 请求了 GIN 资源，则 device communicator 创建会以 **`ncclInvalidArgument`** 失败。

### ncclCommQueryProperties

```c
ncclResult_t ncclCommQueryProperties(ncclComm_t comm, ncclCommProperties_t* props)
```

通过填充 `props` 暴露 communicator 属性。**调用前 `props` 必须用 `NCCL_COMM_PROPERTIES_INITIALIZER` 初始化。** NCCL 2.29 引入。

这是 host 侧函数。

### ncclCommProperties_t

描述 communicator 属性的结构体，NCCL 2.29 引入。

| 字段 | 起始版本 | 含义 |
| --- | --- | --- |
| `int rank` | 2.29 | communicator 内的 rank |
| `int nRanks` | 2.29 | communicator 的大小 |
| `int cudaDev` | 2.29 | CUDA 设备索引 |
| `int nvmlDev` | 2.29 | NVML 设备索引 |
| `bool deviceApiSupport` | 2.29 | 是否支持 device API。**为 false 时无法创建 `ncclDevComm`** |
| `bool multimemSupport` | 2.29 | 同一 LSA team 内的 rank 是否能用 multimem 通信。**为 false 时无法创建带 multimem 资源的 `ncclDevComm`** |
| `ncclGinType_t ginType` | 2.29 | communicator 支持的 GIN 类型。**等于 `NCCL_GIN_TYPE_NONE` 时无法以 `NCCL_GIN_CONNECTION_FULL` 创建 `ncclDevComm`** |
| `int nLsaTeams` | 2.29.7 | 整个 communicator 中 LSA team 的数量 |
| `ncclGinType_t railedGinType` | 2.29.7 | communicator 支持的 railed GIN 类型。**等于 `NCCL_GIN_TYPE_NONE` 时无法以 `NCCL_GIN_CONNECTION_RAIL` 创建 `ncclDevComm`** |

### ncclGinType_t

GIN 类型。**不同 GIN 类型之间的通信不受支持。**

| 值 | 含义 |
| --- | --- |
| `NCCL_GIN_TYPE_NONE` | 不支持 GIN |
| `NCCL_GIN_TYPE_PROXY` | Host Proxy GIN 类型 |
| `NCCL_GIN_TYPE_GDAKI` | GPUDirect Async Kernel-Initiated（GDAKI）GIN 类型 |
| `NCCL_GIN_TYPE_GPI` | GPU-Push Interface（GPI）GIN 类型。需要 SpectrumX，详见 SpectrumX 文档。**NCCL 2.30.6 作为实验特性加入** |

### ncclGinConnectionType_t

指定 device communicator 的 GIN 连接类型，控制是否分配 GIN 资源以及用哪种连接类型。用于 `ncclDevCommRequirements`。**NCCL 2.29.7 起可用。**

| 值 | 含义 |
| --- | --- |
| `NCCL_GIN_CONNECTION_NONE` | 无 GIN 连通性 |
| `NCCL_GIN_CONNECTION_FULL` | 完整 GIN 连通性，每个 rank 连到所有其他 rank |
| `NCCL_GIN_CONNECTION_RAIL` | Railed GIN 连通性，每个 rank 连到同一 rail team 内的其他 rank |

## Host 可访问的 device 指针函数

以下函数**只能从 host 代码调用**，为 host 代码提供获取 LSA 内存区域指针的能力。

通用约定：

- 所有函数返回 `ncclResult_t`。成功返回 `ncclSuccess`；失败返回相应错误码（如参数非法返回 `ncclInvalidArgument`，内部失败返回 `ncclInternalError`），除非另有说明。
- **返回的指针在 window 生命周期内有效**。window 或 communicator 销毁后不得再使用这些指针。
- 获得的指针是 **device 指针**。

### ncclGetLsaMultimemDevicePointer

```c
ncclResult_t ncclGetLsaMultimemDevicePointer(ncclWindow_t window, size_t offset, void** outPtr)
```

返回与给定 window 关联的 LSA team 的 **multimem 基址指针**。

参数：`window` 不得为 NULL；`offset` 是 window 内的字节偏移；`outPtr` 是输出参数，不得为 NULL。

要求：需要 LSA multimem 支持（系统具备 multicast 能力）。window 必须注册在支持对称内存的 communicator 上，且硬件必须支持 NVLink SHARP multicast 功能。

**注意**：若系统不支持 multimem，函数返回 `ncclSuccess` 且 `*outPtr` 被设为 `nullptr`。这让应用能优雅地检测并处理 multimem 缺失，而不破坏 communicator。**用户应检查返回指针是否为 `nullptr` 来判断可用性。**

```c
void* multimemPtr;
ncclResult_t result = ncclGetLsaMultimemDevicePointer(window, 0, &multimemPtr);
if (result == ncclSuccess) {
  if (multimemPtr != nullptr) {
    // Use multimemPtr for multimem operations
  } else {
    // Multimem not supported, use fallback approach
  }
}
```

### ncclGetMultimemDevicePointer

```c
ncclResult_t ncclGetMultimemDevicePointer(ncclWindow_t window, size_t offset,
                                         ncclMultimemHandle multimem, void** outPtr)
```

用**提供的 multimem handle** 而非 window 内部的 multimem 来返回 multimem 基址指针，使得可以用外部或自定义 multimem handle 做指针计算。

参数：`window` 不得为 NULL；`offset` 为字节偏移；`multimem` 是含 multimem 基址指针的 handle，**`multimem.mcBasePtr` 不得为 NULL**；`outPtr` 不得为 NULL。

要求：需要 LSA multimem 支持。

**注意**：若系统不支持 multimem，返回 `ncclSuccess` 且 `*outPtr` 设为 `nullptr`。函数在继续之前会校验 `multimem.mcBasePtr` 非 `nullptr`。

```c
// Get multimem handle from device communicator setup
ncclMultimemHandle customHandle;
// ... (obtain handle)

void* multimemPtr;
ncclResult_t result = ncclGetMultimemDevicePointer(window, 0, customHandle, &multimemPtr);
if (result == ncclSuccess) {
  if (multimemPtr != nullptr) {
    // Use multimemPtr for multimem operations with custom handle
  } else {
    // Multimem not supported, use fallback approach
  }
}
```

### ncclGetLsaDevicePointer

```c
ncclResult_t ncclGetLsaDevicePointer(ncclWindow_t window, size_t offset,
                                    int lsaRank, void** outPtr)
```

返回 window 内**特定 LSA peer** 内存 buffer 的 load/store 可访问指针，直接用 LSA rank 寻址。

参数约束：

- `window` 不得为 NULL
- `offset` 必须 **≥ 0 且 < window 大小**
- `lsaRank` 是目标 peer 的 LSA rank，必须 **≥ 0 且 < LSA team 大小**
- `outPtr` 不得为 NULL

要求：window 必须注册在支持 LSA 的 communicator 上；LSA rank 必须在 LSA team 的有效范围内；目标 peer 必须是 load/store 可访问的（需要 P2P 连通性）。

```c
void* lsaPtr;
ncclResult_t result = ncclGetLsaDevicePointer(window, 0, 1, &lsaPtr);
if (result == ncclSuccess) {
  // Use lsaPtr to access LSA peer 1's memory
}
```

### ncclGetPeerDevicePointer

```c
ncclResult_t ncclGetPeerDevicePointer(ncclWindow_t window, size_t offset,
                                     int peer, void** outPtr)
```

返回 window 内**特定 world rank peer** 内存 buffer 的 load/store 可访问指针。函数内部把 world rank 转换为 LSA rank。

参数：`window` 不得为 NULL；`offset` 为字节偏移；`peer` 是目标 peer 的 world rank，必须 **≥ 0 且 < communicator 大小**；`outPtr` 不得为 NULL。

**关键行为**：若该 peer 无法通过 LSA 访问（不在 LSA team 内），`outPtr` 被设为 **NULL** 且返回 `ncclSuccess`。这与 device 侧 `ncclGetPeerPointer` 的行为一致。

要求同上：window 注册在支持 LSA 的 communicator 上，peer rank 在 communicator 有效范围内，目标 peer load/store 可访问（需 P2P 连通性）。

```c
void* peerPtr;
ncclResult_t result = ncclGetPeerDevicePointer(window, 0, 2, &peerPtr);
if (result == ncclSuccess) {
  if (peerPtr != NULL) {
    // Use peerPtr to access world rank 2's memory
  } else {
    // Peer 2 is not reachable via LSA
  }
}
```

## 四个 host 侧指针函数速查

| 函数 | 寻址方式 | 不可用时的返回 |
| --- | --- | --- |
| `ncclGetLsaMultimemDevicePointer` | window 内部 multimem | `ncclSuccess` + `nullptr` |
| `ncclGetMultimemDevicePointer` | 自定义 multimem handle | `ncclSuccess` + `nullptr` |
| `ncclGetLsaDevicePointer` | LSA rank | 参数非法则报错 |
| `ncclGetPeerDevicePointer` | world rank | `ncclSuccess` + `NULL`（peer 不在 LSA team 内） |
