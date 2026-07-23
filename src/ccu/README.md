# CCU data plane

`libasccomm_ccu_dataplane.so` owns CCU Kernel registration, REP generation,
translation, launch, memory-token lookup and all `Ccu*` primitives. These
public symbols are exported directly by the data-plane library. Applications
must link both `libhcomm.so` and `libasccomm_ccu_dataplane.so`.

`HcommCcuInsCreateLegacy` and `HcommCcuInsDestroyLegacy` remain in hcomm. hcomm
uses the versioned, size-tagged `AscCommCcuGetDataPlaneV1` private ABI only for
legacy instance lifecycle and Channel snapshot synchronization. The public
resource-description APIs, including `HcommCcuInsCreate` and
`HcommCcuInsDestroy`, are owned by asc-comm. The table contains no public
register, launch, token or primitive forwarding entry.

hcomm owns `CcuUrmaChannel` and its transport objects. Once a CCU Channel is
connected, hcomm copies the die/channel identifiers, CKE/XN identifiers and
remote-buffer address, size and token into asc-comm. REP generation resolves a
`ChannelHandle` only against that local snapshot. No Channel pointer, transport
object or C++ RTTI crosses the component boundary.

hcomm keeps the CCU allocator and instruction-submission implementation.
asc-comm requests instance, translator and instruction resources at the points
where Register consumes them, through private C resource symbols whose
structured arguments are opaque pointers at the component boundary. Resource
handles are released by the side that requested them; no hcomm allocator object
is transferred to asc-comm. This intentionally preserves a runtime dependency
on hcomm while avoiding a shared CCU public-type header.

`instanceCreate` passes device properties but no physical-resource snapshot.
Its minimal process-level host-service table contains only user-memory token,
launch-context and task-report callbacks. Launch executes `rtCCULaunch` inside
asc-comm and reports successful tasks through that table for profiling and task
callbacks.
