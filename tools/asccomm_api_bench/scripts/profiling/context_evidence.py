# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""CCU context lifetimes within one verified native rank process.

Creation registers a resource; it does not change a process-wide active executor.
Explicit cache gets can select an older resource. Without gets, inheritance is
restricted to a single executor and an unchanged requested configuration. The
caller verifies the native run/rank/PID; its session uses one communicator. SDK
tag prefixes are resource namespaces, not portable communication-group names.
"""

import re

from . import api_core as api
from . import external_algorithms as external
from . import legacy_algorithms as legacy

OPERATION = re.compile(
    r"\bHcclEngineCtx(?P<action>Create|Get|Destroy)\]?\s+"
    r"(?:success(?:fully)?\b|HIT\s+cache\b)",
    re.I,
)
TAG = re.compile(r"\b(?:ctxTag|algTag|tagOpResCtx|tag)\[")
DTYPE = (
    r"(?:HCCL_DATA_TYPE_)?"
    r"(?:fp16|fp32|fp64|bf16|bfp16|int8|int32|int64|uint8|uint64)"
)
DTYPE_HINT = re.compile(r"_(?:host|device)_(?P<dtype>" + DTYPE + r")(?=_|$)", re.I)


def _config(value):
    return value.replace(" ", "").replace("\t", "")


def _tag(line, start):
    match = TAG.search(line, start)
    if match is None:
        return None
    depth, end = 1, match.end()
    while end < len(line) and depth:
        depth += (line[end] == "[") - (line[end] == "]")
        end += 1
    return line[match.end() : end - 1] if depth == 0 else None


def context_event(line, pid, number, path, operator, dtype):
    """Decode positive operations, keeping the full tag as the resource key."""
    match = OPERATION.search(line)
    if match is None:
        return None
    tag = _tag(line, match.end())
    if not tag:
        return None
    # Keep the existing tool's SDK executor decoder. Request-resource tags
    # containing nested public names are not executor identity observations.
    record = legacy.ccu_algorithm_record_from_log_line(
        pid, number, "HcclEngineCtxCreate success ctxTag[" + tag + "]", path
    )
    if record is None:
        return None
    actual_op = record.operator
    if actual_op is None:
        operators = {
            op
            for (op, _name), executors in external.REGISTRATIONS.items()
            if record.algorithm in executors
        }
        if len(operators) == 1:
            actual_op = api.canonical_operator(next(iter(operators)))
    if actual_op is None or actual_op.casefold() != operator.casefold():
        return None
    position = tag.find(record.algorithm)
    tail = tag[position + len(record.algorithm) :]
    hint = DTYPE_HINT.search(tail)
    source_dtype = (
        api.normalize_dtype(hint["dtype"].casefold().removeprefix("hccl_data_type_"))
        if hint
        else None
    )
    # Tag layouts are SDK implementation details, not an execution-evidence
    # contract. The legacy decoder already identified the executor. Keep the
    # complete resource key without inferring a group from private prefixes.
    return {
        "timestamp": legacy.parse_run_log_timestamp(line),
        "action": match["action"].casefold(),
        "tag": tag,
        "executor": record.algorithm,
        "applicable": source_dtype is None or source_dtype == dtype,
        "file": str(path),
        "line": number,
    }


def bind_contexts(entries, intervals, plans, events, selections):
    """Attach evidence using one chronological sweep, without copying timings.

    Context operations in inter-entry gaps are retained. Future operations are
    never applied to an earlier window. Conflicting selector observations block
    implicit inheritance, even when their resource creation was not captured.
    """
    events = sorted(
        (
            event
            for event in events
            if event["timestamp"] is not None and event.get("applicable", True)
        ),
        key=lambda event: (event["timestamp"], event["file"], event["line"]),
    )
    selections = sorted(selections, key=lambda record: record.timestamp)
    active, removed, seen, requests = {}, {}, set(), set()
    cursor, selection_cursor, uncertain = 0, 0, False
    for begin, end, key in intervals:
        plan = plans.get(key[0])
        requested = _config(plan["algorithm"]) if plan else None
        if requested is not None:
            requests.add(requested)
        created, used, missing, lifecycle = [], [], False, False
        while cursor < len(events) and events[cursor]["timestamp"] <= end:
            event = events[cursor]
            cursor += 1
            if event["action"] == "unknown":
                uncertain = True
                continue
            tag = event["tag"]
            within = event["timestamp"] >= begin
            if event["action"] == "create":
                value = dict(event, origin=key if within else None)
                active[tag] = value
                removed.pop(tag, None)
                if within:
                    created.append(value)
            elif event["action"] == "destroy":
                active.pop(tag, None)
                removed[tag] = event
                lifecycle = lifecycle or within
            elif within:
                value = active.get(tag)
                if value is None or value["executor"] != event["executor"]:
                    missing = True
                else:
                    used.append((value, event))
        while (
            selection_cursor < len(selections)
            and selections[selection_cursor].timestamp <= end
        ):
            seen.add(selections[selection_cursor].algorithm)
            selection_cursor += 1
        # A cache get is positive evidence of using its exact older resource;
        # mere creation of a different resource cannot override that identity.
        candidates = [value for value, _event in used] if used else created
        source = "engine-ctx-reuse" if used else "engine-ctx"
        reason = ""
        if uncertain and not used:
            candidates = []
            reason = "unrecognized executor context prevents implicit attribution"
        elif missing or lifecycle:
            candidates = []
            reason = "context reuse is missing a creation or crosses destruction"
        elif not candidates:
            source = "engine-ctx-inherited"
            candidates = list(active.values())
            if requested is None or requests != {requested}:
                candidates = []
                reason = "implicit context inheritance crosses requested configurations"
            elif removed:
                candidates = []
                reason = "implicit context inheritance follows context destruction"
            elif seen - {value["executor"] for value in candidates}:
                candidates = []
                reason = "selector evidence conflicts with the available contexts"
        executors = {value["executor"] for value in candidates}
        confirmed = len(executors) == 1
        if not confirmed and not reason:
            reason = "missing or ambiguous compatible executor contexts"
        proof = {
            "source": source,
            "executor": next(iter(executors)) if confirmed else None,
            "contexts": [
                {
                    "tag": value["tag"],
                    "executor": value["executor"],
                    "created_at": str(value["timestamp"]),
                    "file": value["file"],
                    "line": value["line"],
                    "origin": list(value["origin"]) if value["origin"] else None,
                }
                for value in candidates
            ],
            "uses": [
                {"tag": event["tag"], "file": event["file"], "line": event["line"]}
                for _value, event in used
            ],
            "error": reason,
        }
        entry = entries[key]
        entry["context_evidence"] = proof
        if confirmed:
            for value in candidates:
                entry["files"].add(value["file"])
                origin = entries.get(value["origin"])
                if origin is not None:
                    entry["mappings"].update(origin["mappings"])
                    if plans.get(value["origin"][0], {}).get("algorithm") == (
                        plan or {}
                    ).get("algorithm"):
                        entry["accepted"].update(origin["accepted"])
                    entry["files"].update(origin["files"])
            entry["files"].update(event["file"] for _value, event in used)
