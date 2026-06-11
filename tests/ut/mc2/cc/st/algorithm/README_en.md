# Algorithm Analyzer User Guide

## Tool Introduction

The HCCL algorithm analyzer simulates HCCL algorithm execution in an offline environment. It verifies algorithm logic and memory operations, and efficiently executes test tasks to meet developer requirements.

## Principle Introduction

![](../../../docs/figures/algorithm_analyzer/introduction-theory.png)

**Key Points:**

1. The algorithm analyzer stubs the dependencies (hcomm and runtime interfaces) of the HCCL single operator execution flow. During algorithm execution, it captures Task sequences from all ranks.
2. It organizes Task information from all ranks into a **directed acyclic graph**.
3. It performs validations based on **graph algorithms**, such as memory read-write conflict validation and semantic validation.
   - Memory conflict validation analyzes whether potential read-write conflicts exist based on synchronization in the graph.
   - Semantic validation simulates Task graph execution and records **data transfer information**. After simulation completes, it checks whether the **data transfer information** in UserOutput memory meets the operator requirements.

## Environment Preparation

Follow the environment preparation, source code download, compilation, and installation steps in [Source Code Build](../../../docs/build.md) to prepare for algorithm analyzer compilation.

## Test Case Writing

### LLT Test Case Overview

An algorithm checker test case consists of 5 steps, as shown below. The following sections describe how to write each step to accommodate different operator requirements. Finally, it explains how to use the checker tool for issue diagnosis.

![](../../../docs/figures/algorithm_analyzer/compile_testcase_1.png)

### LLT Test Case Step Details

#### Simulation Model Initialization

   - **TopoMeta Structure Introduction**

     ![](../../../docs/figures/algorithm_analyzer/compile_testcase_2.png)

     The checker uses TopoMeta to represent a topology. TopoMeta is a three-layer vector structure.

     - PhyDeviceId represents the physical ID of an NPU.

     - ServerMeta consists of PhyDeviceIds and represents the number of cards in a server and their corresponding PhyDeviceIds.

     - SuperPodMeta consists of ServerMetas and represents the servers that form a super node.

     - TopoMeta represents the overall topology of the cluster.

   - **TopoMeta Generation Methods**

     There are two ways to generate TopoMeta:

     1. Specify the number of super nodes, servers, and cards per server, then use the provided GenTopoMeta function to generate it. This applies to symmetric topology scenarios.

        ![](../../../docs/figures/algorithm_analyzer/compile_testcase_3.png)

     2. Fully customize super nodes, servers, and card counts. This applies to both symmetric and asymmetric topology scenarios, as shown below.

        ![](../../../docs/figures/algorithm_analyzer/compile_testcase_4.png)

   - **Model Initialization**

     Pass in the generated TopoMeta and specify the device type for simulation.

     ![](../../../docs/figures/algorithm_analyzer/compile_testcase_5.png)

#### Operator Parameter Settings

- Operator Execution Parameters

     Using Scatter as an example, you need to set some input parameters for executing the HcclScatter operator and validation. The specific parameters are:
    - root: Set the root node. The Scatter operation distributes data from the root node in the communication domain evenly to other Ranks.
    - rankSize: The number of Ranks participating in collective communication in this communication domain (must be consistent with the number of cards in topoMeta).
    - recvCount: The amount of data each Rank receives from the root node.
    - dataType: The data type corresponding to recvCount.

     For other operators or custom operator scenarios, set parameters according to the operator requirements.
     ![](../../../docs/figures/algorithm_analyzer/compile_testcase_5-1.png)

- Set Environment Variables

   Environment variables affect judgment logic in the code. Use the setenv function to set the required conditions before test case execution.

- Important Notes

   - Supported operators: Currently only the scatter operator is supported.
   - Supported modes: Currently only OPBASE single operator mode is supported.
   - Supported device types: Currently only DEV_TYPE_910B and DEV_TYPE_91093 (represents DEV_TYPE_910C) are supported.

#### Operator Execution Flow

