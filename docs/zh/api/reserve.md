# 预留接口

本章节列出的接口均为预留接口，后续可能存在变更，暂不支持开发者使用。

## Host Hcomm API

| 接口名 | 描述 |
| ---- | ---- |
| MakeMultiChannelHandle | 基于HCCL通信域或Hcomm Endpoint创建一组共享Jetty多通道及Device侧多通道句柄。 |
| DestroyMultiChannelHandle | 销毁MakeMultiChannelHandle（Endpoint版本）创建的多通道句柄及其资源。 |

## AICore Hcomm API

### Hcomm类

| 接口名 | 描述 |
| ---- | ---- |
| Hcomm | AI Core侧点对点通信接口模板，提供普通和批量两种调用链，支持读、写、写通知、原子操作和归约写任务。 |
| Init | 使用调用者提供的UB缓冲区初始化Hcomm通信工作区。 |
| MakeBatchHandle | 基于通信通道和UB缓冲区创建批量任务句柄，用于批量准备WQE。 |
| GetHandleRef | 获取多通道批量句柄中指定逻辑通道的执行批量句柄引用。 |
| WriteNbi | 通过指定通道异步将本端数据写入远端地址。 |
| WriteValueNbi | 通过指定通道异步向远端写入内联立即数，数据随WQE携带。 |
| WriteReduceNbi | 通过指定通道将本端数据写到远端并在远端执行规约。 |
| WriteWithNotifyNbi | 通过指定通道写远端数据，并在远端通知地址写入通知值。 |
| AtomicFAA | 对远端地址执行原子Fetch-and-add操作，并回读旧值。 |
| AtomicCAS | 对远端地址执行原子比较交换操作，并回读旧值。 |
| ReadNbi | 通过指定通道异步读取远端数据到本端地址。 |
| Commit | 通知服务端执行指定通道上已提交但未下发的通信任务。 |
| BatchCommit | 一次性复制并提交批量句柄中准备的全部WQE。 |
| Drain | 阻塞当前AI Core，等待指定通道上的通信任务全部完成。 |
| Lock | 获取通道的跨AI Core锁，保护通道状态的并发访问。 |
| Unlock | 刷新通道状态并释放跨AI Core锁。 |

### SIMT Hcomm类

| 接口名 | 描述 |
| ---- | ---- |
| Hcomm | Hcomm在SIMT执行空间的对应接口模板，提供相同的点对点通信原语，适用于SIMT Kernel侧代码。 |
| Init | 初始化SIMT Hcomm接口，UB_CTP协议下无需临时工作区。 |
| WriteNbi | 将本端数据写入远端地址，支持普通、批量和Group协作重载。 |
| ReadNbi | 将远端数据读取到本端地址，支持普通、批量和Group协作重载。 |
| WriteValueNbi | 提交立即数写任务，立即数按值携带在任务中写入远端，不依赖本端源缓冲区。 |
| WriteWithNotifyNbi | 将本端数据写入远端地址，并携带远端通知地址和通知值。 |
| AtomicFAA | 提交Fetch-and-add原子任务，对远端地址执行原子加，并将旧值写入本端。 |
| AtomicCAS | 提交Compare-and-swap原子任务，远端值与比较值相等时替换，并将旧值写入本端。 |
| Drain | 等待通道上已提交的通信任务或批量句柄已提交任务完成，也支持等待完成集中全部已登记通道的目标完成。 |
| MakeBatchHandle | 创建批量句柄并绑定批量操作所需的UB工作区，支持单线程和Group协作两种模式。 |
| BatchCommit | 提交批量句柄当前批次中的全部任务，支持单线程和Group协作重载。 |
| InitCompletionSet | 初始化完成集（UbcCtpCompletionSet），接管多个共享CQ通道的完成信息处理，需在全新未使用的通道上以128字节对齐且清零的GM工作区初始化一次。 |
| PrepareCompletion | 在批量任务发布前，按WQE数量和SQ BB总数为指定通道登记一个待发布窗口，每个通道同时仅允许一个活动窗口。 |
| Progress | 非阻塞推进完成集，最多消费budget个已就绪的CQE，不等待未就绪的CQE，实际处理数量通过processed返回。 |
| WaitCompletion | 阻塞等待指定通道已登记窗口的目标完成，期间持续推进所有CQ，空轮询超过maxIdlePolls次后返回。 |
| LocalBufferAddr | 获取通道上本端注册内存缓冲区的基地址，bufferIdx为本端注册缓冲区表的索引。 |
| RemoteBufferAddr | 获取通道上远端注册内存缓冲区的基地址，bufferIdx为远端注册缓冲区表的索引。 |

