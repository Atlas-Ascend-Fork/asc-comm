# CPU_TS Asccomm接口简介

CPU_TS Asccomm提供Host CPU侧使用的Notify同步C接口，用于在Host CPU与AI CPU执行线程之间发送通知信号并提交等待任务。

安装后的公开头文件路径为`"cpu_ts/asccomm_cpu_primitives.h"`。接口实现在`libasccomm_cpu.so`中，调用方需要在链接阶段链接该动态库。

与AICPU_TS Asccomm接口的区别：

- AICPU_TS Asccomm接口运行在AI CPU上，负责设备侧本地操作、远端数据搬运和线程/通道同步，详见[AICPU_TS Asccomm接口简介](../../aicpu_ts/asccomm/README.md)。
- CPU_TS Asccomm接口运行在Host CPU进程中，当前只提供Notify的记录与等待能力，不提供数据搬运、Reduce、通道同步或批量提交能力。
- CPU_TS接口使用`uint64_t`承载控制面导出的线程对象地址；AICPU_TS接口使用`ThreadHandle`承载AI CPU线程资源句柄。