As shown below, run the single operator flow in a multi-threaded manner.
![](../../../docs/figures/algorithm_analyzer/compile_testcase_6.png)

1. Construct operator input parameters.
   
   Construct the parameters required for single operator execution, including:
   - SetDevice: Binds a thread to a Rank so that each thread simulates a corresponding Rank.
   - Main stream resource creation: Call the aclrtCreateStream interface, with stub implementation to simulate stream resource creation.
   - Communication domain initialization: Call HcclCommInitClusterInfo, with stub implementation to simulate communication domain creation.
   - Input/output memory allocation: Call aclrtMalloc, with stub implementation to simulate memory creation and mark memory types. Users must calculate the required memory in bytes based on operator type, quantity, and data type.

2. Operator dispatch.

   Call the HcclScatter operator and pass in the constructed parameters above. For custom operator scenarios, replace this with the custom operator API and modify the operator parameters above to match the custom operator requirements.

3. Communication domain destruction.

   Call the HcclCommDestroy interface to destroy the communication domain.

#### Result Graph Validation

Get the Task queue from all Ranks and call the corresponding operator validation function. For the Scatter operator, call CheckScatter and pass in the Task queue and the parameters required for Scatter operator validation. The gtest framework prints based on the validation result return value.

#### Resource Cleanup

The final step of a single test case execution is to clean up simulation model resources to avoid interference with the next test case execution.

### Test Case Filtering and Debugging

When there are many test cases and you only need to execute one, modify the test case name in main.cc.

![](../../../docs/figures/algorithm_analyzer/compile_testcase_7.png)

## Test Case Compilation and Execution

Compile and execute algorithm analyzer test cases:

```bash
# Enter algorithm analyzer directory /hccl/test/st/algorithm
cd ./hccl/test/st/algorithm

# Compile test cases and automatically execute
bash build.sh
```

## Result Example

Test case execution results are shown below:

![](../../../docs/figures/algorithm_analyzer/result_1.png)

The meaning of each field:

[run]: Indicates the test case being executed for validation

\[OK\]: Indicates successful execution, validation passed

\[FAIL\]: Indicates execution failure. Analyze the specific reason based on console logs.

## Issue Diagnosis

### Memory Conflict Validation Diagnosis Method

#### Issue Phenomenon<a name="en-us_topic_0000002306628476_section158963105533"></a>

Memory conflicts occur when a memory region between two synchronization signals is written concurrently by multiple tasks, or is written while being read. In actual runtime environments, this typically manifests as randomly occurring precision issues.

Under the current Mesh structure, if a Reduce operator exists, false positives may occur. The reason is that under Mesh structure, a memory block may be written by other cards simultaneously within one synchronization. Hardware ensures the atomicity of Reduce operations, so no precision issues occur in actual runtime. However, from the checker's perspective, multiple read-write operations on the same memory between two synchronizations are detected, so it is flagged as an error.

Except for the above scenario, if the following error appears, it indicates a memory conflict risk in task scheduling:

```
[1]there is memory use confilict in two SliceMemoryStatus
[2]one is startAddr is 0, size  is 3200, status is WRITE.
[3]another is startAddr is 0, size  is 3200, status is WRITE.
[4]failed to check memory BufferType::OUTPUT_CCL
[5]memory conflict between node [rankId:1, queueId:0, index:1] and node [rankId:2, queueId:0, index:1]
[6]check rank memory conflict failed for rank 0
```

-   Lines 2 and 3 indicate the start address (startAddr), size, and read/write status (status) of the two conflicting memory blocks.

     status has two states: READ and WRITE. READ indicates the memory block is being read, WRITE indicates the memory block is being written. Being read and being written are abstract memory operation semantics, not just write task and read task.

     Memory blocks that may be in READ status include: localcopy task src, read task src, write task src. Memory blocks that may be in WRITE status include: localcopy task dst, read task dst, write task dst.

-   Line 4 indicates the type of the conflicting memory block.
-   Line 5 indicates which two tasks caused the memory conflict.
-   Line 6 indicates the rank number where the memory conflict occurred.