### 数据结构

| 名称 | 描述 |
| ---- | ---- |
| BufDesc | 本端GM内存分段描述结构，包含分段起始地址addr和字节长度len，用于WriteNbi、ReadNbi等接口的分散/聚合批量重载。 |
| CommProtocol | 通信协议类型枚举，用于指定Hcomm模板的通信协议。 |
| WqeEntry | WQE控制配置结构，作为Hcomm数据面接口的config模板参数，控制任务的下发顺序、fence、完成记录生成与数据内联行为。 |
| HcommUrmaReduceOp | 规约操作类型枚举，作为WriteReduceNbi等接口的模板参数，支持MAX、MIN、SUM。 |

## AICPU Asccomm API

### 本地操作

| 接口名 | 描述 |
| ---- | ---- |
| asccomm_local_copy_on_thread | 提供本地内存拷贝功能，将src指向的长度为len的内存数据拷贝到dst指向的相同长度的内存中。 |
| asccomm_local_reduce_on_thread | 提供本地归约功能：将src中的count个dataType类型元素与dst中的对应元素按reduceOp进行计算，并将结果写回dst。 |

### 远端操作

| 接口名 | 描述 |
| ---- | ---- |
| asccomm_write_on_thread | 在指定Channel上发起单边写，将本端src指向的len字节数据写入远端dst指向的内存，接口调用方为src所在节点。 |
| asccomm_write_reduce_on_thread | 在指定Channel上发起归约写：将本端src中的count个dataType类型元素写入远端dst，并与dst中的对应元素按reduceOp计算，结果写回dst。 |
| asccomm_write_with_notify_on_thread | 在指定Channel上发起带通知的单边写：将本端src指向的len字节数据写入远端dst指向的通信内存，同时向远端发送通知。 |
| asccomm_write_reduce_with_notify_on_thread | 在指定Channel上发起带通知的归约写：将本端src中的count个dataType类型元素写入远端dst并按reduceOp计算，结果写回dst，同时向远端发送通知。 |
| asccomm_read_on_thread | 在指定Channel上发起单边读，将远端src指向的len字节数据读取到本端dst指向的内存，接口调用方为dst所在节点。 |
| asccomm_read_reduce_on_thread | 在指定Channel上发起归约读：从远端src读取count个dataType类型元素，与本端dst中的对应元素按reduceOp计算，结果写回dst。 |
| asccomm_batch_transfer_on_thread | 在指定线程和通道上异步提交一组传输任务，每个任务由AsccommBatchTransferDesc描述，支持单边写、单边读、写归约、归约读、带通知的写、带通知的归约写以及通知记录等操作类型。 |

### 同步与屏障

| 接口名 | 描述 |
| ---- | ---- |
| asccomm_thread_notify_record_on_thread | 向目标Thread的Notify发送同步信号，主要用于多Thread之间的同步等待场景。 |
| asccomm_thread_notify_wait_on_thread | 生成本地Notify等待任务，等待指定Thread上的同步信号。 |
| asccomm_thread_notify_wait_on_thread_with_default_timeout | 生成本地Notify等待任务，等待指定Thread上的同步信号，使用asccomm_set_notify_wait_timeout设置的默认超时时间。 |
| asccomm_channel_notify_record_on_thread | 向指定Channel的远端Notify发送同步信号，主要用于Channel两端同步等待场景。 |
| asccomm_channel_notify_wait_on_thread | 生成本地Notify等待任务，等待指定Channel上的同步信号。 |
| asccomm_channel_notify_wait_on_thread_with_default_timeout | 生成本地Notify等待任务，等待指定Channel上的同步信号，使用asccomm_set_notify_wait_timeout设置的默认超时时间。 |
| asccomm_aclrt_notify_record_on_thread | 向ACL Runtime通知（notify）发送同步信号，用于AICPU执行线程与Host侧ACL Runtime通知的同步。 |
| asccomm_aclrt_notify_wait_on_thread | 生成本地Notify等待任务，等待指定ACL Runtime通知上的同步信号，等待时长不超过timeOut。 |
| asccomm_channel_fence_on_thread | 在指定Channel上设置屏障标志，保证屏障前的通道读写操作先于屏障后的通道读写操作完成。 |
| asccomm_channel_drain_on_thread | 等待指定Channel上已提交的通信操作完成。 |

### 其他

