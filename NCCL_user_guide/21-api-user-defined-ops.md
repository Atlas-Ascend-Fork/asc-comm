# 21 · API · 用户自定义归约算子

> 原文：NCCL API → User Defined Reduction Operators

NCCL 暴露了两个公开 API，用于创建和销毁供归约类集合操作使用的自定义归约算子。

## ncclRedOpCreatePreMulSum

```c
ncclResult_t ncclRedOpCreatePreMulSum(ncclRedOp_t* op, void* scalar,
                                     ncclDataType_t datatype,
                                     ncclScalarResidence_t residence,
                                     ncclComm_t comm)
```

创建一个新的归约算子，其行为是：**先在本地把输入值乘以给定 scalar，再通过求和与 peer 的值归约**。

参数说明：

| 参数 | 含义 |
| --- | --- |
| `op` | 返回值：新建算子的 handle 存到这里 |
| `scalar` | 预乘用的标量 |
| `datatype` | 输入值和 scalar 都是这个类型 |
| `residence` | 决定 `scalar` 指向的内存何时被解引用（见下） |
| `comm` | 该算子只能用于针对这个 comm 和这个 datatype 发起的集合操作 |

`residence` 的两个取值：

| 值 | 何时解引用 scalar 内存 |
| --- | --- |
| `ncclScalarHostImmediate` | 本函数返回前，由 **host 立即**解引用 |
| `ncclScalarDevice` | 归约集合操作执行期间，由 **device** 解引用 |

**使用限制**：该算子只能用于针对创建时指定的 `comm` 和 `datatype` 发起的集合操作。

## ncclRedOpDestroy

```c
ncclResult_t ncclRedOpDestroy(ncclRedOp_t op, ncclComm_t comm)
```

销毁归约算子 `op`。

- 该算子必须是用 `ncclRedOpCreatePreMulSum` 配**匹配的 communicator `comm`** 创建的。
- **销毁时机**：最后一个接受该算子的 NCCL 函数**返回之后**即可立即销毁——不需要等通信实际完成。