The above error log indicates that two tasks are simultaneously performing write operations to the range 0-3200 of OUTPUT\_CCL type.

#### Diagnosis Method<a name="en-us_topic_0000002306628476_section4483726165314"></a>

  Based on the error log, find the two tasks that caused the memory conflict and investigate the synchronization scheduling before and after these two tasks.

  The error log in [Issue Phenomenon](#en-us_topic_0000002306628476_section158963105533) indicates that two tasks are simultaneously performing write operations to the range 0-3200 of OUTPUT\_CCL type.

### Semantic Validation Failure Diagnosis Method<a name="EN-US_TOPIC_0000002359685061"></a>

#### Semantic Validation Basic Concepts<a name="en-us_topic_0000002306468780_section1118514685412"></a>

The algorithm analyzer uses relative addresses to represent memory, composed of three fields: memory type, offset address, and size, represented by the DataSlice struct:

```
class DataSlice {
public:
    //  Some method functions

private:
    BufferType type;
    u64        offset;
    u64        size;
}
```

Memory supports types such as Input, Output, and CCL.

Collective communication algorithms involve complex data transfer and reduction operations during execution. The algorithm analyzer uses **BufferSemantic** to record **data transfer relationships**, which includes a destination memory expression and multiple source memory expressions. The destination memory is represented by member variables startAddr and Size. The source memory is represented by the SrcBufDes struct, defined as follows:

```
struct BufferSemantic {
    u64                         startAddr;
    mutable u64                 size;       // Size, source and destination memory share the same size
    mutable bool                isReduce;   // Whether reduction is performed, true when srcBufs has multiple entries
    mutable HcclReduce0p        reduceType; // Type of reduction operation
    mutable std::set<SrcBufDes> srcBufs;    // Which rank(s) this data comes from
};

struct SrcBufDes {
    RankId      rankId;   // Source rankId
    BufferType  bufType;   // Source memory type
    mutable u64 srcAddr;  // Offset address relative to source memory type
};
```

#### Semantic Calculation Example<a name="en-us_topic_0000002306468780_section821014556581"></a>

The following example explains what semantic calculation is.

1.  Initial state: There are two Ranks, Rank0 and Rank1, with two memory types, Input and Output.

    ![](../../../docs/figures/algorithm_analyzer/allgather.png)

2.  State one action: Transfer the data block from rank0's Input with offset address 20 and size 30 to rank0's Output with offset address 35. Result: A semantic block is generated on rank0's Output, recording this transfer information.

    ![](../../../docs/figures/algorithm_analyzer/allgather-0.png)

3.  State two action: Transfer the data block from rank1's Input with offset address 70 and size 15 to rank0's Output with offset address 50. Result: The destination memory overlaps with an existing semantic block, requiring the existing semantic block to be split, generating two semantic blocks.

    ![](../../../docs/figures/algorithm_analyzer/allgather-1.png)

#### Result Validation<a name="en-us_topic_0000002306468780_section1791912221916"></a>

During semantic analysis execution, many semantic blocks are generated (recording many data transfer relationships). After execution completes, validate whether the semantic blocks in Output memory meet expectations.

The following example uses 2-rank AllGather to illustrate normal and abnormal scenarios for semantic blocks in Rank0's Output memory. Assume input data size is 100 bytes.

-   **Correct Scenario:**

    ![](../../../docs/figures/algorithm_analyzer/allgather-2.png)

-   **Error Scenario:**

    ![](../../../docs/figures/algorithm_analyzer/allgather-3.png)

#### Diagnosis Approach<a name="en-us_topic_0000002306468780_section193706117214"></a>

The semantic validation phase can detect two types of errors:

-   Missing data.
-   Incorrect data source.

Extended to reduction scenarios, similar issues exist, such as missing ranks participating in reduction, inconsistent data offset addresses participating in reduction, and so on. Normally, when semantic errors occur, the system provides certain hints. You need to use these hints combined with the task sequence printed by the algorithm analyzer for specific analysis.