| 接口名 | 描述 |
| ---- | ---- |
| asccomm_batch_mode_start | 开启当前AICPU执行线程的批量提交模式，批量模式期间需要下发的通信任务会先缓存。 |
| asccomm_batch_mode_end | 结束asccomm_batch_mode_start开启的批量提交模式，将当前执行线程在该模式下缓存的通信任务统一提交。 |
| asccomm_set_notify_wait_timeout | 设置当前AICPU执行线程通知等待的默认超时时间。 |
| asccomm_thread_res_acquire_timeout | 设置当前AICPU执行线程通信任务提交队列满时的等待超时时间。 |

## CCU API

### 资源创建与操作

| 接口名 | 描述 |
| ---- | ---- |
| variable | CCU kernel内通用寄存器的C++包装类，用于保存标量值，支持赋值、加法和移位运算。 |
| event | CCU kernel内同步寄存器槽位的C++包装类，用于标记异步操作（如数据搬运）的完成状态。 |
| address | CCU kernel内地址寄存器的C++包装类，用于保存地址值，支持赋值和加法运算。 |
| ccu_buffer | CCU kernel内片上CCU Buffer的C++包装类，每个切片固定4096个字节，用于在片上内存与对端之间中转数据。 |
| local_addr | CCU kernel内本端片上内存地址的C++包装类，是地址（address）与token（variable）的复合对象。 |
| remote_addr | CCU kernel内对端片上内存地址的C++包装类，是地址（address）与token（variable）的复合对象，结构与local_addr相同。 |
| array | CCU kernel内批量持有物理连续资源的C++模板类。 |
| get_res_by_channel | 在CCU kernel内获取指向channel共享variable槽位的句柄，不消耗新的通用寄存器。 |

### 参数加载/存储

| 接口名 | 描述 |
| ---- | ---- |
| load_arg | 在kernel函数体内调用，将host下发Kernel时通过taskArgs传入的参数加载到variable，作为其初值。 |
| load | 将指定地址处的数据加载到variable，支持从uint64_t地址或variable中存储的地址加载，单次可加载单个或多个连续的variable。 |
| store | 将variable的值存储到指定地址，支持写入uint64_t地址或variable中存储的地址，单次可存储单个或多个连续的variable。 |

### 数据搬运

| 接口名 | 描述 |
| ---- | ---- |
| local_copy | 支持本端片上内存、本端CCU Buffer之间的异步数据拷贝，硬件完成搬运时自动将event中mask对应的标志位设置为1。 |
| write | 通过已建链的ChannelHandle将本端数据异步写入对端片上内存，硬件完成时自动将event中mask对应的标志位设置为1。 |
| write_reduce | 将本端数据异步写入对端片上内存，同时按op_type指定的算子与对端现有数据进行归约，硬件完成时自动将event中mask对应的标志位设置为1。 |

### 同步

| 接口名 | 描述 |
| ---- | ---- |
| event_record | 将本端同步寄存器中mask对应的event标志位显式设置为1，表示当前操作或阶段已完成，供同kernel内的event_wait读取完成信号。 |
| event_wait | 阻塞等待本端同步寄存器中mask对应的event标志位，直到其全部为1后继续执行，随后自动将这些标志位清零。 |
| notify_record | 通过channel向对端die发送完成信号，将对端同步寄存器中mask对应的notify标志位设置为1，适用于跨die场景。 |
| notify_wait | 阻塞等待本端同步寄存器中mask对应的notify标志位，直到其全部为1后继续执行。 |
| write_variable_with_notify | 将写对端variable值与触发对端notify信号合并为单条原子操作，硬件保证先写值后发通知的完成顺序。 |

### 流程控制

| 接口名 | 描述 |
| ---- | ---- |
| CCU_IF | 在CCU kernel内开始一个软件条件分支块，条件在执行阶段由CCU硬件计算，条件满足时执行then代码块。 |
| loop | CCU kernel内的硬件循环类，用于描述一段需要重复执行的搬运逻辑，循环体指令只记录一份，由硬件LoopEngine自动迭代执行。 |
| loop_group | 将多个loop对象组织为一组，共享同一份LoopEngine资源池，避免多个独立loop各自独占LoopEngine导致资源耗尽。 |
| func | 将一个lambda封装为可复用的函数块，首次调用时将逻辑合成为一份指令块，后续调用复用同一份指令块。 |
| call_func | 函数块的调用接口，与func配合使用，首次调用时合成函数块并追加跳转指令，后续调用只追加跳转指令。 |
