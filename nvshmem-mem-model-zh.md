# NVSHMEM 内存模型（中文整理）

> 来源：NVSHMEM 2.4.1 文档 · Memory Model
> https://docs.nvidia.com/nvshmem/archives/nvshmem-241/api/docs/gen/mem-model.html

## 1. NVSHMEM 内存模型

NVSHMEM 程序中的数据对象分两类：

- **私有数据对象**：存放在各 PE 的本地内存中，只能被该 PE 自己访问，其他 PE 无法通过 NVSHMEM 例程访问。私有数据对象遵循 C 的内存模型。
- **远程可访问对象**：可被远端 PE 通过 NVSHMEM 例程访问，称为**对称数据对象（Symmetric Data Object）**。每个对称数据对象在所有可通过 NVSHMEM API 访问它的 PE 上，都有一个**同名、同类型、同大小**的对应对象。<sup>[1]</sup>

关键点：**由 NVSHMEM 内存管理例程分配的 GPU 内存都是对称的**（对称内存分配详见文档 *Memory Management* 一节）。

动态分配例程（如 `nvshmem_malloc`）在一块称为**对称堆（Symmetric Heap）**的特殊内存区域上集合式地分配对称数据对象。对称堆在程序执行期间由 NVSHMEM 库决定其内存位置创建；**不同 PE 上的对称堆可能位于不同的内存区域**。

## 2. 指向对称对象的指针

### 2.1 对称地址（symmetric address）

- NVSHMEM 操作通过指向目标远程可访问对象的**本地指针**来引用对称数据对象，该指针中的地址称为**对称地址**。
- 每个对称地址同时也是一个可直接内存访问的有效本地地址；但**反之不成立**——并非所有本地地址都是对称地址。
- 对传给 NVSHMEM 例程的对称地址做指针运算、数组下标、访问 struct/union 成员等操作是允许的，**前提是结果本地指针仍落在同一个对称分配或对象内部**。
- 对称地址**只在生成它的那个 PE 上有效**。把某个 PE 生成的对称地址拿到别的 PE 上做直接内存访问、或作为 NVSHMEM 例程参数，都是未定义行为。

### 2.2 对齐要求

| 接口类型 | 对齐要求 |
| --- | --- |
| 类型化接口（typed） | 必须按其类型以及底层架构的要求做自然对齐 |
| 定长接口（如 `nvshmem_put32`） | 除自然对齐外，还必须按给定大小对齐；且传入的对称对象**存储大小必须等于该操作的位宽**<sup>[2]</sup> |
| "mem" 接口（如 `nvshmem_putmem`） | 无对齐要求 |

由于 C/C++ 结构体可能包含实现定义的填充字节，**定长接口不应该用于 C/C++ 结构体**。

### 2.3 nvshmem_ptr

`nvshmem_ptr` 允许程序员查询指定 PE 上某个远程可访问数据对象的本地地址。返回的指针可用于直接内存访问；但把这个地址作为「要求对称地址」的 NVSHMEM 例程的参数传入，是未定义行为。

## 3. 操作的定序（Ordering）

OpenSHMEM 规定：读数据的阻塞操作（如 get、atomic fetch-and-add）应按操作执行的顺序返回数据。例如对 PE 0 上的对称变量 `x` 连续做两次原子 fetch-and-add：

```c
a = nvshmem_int_fadd(x, 1, 0);
b = nvshmem_int_fadd(x, 1, 0);
```

- **OpenSHMEM 规范**保证 `b > a`。
- 但这种强序在弱序架构上代价很高——要求任何此类操作返回前都插入内存屏障。**NVSHMEM 放宽了这一要求**以在 NVIDIA GPU 上获得更高效的实现，因此 **NVSHMEM 不保证 `b > a`**。

需要该顺序时：

- 阻塞操作：用 `nvshmem_fence` 强制定序（例如插在上面两条语句之间）。
- **非阻塞操作不受 `nvshmem_fence` 定序**，必须用 `nvshmem_quiet` 来完成它们。
- 取值类操作（fetching）的完成语义与 OpenSHMEM 规范一致：get 或 AMO 的结果，对程序顺序中出现在其之后的任何依赖操作都是可用的。

## 4. 原子性保证

NVSHMEM 提供若干对对称数据对象执行原子操作的例程（详见文档 *Atomic Memory Operations* 一节）。

