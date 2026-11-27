# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Public names for observed executors, without guessing from class names.

REGISTRATIONS copies the (operator, registeredName, externalName) fields of
asc-devkit REGISTER_ALG_META at SOURCE_COMMIT. Installed-SDK log mappings take
precedence. This table is a display fallback, never evidence of execution or
support; runtime needs neither the SDK source tree nor internal SDK symbols.
"""

import re

SOURCE_COMMIT = "4d343cc39131f257a1ea380396151cf4a532a4be"
REGISTRATIONS = {
    ("allgather", "concur[mesh,nhr]"): ("AicpuAllGatherConcurMeshNHR",),
    ("allgather", "pipeline[mesh,nhr]"): (
        "AicpuAllGatherPipeLineMeshNHR",
        "AicpuAllGatherPipeLinePcie",
    ),
    ("allgather", "sequence[mesh,nhr]"): (
        "AicpuAllGatherSequenceMeshConcurNHR",
        "InsAllGatherMeshNhrDPU",
    ),
    ("allgather", "sole[nhr]"): (
        "AicpuAllGatherSoleNHR",
        "CcuSchedAllGatherSoleNHR",
    ),
    ("allgather", "sole[nhr.multi_channel]"): ("AicpuAllGatherSoleNHRMultiLink",),
    ("allgather", "concur[mesh,nhr.multi_channel]"): (
        "CcuSchedAllGatherConcurMeshNHRMultiLink",
    ),
    ("allgather", "parallel[mesh,nhr.multi_channel]"): (
        "CcuSchedAllGatherParallelMeshNHRMultiLink",
        "InsAllGatherParallelMesh1DNHRMultiJetty",
    ),
    ("allgather", "sole[mesh]"): ("CcuSchedAllGatherSoleMesh", "InsAllGatherMesh1D"),
    ("allgather", "parallel[mesh,nhr]"): (
        "InsAllGatherParallelMesh1DNHR",
        "InsAllGatherParallelMesh1DNHRPcie",
    ),
    ("allreduce", "parallel[mesh,nhr]"): (
        "AicpuAllReduceParallelMeshNHR",
        "AicpuAllreduceParallelMeshNHRPcie",
    ),
    ("allreduce", "pipeline[mesh,nhr]"): ("AicpuAllReducePipeLinePcie",),
    ("allreduce", "sole[mesh.two_shot.chunk]"): ("AicpuAllReduceSoleMeshChunkTwoShot",),
    ("allreduce", "sole[mesh.one_shot]"): ("AicpuAllReduceSoleMeshOneShot",),
    ("allreduce", "sole[mesh.two_shot]"): ("AicpuAllReduceSoleMeshTwoShot",),
    ("allreduce", "sole[nhr]"): (
        "AicpuAllReduceSoleNHR",
        "AicpuAllReduceSoleNHRAicpuReduce",
        "CcuSchedAllReduceSoleNHR",
    ),
    ("allreduce", "sole[mesh]"): ("CcuSchedAllReduceSoleMesh",),
    ("alltoall", "sole[mesh]"): (
        "AicpuAllToAllSoleMesh",
        "AicpuAllToAllSoleMeshUBX",
        "CcuSchedAllToAllSoleMesh",
    ),
    ("alltoall", "concur[mesh,mesh]"): (
        "AicpuAllToAllSoleMeshConcurrent",
        "CcuSchedAllToAllSoleMeshConcurrent",
    ),
    ("alltoall", "sole[mesh.single_channel]"): ("AicpuAllToAllSoleMeshSingleChannel",),
    ("alltoall", "sole[mesh.multi_channel]"): ("CcuSchedAllToAllMesh1DMultiJetty",),
    ("alltoallv", "sole[mesh]"): (
        "AicpuAllToAllVSoleMesh",
        "CcuSchedAllToAllVSoleMesh",
    ),
    ("alltoallv", "concur[mesh,mesh]"): (
        "AicpuAllToAllVSoleMeshConcurrent",
        "CcuSchedAllToAllVSoleMeshConcurrent",
    ),
    ("alltoallv", "sole[mesh.multi_channel]"): (
        "AicpuAllToAllVSoleMeshMultiJetty",
        "CcuSchedAllToAllVSoleMeshMultiJetty",
    ),
    ("alltoallv", "sole[mesh.pairwise]"): ("AicpuAllToAllVSolePairwise",),
    ("alltoallvwrite", "sole[mesh]"): ("CcuHalfAll2AllVMesh1D",),
    ("reducescatter", "concur[mesh,nhr]"): ("AicpuReduceScatterConcurMeshNHR",),
    ("reducescatter", "parallel[mesh,nhr]"): (
        "AicpuReduceScatterParallelMeshNHRUBX",
        "InsReduceScatterParallelMesh1DNHR",
        "InsReduceScatterParallelMesh1DNHRPcie",
    ),
    ("reducescatter", "pipeline[mesh,nhr]"): ("AicpuReduceScatterPipeLinePcie",),
    ("reducescatter", "sole[mesh.chunk]"): ("AicpuReduceScatterSoleMeshChunk",),
    ("reducescatter", "sole[nhr.multi_channel]"): (
        "AicpuReduceScatterSoleNHRMultiLink",
        "CcuSchedReduceScatterSoleNHRMultiLink",
    ),
    ("reducescatter", "concur[mesh,nhr.multi_channel]"): (
        "CcuSchedReduceScatterConcurMeshNHRMultiLink",
    ),
    ("reducescatter", "sole[mesh]"): (
        "CcuSchedReduceScatterSoleMesh",
        "CcuSchedReduceScatterSoleMeshPeerOnly",
        "InsReduceScatterMesh1D",
        "InsReduceScatterMesh1DZAxisDetour",
    ),
    ("reducescatter", "sole[nhr]"): (
        "CcuSchedReduceScatterSoleNHR",
        "InsReduceScatterAicpuReduceNHR",
        "InsReduceScatterNHR",
    ),
    ("reducescatter", "sequence[mesh,nhr]"): ("InsReduceScatterSequenceMesh1DNhr",),
    ("reducescatter", "sequence[mesh,mesh]"): ("InsReduceScatterSequenceMeshMeshDPU",),
}

CANDIDATES = re.compile(
    r"\[MC2_EXT_ALG\]\s+op\[(\d+)\]\s+ext\[(.*?)\]\s+"
    r"engine\[([^\]]+)\]\s+candidates\((\d+)\):\s*\[([^\]]*)\]"
)


def is_external_name(value):
    """Check the public-name boundary without interpreting the SDK grammar."""
    if not isinstance(value, str) or not value or value.strip() != value:
        return False
    if any(ord(char) < 32 or ord(char) == 127 for char in value):
        return False
    if value.find("[") <= 0 or not value.endswith("]"):
        return False
    depth = 0
    for char in value:
        if char == "[":
            depth += 1
        elif char == "]":
            if depth == 0:
                return False
            depth -= 1
        elif char == "," and depth == 0:
            return False
    return depth == 0


def candidate_mappings(line, op_type, engine):
    """Read explicit SDK candidate mappings, including condition annotations."""
    match = CANDIDATES.search(line)
    if not match or int(match[1]) != op_type:
        return set()
    engines = {"AICPU", "AICPU_TS"} if engine == "aicpu_ts" else {"CCU"}
    external = match[2].replace(" ", "").replace("\t", "")
    if match[3] not in engines or not is_external_name(external):
        return set()
    names = re.findall(
        r"(?:^|,)\s*([A-Za-z_][A-Za-z0-9_]*)(?:\([^()]*\))*\s*(?=,|$)", match[5]
    )
    if len(names) != int(match[4]):
        return set()
    return {(external, name) for name in names}


def resolve(op, executor, mappings):
    """Return one public name and its source; conflicts/unknowns stay blank."""
    names = {
        external
        for external, registered in mappings
        if registered == executor and is_external_name(external)
    }
    if names:
        return (
            (next(iter(names)), "sdk-log")
            if len(names) == 1
            else ("", "conflicting_sdk_mappings")
        )
    names = {
        external
        for (operator, external), executors in REGISTRATIONS.items()
        if operator == op and executor in executors
    }
    return (
        (next(iter(names)), "sdk-registration:" + SOURCE_COMMIT)
        if len(names) == 1
        else ("", "missing_or_conflicting_external_mapping")
    )
