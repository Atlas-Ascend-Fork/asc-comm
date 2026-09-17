# CCU 同步接口测试样例对照表

五个同步接口（`event_record`、`event_wait`、`notify_record`、`notify_wait`、`write_variable_with_notify`）在 `./asc-comm/examples/ccu` 各样例中的覆盖情况。调用点均位于各样例的 `op_kernel_ccu/ccu_kernel.cc`。

| 样例 | event_record | event_wait | notify_record | notify_wait | write_variable_with_notify |
|---|---|---|---|---|---|
| ccu_allgather | ✓ :168 | ✓ :171 | ✓ :226 | ✓ :220,229 | ✓ :212,214 |
| ccu_allgather_mesh1d_mem2mem | ✓ :236 | ✓ :253 | ✓ :221 | ✓ :214,224 | ✓ :207,209 |
| ccu_allgather_nhr1d_mem2mem | ✗ | ✓ :272 | ✓ :211,278 | ✓ :204,214,279 | ✓ :197,199 |
| ccu_allreduce | ✗ | ✓ :46,67 | ✓ :49,122 | ✓ :52,114,126 | ✓ :106,108 |
| ccu_broadcast | ✗ | ✓ :58 | ✓ :90 | ✓ :30,93 | ✓ :23,25 |
| ccu_all_to_all | ✓ :206 | ✓ :226 | ✓ :287 | ✓ :279,291 | ✓ :271,273 |
| ccu_all_to_allv | ✓ :85 | ✓ :92 | ✓ :126 | ✓ :54,129 | ✓ :45,47 |
| ccu_all_to_all_vc | ✓ :85 | ✓ :92 | ✓ :126 | ✓ :54,129 | ✓ :45,47 |
| ccu_reduce_scatter | ✓ :201 | ✓ :202,230 | ✓ :205,291 | ✓ :208,283,295 | ✓ :275,277 |

## 说明

- **event_wait / notify_record / notify_wait / write_variable_with_notify**：上述 9 个样例全部覆盖，均可测试。
- **event_record**：仅 6 个样例覆盖（ccu_allgather、ccu_allgather_mesh1d_mem2mem、ccu_all_to_all、ccu_all_to_allv、ccu_all_to_all_vc、ccu_reduce_scatter）；ccu_allgather_nhr1d_mem2mem、ccu_allreduce、ccu_broadcast 未用到。
- **ccu_builtin、ccu_direct**：不含这 5 个同步接口。
- 冒号后数字为该接口在 `op_kernel_ccu/ccu_kernel.cc` 中的行号，✓ 表示可测，✗ 表示未覆盖。