**保证互斥的情形**：

- 这些原子例程对**同一位置、使用同一数据类型**（类型见文档中 *Standard AMO Types and Names* 与 *Extended AMO Types and Names* 两张表）的并发访问是互斥的。
- 当目标 PE 对同一位置、以同一数据类型执行 wait 或 test 操作，与一个或多个原子操作并发时，同样保证互斥。

**不保证互斥、属于未定义行为的情形**：

1. 用**不同数据类型**的 NVSHMEM 原子操作并发访问同一位置。
2. 用原子与非原子的 NVSHMEM 操作并发访问同一位置。
3. 用 NVSHMEM 原子操作与非 NVSHMEM 操作（如 load / store）并发访问同一位置。

## 5. NVSHMEM 与 OpenSHMEM 的差异

### 5.1 阻塞取值操作的定序

OpenSHMEM 期望读数据的阻塞操作按执行顺序返回数据。看这个用原子 set 更新 PE 0 上对称变量 `x` 和 `y` 的例子：

```c
// 约定：v_N 表示 PE N 上的对称变量 v
// 输入：x_0 = 0, y_0 = 0, i = 0, j = 0

if (nvshmem_my_pe() == 0) {
    a = nvshmem_int_atomic_set(x, 1, 0);
    nvshmem_quiet();
    b = nvshmem_int_atomic_set(y, 1, 0);
}

i = nvshmem_int_atomic_fetch(y, 0);
j = nvshmem_int_atomic_fetch(x, 0);

// NVSHMEM 允许的输出：i = 1, j = 0
```

- **OpenSHMEM 规范**保证：若 `i == 1`，则 `j == 1`。
- 这种强序在弱序架构上开销显著（任何取值操作返回前都需内存屏障）。NVSHMEM 放宽了它，以获得与 **NVIDIA GPU 内存模型**一致的更高效实现。因此 **NVSHMEM 不保证「`i == 1` ⇒ `j == 1`」**。

需要该顺序时，用 `nvshmem_fence` 为阻塞操作强制定序（例如插在上面两条 `nvshmem_int_atomic_fetch` 之间）。非阻塞操作不受 `nvshmem_fence` 定序，须用 `nvshmem_quiet` 完成。取值操作的完成语义与规范一致（get / AMO 的结果对程序顺序中其后的依赖操作可用）。

### 5.2 可见性保证

这一点与互连拓扑有关：

| 系统互连 | `nvshmem_barrier`、`nvshmem_barrier_all`、`nvshmem_quiet`、`nvshmem_wait_until_*`、`nvshmem_test_*` 的可见性保证 |
| --- | --- |
| 同时有 NVLink 和 InfiniBand | **仅**保证本地 PE 的对称对象上的更新可见 |
| 只有 NVLink | 保证对称对象上的更新**全局可见** |

## 注释

- **[1]** 出于效率考虑，所有 PE 上的对称数据对象可能使用（相对某个任意内存地址的）相同偏移。关于对称堆布局与实现效率的进一步讨论见文档 *NVSHMEM_MALLOC, NVSHMEM_FREE, NVSHMEM_ALIGN* 一节。
- **[2]** 一个字节的位宽在 C 中是实现定义的，可用 `limits.h` 中的 `CHAR_BIT` 常量可移植地计算 C 对象的位宽。

## 要点速查

1. NVSHMEM 分配的 GPU 内存全是对称的；对称对象在各 PE 上同名同型同大小，但对称堆的实际内存区域各 PE 可能不同。
2. 对称地址只在生成它的 PE 上有效；指针运算不得越出所在的对称分配。
3. 定长接口（`put32` 等）有额外对齐与存储大小要求，不要用于结构体；`putmem` 类无对齐要求。
4. `nvshmem_ptr` 返回的地址只能直接访存，不能当对称地址传给 NVSHMEM API。
5. NVSHMEM **不**继承 OpenSHMEM 对阻塞取值操作的强序保证 → 需要顺序就显式 `nvshmem_fence`。
6. 非阻塞操作只能靠 `nvshmem_quiet` 完成，`fence` 对它无效。
7. AMO 互斥仅在「同位置 + 同数据类型 + 全部走 NVSHMEM 原子操作」时成立，混用即 UB。
8. NVLink + IB 混合系统上，同步操作只保证本地 PE 对称对象的更新可见；纯 NVLink 才是全局可见。
