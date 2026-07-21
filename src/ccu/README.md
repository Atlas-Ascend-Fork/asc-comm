# CCU data plane

`libasccomm_ccu_dataplane.so` owns CCU Kernel registration, REP generation,
translation, launch, memory-token lookup and all `Ccu*` primitives. These
public symbols are exported directly by the data-plane library. Applications
must link both `libhcomm.so` and `libasccomm_ccu_dataplane.so`.

`HcommCcuInsCreate` and `HcommCcuInsDestroy` remain in hcomm. hcomm uses the
versioned, size-tagged `AscCommCcuGetDataPlaneV1` private ABI only for instance
lifecycle and Channel snapshot synchronization. The table contains no public
register, launch, token or primitive forwarding entry.

hcomm owns `CcuUrmaChannel` and its transport objects. Once a CCU Channel is
connected, hcomm copies the die/channel identifiers, CKE/XN identifiers and
remote-buffer address, size and token into asc-comm. REP generation resolves a
`ChannelHandle` only against that local snapshot. No Channel pointer, transport
object or C++ RTTI crosses the component boundary.

hcomm allocates the instance and translator resource warehouses together and
passes a copied resource snapshot plus device properties to `instanceCreate`.
asc-comm does not own allocator handles and never loads `libhcomm.so`. The same
call injects a minimal process-level host-service table for dynamic instruction
allocation/submission, user-memory tokens, launch-context lookup and task
reporting. Launch executes `rtCCULaunch` inside asc-comm and reports successful
tasks through that table for profiling and task callbacks.
