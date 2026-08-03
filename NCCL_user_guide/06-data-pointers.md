# 06 · Data Pointers

> 原文：Using NCCL → Data Pointers

## 可接受的指针类型

一般来说，NCCL 接受**任何能从与 communicator 对象关联的那个 CUDA 设备访问到的 CUDA 指针**，包括：

- 该 CUDA 设备本地的 device memory
- 用 CUDA SDK API `cudaHostRegister` 或 `cudaGetDevicePointer` 注册过的 host memory
- managed memory 和 unified memory

## 唯一的例外

**位于另一个设备上、但当前设备通过 peer access 能访问到的 device memory** 不被接受。这种情况 NCCL 会返回错误，目的是避免编程错误。

该检查自 2.2.12 起只在 `NCCL_CHECK_POINTERS=1` 时进行。

实践含义：不要图省事把 peer 设备的显存指针直接喂给 NCCL——即使 peer access 已开、指针在当前设备上"能访问"。数据应该在自己设备的显存里，跨设备搬运交给 NCCL 本身去做。
