# CPU_TS Asccomm API Introduction

CPU_TS Asccomm provides C synchronization APIs used by the Host CPU to send notification signals and submit notification wait tasks between the Host CPU and an AI CPU execution thread.

After installation, the public header is `"cpu_ts/asccomm_cpu_primitives.h"`. The APIs are implemented in `libasccomm_cpu.so`; callers must link against this shared library.

Differences from the AICPU_TS Asccomm APIs:

- The AICPU_TS Asccomm APIs run on the AI CPU and provide local operations, remote data movement, and thread or channel synchronization. For details, see [AICPU_TS Asccomm API Introduction](../../aicpu_ts/asccomm/README.md).
- The CPU_TS Asccomm APIs run in a Host CPU process and currently provide only Notify record and Notify wait capabilities. They do not provide data movement, reduction, channel synchronization, or batch submission.
- The CPU_TS APIs use `uint64_t` values to carry thread object addresses exported by the control plane; the AICPU_TS APIs use `ThreadHandle` values that represent AI CPU thread resources.
