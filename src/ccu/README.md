# CCU Data Plane

The asc-comm CCU data plane is organized by responsibility:

- `include/ccu/hcomm`: public CCU headers.
- `src/ccu/hcomm/primitives`: primitive and launch API implementations.
- `src/ccu/hcomm/resource`: instance, kernel, microcode, and representation implementations.
- `src/ccu/hcomm/common`: data-plane compatibility and logging helpers.

The build produces one standalone data-plane shared-library target:

- `asccomm_ccu_dataplane`: CCU data-plane execution support, primitive C API,
  launch/token ABI, CCU kernel/template code, and device-side CCU headers. This
  target builds `libasccomm_ccu_dataplane.so`.

Control-plane entry points such as hcomm communicator/resource selection,
device/channel management, DFX, and resource query remain in hcomm. `asccomm_ccu_kernel_register*`,
`asccomm_ccu_kernel_launch`, and `asccomm_ccu_get_mem_token` are exported by this
data-plane SO.

Build checks:

```bash
cmake -S . -B build/ccu-check -DASCCOMM_BUILD_CCU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/ccu-check --target asccomm_ccu_dataplane_compile_check -j 8
```

`asccomm_ccu_compile_check` and `asccomm_ccu_dataplane_compile_check` both
validate the standalone data-plane SO.
