# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Selected-executor parsing copied from hccl_api_test 44e6b84cb336844ab83bd103edc45ece774e89e0.

Original files: scripts/analyze_hccl_api_profiling.py and
scripts/analyze_hccl_ccu_profiling.py. The copied pure parsing/aggregation
functions are unchanged except that common.* helpers resolve locally.
Native entry and rank attribution lives in algorithm_evidence.py.
"""

from __future__ import annotations
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
import re
import time
from typing import Any, Mapping, Sequence

from .api_core import canonical_operator, log_message

SOURCE_COMMIT = "44e6b84cb336844ab83bd103edc45ece774e89e0"


PLOG_PID_RE = re.compile(r"^plog-(\d+)_.*\.log$")

RUN_LOG_TIMESTAMP_RE = re.compile(
    r"(?P<date>\d{4}-\d{2}-\d{2})-"
    r"(?P<clock>\d{2}:\d{2}:\d{2})\."
    r"(?P<millis>\d{3})\.(?P<micros>\d{3})"
)

API_ALGORITHM_LOG_RE = re.compile(
    r"\[MC2_ALG_INFO\]\s+"
    r"rank\[(?P<rank>\d+)\],\s+"
    r"group\[(?P<group>[^\]]*)\],\s+"
    r"opType\[(?P<operator>"
    r"AllGather|AllReduce|ReduceScatter|AllToAllV|AllToAll"
    r")\]\(\d+\),\s+"
    r"algName\[(?P<algorithm>[^\]]+)\],\s+"
    r"srcDataType\[(?P<src_dtype>[^\]]+)\]\(\d+\)",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class ApiAlgorithmLogRecord:
    process_id: int
    timestamp: Decimal
    line_number: int
    rank: int
    group: str
    operator: str
    algorithm: str
    src_dtype: str
    log_path: Path


def parse_run_log_timestamp(line: str) -> Decimal | None:
    match = RUN_LOG_TIMESTAMP_RE.search(line)
    if match is None:
        return None
    try:
        parsed = time.strptime(
            f"{match.group('date')} {match.group('clock')}",
            "%Y-%m-%d %H:%M:%S",
        )
        epoch_seconds = int(time.mktime(parsed))
        subsecond_us = int(match.group("millis")) * 1000 + int(match.group("micros"))
    except (OverflowError, ValueError):
        return None
    return Decimal(epoch_seconds * 1_000_000 + subsecond_us)


def resolve_summary_algorithms(
    observations: Mapping[tuple[Any, ...], Mapping[int, str | None]],
    ranks: int,
) -> dict[tuple[Any, ...], str]:
    resolved: dict[tuple[Any, ...], str] = {}
    for key, by_device in observations.items():
        operator, dtype, path_type, data_size, _ = key
        algorithms = {
            algorithm for algorithm in by_device.values() if algorithm is not None
        }
        all_devices_present = len(by_device) == ranks and all(
            algorithm is not None for algorithm in by_device.values()
        )
        if all_devices_present and len(algorithms) == 1:
            resolved[key] = next(iter(algorithms))
            continue
        log_message(
            "WARNING",
            f"{path_type} algorithm summary "
            f"operator={operator}, dtype={dtype}, data_size={data_size}: "
            f"rank values={dict(sorted(by_device.items()))}; "
            "summary cell is left blank",
        )
    return resolved


CCU_KFC_ALGORITHM_LOG_RE = re.compile(
    r"\[KFC\]\[(?P<operator>"
    r"AllGather|AllReduce|ReduceScatter|AllToAllV|AllToAll"
    r")\]\[Select\]\s+algorithm\s*\[(?P<algorithm>"
    r"(?:Ccu|Ins|Aiv)[A-Za-z0-9_]+)\]"
    r"(?:,\s*dataSize\s*\[(?P<data_size>\d+)\])?",
    re.I,
)

CCU_TILING_ALGORITHM_LOG_RE = re.compile(
    r"\[CcuSelectAlg\]\s+ccTiling\[\d+\]:\s*opType\[(?P<op_type>\d+)\],\s*"
    r"algName\[(?P<algorithm>[^\]]+)\],\s*algorithmType\[\d+\]",
    re.I,
)

CCU_BASE_SELECTOR_ALGORITHM_LOG_RE = re.compile(
    r"\[asc\]\[AlgoSelect\]\[AutoSelectorBase::Select\]\s+"
    r"CCU_(?:MS|SCHED)\s+matched,\s*opType\[(?P<op_type>\d+)\],\s*"
    r"algName\[(?P<algorithm>(?:Ccu|Ins|Aiv)[A-Za-z0-9_]+)\]",
    re.I,
)

CCU_SELECTOR_ALGORITHM_LOG_RE = re.compile(
    r"\[(?P<selector>[A-Za-z0-9_]+(?:Auto)?Selector)\]"
    # Skip optional bracketed segments such as the __func__ tag
    # (e.g. [SelectCcuScheduleAlgo]) so their own "select" text does
    # not trigger the match keyword prematurely; \s* before the keyword
    # keeps the \b boundary on the keyword itself.
    r"(?:\[[^\]]*\])*?"
    r"\s*(?:\bAlgo\s+match|\bselect(?:ed)?\b|\balgorithm)"
    r"\s*(?:is|=|:)?\s*\[?(?P<algorithm>"
    r"(?:Ccu|Ins|Aiv)[A-Za-z0-9_]+)\]?",
    re.I,
)

CCU_GRAPH_MODE_ALGORITHM_LOG_RE = re.compile(
    r"\[HcclSelectAlgGraphMode\]\s+Success\."
    r".*?\balgName\s*(?:=|\[)\s*(?P<algorithm>"
    r"(?:Ccu|Ins|Aiv)[A-Za-z0-9_]+)",
    re.I,
)

CCU_RESOURCE_LOG_RE = re.compile(
    r"\[HcclGetAlgRes\]\s+engine\[(?P<engine>[^\]]+)\],\s+"
    r"algTag\[(?P<tag>[^\]]+)\]",
    re.I,
)

CCU_ALGORITHM_TAG_RE = re.compile(
    r"^(?P<operator>"
    r"AllGather|AllReduce|ReduceScatter|AllToAllV|AllToAll"
    r")_(?P<context>.*)_(?P<algorithm>(?:Ccu|Ins|Aiv)[A-Za-z0-9_]+)_"
    r"(?:device|host)$",
    re.I,
)

CCU_ENGINE_CTX_ALGORITHM_LOG_RE = re.compile(
    r"HcclEngineCtxCreate\s+success.*?"
    r"ctxTag\[[^\]]*?"
    # KFC/scheduler executors insert their execution-family name between
    # ``Ccu`` and the collective, for example
    # ``CcuKfcAllGatherMesh1DMem2Mem``.
    r"(?P<algorithm>(?:Ccu(?:Kfc|Sched|Half)?|Ins|Aiv)"
    r"(?:AllGather|AllReduce|ReduceScatter|AllToAllV|All2AllV|AllToAll)"
    r"[A-Za-z0-9]*)",
    re.I,
)

OPERATOR_TEXT_PATTERNS = (
    (re.compile(r"all[_ ]?to[_ ]?all[_ ]?v[_ ]?write", re.I), "AllToAllVWrite"),
    (re.compile(r"all[_ ]?to[_ ]?all[_ ]?v", re.I), "AllToAllV"),
    (re.compile(r"reduce[_ ]?scatter", re.I), "ReduceScatter"),
    (re.compile(r"all[_ ]?gather", re.I), "AllGather"),
    (re.compile(r"all[_ ]?reduce", re.I), "AllReduce"),
    (re.compile(r"all[_ ]?to[_ ]?all", re.I), "AllToAll"),
)

CCU_OP_TYPE_TO_OPERATOR = {
    2: "AllReduce",
    6: "AllGather",
    7: "ReduceScatter",
    8: "AllToAllV",
    10: "AllToAll",
    20: "AllToAllVWrite",
}


@dataclass(frozen=True)
class CcuAlgorithmLogRecord:
    """A selected CCU executor recovered from a single PLOG line."""

    process_id: int
    timestamp: Decimal | None
    line_number: int
    operator: str | None
    algorithm: str
    data_size: int | None
    source: str
    log_path: Path


def operator_from_text(text: str) -> str | None:
    for pattern, operator in OPERATOR_TEXT_PATTERNS:
        if pattern.search(text):
            return operator
    return None


def operator_from_selector(selector: str) -> str | None:
    """Translate an `*AutoSelector` name to the common operator spelling."""
    stem = re.sub(r"(?:Auto)?Selector$", "", selector, flags=re.I)
    return canonical_operator(stem)


def ccu_algorithm_record_from_log_line(
    process_id: int,
    line_number: int,
    line: str,
    log_path: Path,
) -> CcuAlgorithmLogRecord | None:
    """Extract one authoritative CCU executor record from a PLOG line."""
    timestamp = parse_run_log_timestamp(line)
    tiling_match = CCU_TILING_ALGORITHM_LOG_RE.search(line)
    if tiling_match is not None:
        operator = CCU_OP_TYPE_TO_OPERATOR.get(int(tiling_match.group("op_type")))
        if operator is not None:
            return CcuAlgorithmLogRecord(
                process_id=process_id,
                timestamp=timestamp,
                line_number=line_number,
                operator=operator,
                algorithm=tiling_match.group("algorithm"),
                data_size=None,
                source="mc2-tiling",
                log_path=log_path,
            )
    kfc_match = CCU_KFC_ALGORITHM_LOG_RE.search(line)
    if kfc_match is not None:
        operator = canonical_operator(kfc_match.group("operator"))
        if operator is not None:
            data_size_text = kfc_match.group("data_size")
            return CcuAlgorithmLogRecord(
                process_id=process_id,
                timestamp=timestamp,
                line_number=line_number,
                operator=operator,
                algorithm=kfc_match.group("algorithm"),
                data_size=int(data_size_text) if data_size_text else None,
                source="kfc",
                log_path=log_path,
            )
    base_selector_match = CCU_BASE_SELECTOR_ALGORITHM_LOG_RE.search(line)
    if base_selector_match is not None:
        operator = CCU_OP_TYPE_TO_OPERATOR.get(
            int(base_selector_match.group("op_type"))
        )
        if operator is not None:
            return CcuAlgorithmLogRecord(
                process_id=process_id,
                timestamp=timestamp,
                line_number=line_number,
                operator=operator,
                algorithm=base_selector_match.group("algorithm"),
                data_size=None,
                source="base-selector",
                log_path=log_path,
            )
    graph_mode_match = CCU_GRAPH_MODE_ALGORITHM_LOG_RE.search(line)
    if graph_mode_match is not None:
        return CcuAlgorithmLogRecord(
            process_id=process_id,
            timestamp=timestamp,
            line_number=line_number,
            operator=None,
            algorithm=graph_mode_match.group("algorithm"),
            data_size=None,
            source="graph-mode",
            log_path=log_path,
        )
    resource_match = CCU_RESOURCE_LOG_RE.search(line)
    if resource_match is not None and "ccu" in resource_match.group("engine").lower():
        tag_match = CCU_ALGORITHM_TAG_RE.fullmatch(resource_match.group("tag"))
        if tag_match is not None:
            operator = canonical_operator(tag_match.group("operator"))
            if operator is not None:
                return CcuAlgorithmLogRecord(
                    process_id=process_id,
                    timestamp=timestamp,
                    line_number=line_number,
                    operator=operator,
                    algorithm=tag_match.group("algorithm"),
                    data_size=None,
                    source="resource",
                    log_path=log_path,
                )
    selector_match = CCU_SELECTOR_ALGORITHM_LOG_RE.search(line)
    if selector_match is not None:
        operator = operator_from_selector(selector_match.group("selector"))
        if operator is not None:
            return CcuAlgorithmLogRecord(
                process_id=process_id,
                timestamp=timestamp,
                line_number=line_number,
                operator=operator,
                algorithm=selector_match.group("algorithm"),
                data_size=None,
                source="selector",
                log_path=log_path,
            )
    engine_ctx_match = CCU_ENGINE_CTX_ALGORITHM_LOG_RE.search(line)
    if engine_ctx_match is not None:
        algorithm = engine_ctx_match.group("algorithm")
        operator = None
        for op in (
            "AllGather",
            "AllReduce",
            "ReduceScatter",
            "AllToAllVWrite",
            "AllToAllV",
            "AllToAll",
        ):
            if op in algorithm:
                operator = op
                break
        return CcuAlgorithmLogRecord(
            process_id=process_id,
            timestamp=timestamp,
            line_number=line_number,
            operator=operator,
            algorithm=algorithm,
            data_size=None,
            source="engine-ctx",
            log_path=log_path,
        )
    return None


def infer_ordered_ccu_algorithms(
    records: Sequence[CcuAlgorithmLogRecord],
    sizes: Sequence[int],
    calls_per_size: int,
    align_candidates: Sequence[tuple[int, int]] = (),
) -> dict[int, str | None]:
    """Map one ordered selector-log source to benchmark data-size groups.
    A selector may log once per size, once per collective, or more than once
    per collective through nested selector calls.  In every supported form,
    records for a size are contiguous and must agree on the selected executor.

    ``align_candidates`` lists ``(records_per_size, align_index)`` recovery
    shapes for the benchmark's device-sync align op (``-s device``): the align
    op is the same operator at a tiny data size behind a repeatable executor,
    so it contributes exactly one extra build-time selector record per run.
    With one build per case (R=1) that record lands right after case 1
    (index 1); with per-iteration builds (R=calls_per_size) it lands after
    case 1's warmup records (index warmup_iters).  When the strict partition
    fails and the record count is exactly ``len(sizes) * R + 1``, the align
    record is dropped and the partition retried.  The recovery is accepted
    only when every size then agrees on one executor, so it can never turn a
    disagreement into a wrong attribution.
    """

    def strict_partition(
        source: Sequence[CcuAlgorithmLogRecord],
    ) -> dict[int, str | None] | None:
        if not source or not sizes or len(source) % len(sizes) != 0:
            return None
        source_per_size = len(source) // len(sizes)
        if source_per_size != 1 and (
            source_per_size < calls_per_size or source_per_size % calls_per_size != 0
        ):
            return None
        inferred: dict[int, str | None] = {}
        for index, data_size in enumerate(sizes):
            begin = index * source_per_size
            algorithms = {
                record.algorithm for record in source[begin : begin + source_per_size]
            }
            inferred[data_size] = (
                next(iter(algorithms)) if len(algorithms) == 1 else None
            )
        return inferred

    strict = strict_partition(records)
    if strict is not None:
        return strict
    for records_per_size, align_index in align_candidates:
        expected = len(sizes) * records_per_size + 1
        if len(records) != expected:
            continue
        if not 0 <= align_index < len(records):
            continue
        trimmed = list(records[:align_index]) + list(records[align_index + 1 :])
        recovered = strict_partition(trimmed)
        if recovered is not None and all(recovered.values()):
            log_message(
                "INFO",
                "dropped one align-op selector record at index "
                f"{align_index} (records_per_size={records_per_size}); "
                "recovered ordered CCU algorithm inference",
            )
            return recovered
    return {}
