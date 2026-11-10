# CCU API Introduction

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:26:13.686Z pushedAt=2026-11-09T06:47:55.003Z nl=2 -->

CCU programming APIs fall into two categories: control plane and data plane. Control-plane APIs are called on the host side and handle preparatory work such as link establishment, resource reservation, and kernel compilation and dispatch. For details, see [hcomm control-plane API](https://gitcode.com/cann/hcomm/tree/master/docs/en/api_ref/comm_opdev/control_plane_api). Data-plane APIs are called inside the kernel function body and describe communication logic such as data movement and synchronization, which is the focus of this chapter. A CCU communication task is completed by the two categories of APIs working together: the control plane passes the prepared links and resources into the kernel, and the communication logic described by the data plane is compiled and translated before being executed by the CCU hardware.

## Data-Plane API Execution Mechanism

The lifecycle of a data-plane API consists of two phases: compilation and dispatch.

- **Compilation phase**: During `<<<>>>` direct call, the kernel function body is executed once. The data-plane API calls within it do not trigger hardware operations; they are only translated into CCU instructions one by one.
- **Dispatch phase**: After `<<<>>>` dispatches a task, the CCU hardware executes the translated instruction sequence to perform the actual data movement and synchronization.

## Data-Plane API Classification

CCU data-plane APIs are classified by function as follows:

- [Resource Creation and Operations](./resource_allocation_operation/README.md)
- [Parameter Loading/Storage](./arg_load_store/README.md)
- [Data Movement](./data_movement/README.md)
- [Synchronization](./synchronization/README.md)
- [Flow Control](./execution_control/README.md)
