# NVSHMEM 执行模型（中文整理）

> 来源：NVSHMEM 2.4.1 文档 · Execution Model
> https://docs.nvidia.com/nvshmem/archives/nvshmem-241/api/docs/gen/exec-model.html

## 1. 基本执行模型

一个 NVSHMEM 程序由一组称为 **PE** 的 NVSHMEM 进程组成。

- NVSHMEM 本身并不强制要求，但典型用法是按 **SPMD**（单程序多数据）模型执行 PE。SPMD 要求每个 PE 使用同一个可执行文件，但各 PE 可以走不同的控制流分支。
- PE 用 OS 进程实现；在启用线程支持时，PE 可以创建额外的线程。

PE 之间是**松耦合**执行的，依靠 NVSHMEM 操作来通信和同步。

生命周期：

- 程序的 NVSHMEM 阶段以调用 `nvshmem_init` 或 `nvshmem_init_thread` 开始，必须在使用任何其他 NVSHMEM 库例程之前完成。
- 当所有 PE 都调用 `nvshmem_finalize`，或任一 PE 调用 `nvshmem_global_exit` 时，程序结束对 NVSHMEM 库的使用。
- 在 `nvshmem_finalize` 调用期间，NVSHMEM 库必须完成所有挂起的通信，并通过一次**隐式的跨 PE 集合同步**释放与库相关的所有资源。
- **未定义行为**：初始化之前或 `nvshmem_finalize` 之后调用任何 NVSHMEM 例程；finalize 之后再次调用初始化。

PE 标识：

- 各 PE 由唯一整数标识，从 0 起单调递增分配到「PE 总数 − 1」。
- PE 标识符用于 NVSHMEM 调用（例如指定对称数据对象上的 put / get 例程、集合同步调用），也可用 C 语言结构基于它来决定 PE 的控制流。
- 标识符在程序的整个 NVSHMEM 阶段内**固定不变**。

## 2. NVSHMEM 操作的推进（Progress）

NVSHMEM 模型假定计算与通信天然重叠。NVSHMEM 程序被期望在**有**和**没有** NVSHMEM 调用的情况下都能推进通信。

考虑一个正忙于计算、期间不做任何 NVSHMEM 调用的 PE：

- 其他 PE 应当能够与这个计算受限的 PE 进行通信（put、get、atomic 等）并**完成**这些通信操作，而无需该 PE 发出任何显式的 NVSHMEM 调用。
- 涉及该 PE 的单边（one-sided）NVSHMEM 通信调用应当能够推进，与该 PE 下一次何时进入 NVSHMEM 调用无关。

## 3. 调用 NVSHMEM 操作的规则

### 3.1 参数不得内存重叠

指向**非 const** 数据的指针参数，不得与同一次 NVSHMEM 操作的其他参数在内存上重叠，否则行为未定义。例外情况是 in-place 归约（见文档 `NVSHMEM_REDUCTIONS` 一节）。

「内存重叠」的判定：两个参数的任何数据元素落在相同的物理内存位置上，即为重叠。

举例：设 `a` 是对 PE *i* 上的对称对象 *A* 调用 `nvshmem_ptr` 返回的地址。若把本地地址 `a` 和对象 *A* 的对称地址同时传给一个以 PE *i* 为目标的 NVSHMEM 操作，结果是未定义行为。

### 3.2 in-use 缓冲区的读写限制

传给 NVSHMEM 例程的缓冲区，在对应的 NVSHMEM 操作在调用方 PE 上完成之前，都处于 **in-use** 状态。

- **写**：对 in-use 缓冲区的更新（包括通过本地和远端发起的 NVSHMEM 操作进行的更新）都是未定义行为。
- **读**：只有当该缓冲区是以 **const 限定**的参数传给使它处于 in-use 状态的那个 NVSHMEM 例程时，才允许读取；否则行为未定义。
- 例外：被 AMO（原子内存操作）置于 in-use 状态的缓冲区，见文档 *Atomicity Guarantees* 一节。
- 关于 NVSHMEM 操作何时算完成，见文档 *Memory Ordering* 一节。

### 3.3 对称对象可跨段

带多个对称对象参数的 NVSHMEM 例程，**不要求**这些对称对象位于同一个对称内存段中。例如，位于对称数据段的对象和位于对称堆的对象，可以作为同一次 NVSHMEM 操作的参数一起传入。

## 要点速查

1. PE = 进程，典型为 SPMD，允许控制流分歧；启用线程支持后可多线程。
2. `nvshmem_init(_thread)` 之前、`nvshmem_finalize` 之后调用任何 API 都是 UB；finalize 后不能重新 init。
3. `nvshmem_finalize` 内含一次隐式集合同步。
4. PE id 从 0 连续编号，全程固定。
5. 通信推进不依赖目标 PE 主动进入 NVSHMEM 调用。
6. 同一操作的非 const 指针参数不得重叠（in-place 归约除外）。
7. 操作完成前缓冲区 in-use：不可写；只有 const 参数可读。
8. 多个对称对象参数可以来自不同对称内存段。
