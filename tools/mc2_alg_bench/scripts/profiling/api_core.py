#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""API-only extraction from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7."""

from __future__ import annotations
import bisect
import datetime
import itertools
import json
import math
import os
import re
import statistics
import struct
import time
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


def resolved_sync_mode(profile: Any) -> str:
    """Return the effective measured-loop sync mode for one profile.

    Non-api profiles (including hccl baselines) always resolve to "mpi" —
    an explicit ``--sync device`` never crosses over. The explicit value is
    carried on the profile (``profile.sync_mode``, set in run_analysis) so
    it survives the pickle trip into forkserver parse workers. "auto"
    (default) resolves an api profile from the nearest benchmark
    case_status.txt ``sync=`` marker.
    """
    if getattr(profile, "path_type", "") != "api":
        return "mpi"
    explicit = getattr(profile, "sync_mode", "auto")
    if explicit in ("device", "mpi"):
        return explicit
    anchor = getattr(profile, "raw_profile_root", None) or getattr(
        profile, "json_path", None
    )
    if anchor is None:
        return "mpi"
    current = lexical_absolute(anchor if anchor.is_dir() else anchor.parent)
    while True:
        marker = current / BENCHMARK_CASE_STATUS_FILENAME
        if marker.is_file():
            try:
                for line in marker.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines():
                    key, separator, value = line.partition("=")
                    if separator and key.strip() == "sync":
                        value = value.strip()
                        return value if value in ("device", "mpi") else "mpi"
            except OSError:
                return "mpi"
            return "mpi"
        if current == current.parent:
            return "mpi"
        current = current.parent


def _drop_align_entries(
    entries: Sequence[Any],
    sizes: Sequence[int],
    warmup_iters: int,
    iters: int,
    calls_per_size: int,
) -> list[Any]:
    """Drop align-op entries from the per-size [warmup..][align,measured]* layout.

    Device-sync runs submit one same-operator tiny align op before each
    measured iteration; it is indistinguishable by name, so entries are paired
    by position (align first) and the align entries dropped, restoring the
    no-align expected-count layout.
    """
    per_size = calls_per_size + iters
    kept: list[Any] = []
    for size_index in range(len(sizes)):
        block = entries[size_index * per_size : (size_index + 1) * per_size]
        kept.extend(block[:warmup_iters])
        kept.extend(block[warmup_iters + 1 : warmup_iters + 1 + 2 * iters : 2])
    return kept


JSON_STREAM_CHUNK_CHARS = 4 * 1024 * 1024


TIME_QUANT = Decimal("0.001")


SUPPORTED_DTYPES = {
    "int8",
    "int16",
    "int32",
    "fp16",
    "fp32",
    "int64",
    "uint64",
    "uint8",
    "uint16",
    "uint32",
    "fp64",
    "bfp16",
    "bf16",
    "int128",
    "hif8",
    "fp8e4m3",
    "fp8e5m2",
    "fp8e8m0",
}


PROFILER_DTYPE_MAP = {
    "INT8": "int8",
    "INT16": "int16",
    "INT32": "int32",
    "FLOAT16": "fp16",
    "FP16": "fp16",
    "FLOAT": "fp32",
    "FLOAT32": "fp32",
    "FP32": "fp32",
    "INT64": "int64",
    "UINT64": "uint64",
    "UINT8": "uint8",
    "UINT16": "uint16",
    "UINT32": "uint32",
    "DOUBLE": "fp64",
    "FLOAT64": "fp64",
    "FP64": "fp64",
    "BF16": "bf16",
    "BFP16": "bf16",
    "BFLOAT16": "bf16",
    "INT128": "int128",
    "HIF8": "hif8",
    "FLOAT8_E4M3FN": "fp8e4m3",
    "FP8E4M3": "fp8e4m3",
    "FLOAT8_E5M2": "fp8e5m2",
    "FP8E5M2": "fp8e5m2",
    "FP8E8M0": "fp8e8m0",
}


API_KERNEL_RE = re.compile(
    r"^(AllGather|AllReduce|ReduceScatter|AllToAllV|AllToAll)"
    r"CustomV\d+Mc2AicpuKernel$"
)

NATIVE_AICPU_CLIENT_NAMES = {
    "AllReduce": ("AllReduceCustomV3", "all_reduce_custom_v3"),
    "AllGather": ("AllGatherCustomV3", "all_gather_custom_v3"),
    "ReduceScatter": ("ReduceScatterCustomV3", "reduce_scatter_custom_v3"),
    "AllToAll": ("AllToAllCustomV3", "all_to_all_custom_v3"),
    "AllToAllV": ("AllToAllVCustomV3", "all_to_all_v_custom_v3"),
}


def native_aicpu_client_operator(name: str) -> str | None:
    for operator, names in NATIVE_AICPU_CLIENT_NAMES.items():
        if name.lower() in {item.lower() for item in names}:
            return operator
    return None


HCCL_OP_MAP = {
    "ALLGATHER": "AllGather",
    "ALLREDUCE": "AllReduce",
    "REDUCESCATTER": "ReduceScatter",
    "ALLTOALLV": "AllToAllV",
    "ALLTOALLVWRITE": "AlltoAllvWrite",
    "ALLTOALL": "AllToAll",
}


HCCL_DTYPE_BY_ENUM = {
    0: "int8",
    1: "int16",
    2: "int32",
    3: "fp16",
    4: "fp32",
    5: "int64",
    6: "uint64",
    7: "uint8",
    8: "uint16",
    9: "uint32",
    10: "fp64",
    11: "bf16",
    12: "int128",
    14: "hif8",
    15: "fp8e4m3",
    16: "fp8e5m2",
    17: "fp8e8m0",
}


RAW_ADDITIONAL_RECORD_BYTES = 256


RAW_STARS_RECORD_BYTES = 32


RAW_ADDITIONAL_MAGIC = 0x5A5A


RAW_STARS_MAGIC = 0x6BD3


RAW_MAGIC_SKIP_WARNING_LIMIT = 10


RAW_AICPU_NODE_TYPE = 0
# Keep the reported value; only the native reader resolves a missing stream
# through a unique complete task identity from the exported server.
RAW_AICPU_UNKNOWN_STREAM_ID = 0xFFFF


RAW_HCCL_OP_INFO_TYPE = 10


RAW_HCCL_FLAG_TASK_TYPE = 12


RAW_STARS_TASK_START = 0


RAW_STARS_TASK_END = 1


RAW_SQE_NOTIFY_RECORD = 6


RAW_SQE_NOTIFY_WAIT = 7


RAW_SQE_UBDMA = 9


RAW_SQE_SDMA = 11


RAW_SQE_CONDITION = 20


RAW_COMMUNICATION_SQE_TYPES = {
    RAW_SQE_NOTIFY_RECORD,
    RAW_SQE_NOTIFY_WAIT,
    RAW_SQE_UBDMA,
    RAW_SQE_SDMA,
}


RAW_DEVICE_TASK_ID_HIGH_BIT = 0x80000000


RAW_CYCLES_PER_US = Decimal(1000)


BENCHMARK_CASE_STATUS_FILENAME = "case_status.txt"


DATA_TASK_RE = re.compile(
    r"(?:UBDMA|SDMA(?:_SQE)?|RDMA(?:_SQE)?|ROCE(?:_SQE)?|"
    r"PCIE(?:_DMA|_SQE)?|MEMCPY_SQE)",
    re.IGNORECASE,
)


NOTIFY_TASK_RE = re.compile(r"NOTIFY_(?:WAIT|RECORD)_SQE", re.IGNORECASE)


CONDITION_TASK_RE = re.compile(r"CONDITION(?:_WAIT|_RECORD)?_SQE", re.IGNORECASE)


class AnalysisError(RuntimeError):
    """Fatal input or structure error."""


_RUN_LOG_HANDLE: Any | None = None


_PENDING_LOG_MESSAGES: list[str] = []


_RECOVERABLE_ERROR_COUNT = 0


def log_message(level: str, message: str) -> None:
    global _RECOVERABLE_ERROR_COUNT
    timestamp = datetime.datetime.now().astimezone().isoformat(timespec="milliseconds")
    line = f"{timestamp} [{level}] {message}"
    if level == "ERROR":
        _RECOVERABLE_ERROR_COUNT += 1
    print(line, file=sys.stderr, flush=True)
    if _RUN_LOG_HANDLE is None:
        _PENDING_LOG_MESSAGES.append(line)
        return
    _RUN_LOG_HANDLE.write(line + "\n")
    _RUN_LOG_HANDLE.flush()


def print_progress(message: str) -> None:
    log_message("PROGRESS", message)


@dataclass(frozen=True)
class Event:
    name: str
    pid: int
    tid: int
    start: Decimal
    duration: Decimal
    args: Mapping[str, Any] = field(default_factory=dict)

    @property
    def end(self) -> Decimal:
        return self.start + self.duration


@dataclass(frozen=True)
class ProfileFile:
    path_type: str
    device_id: int
    json_path: Path
    op_summary_path: Path | None
    operator: str = ""
    dtype: str = ""
    profiling_level: str = "level1"
    raw_profile_root: Path | None = None
    local_device_id: int = -1
    node_key: str = ""
    execution_scope: str = ""
    # Explicit measured-loop sync mode requested via --sync ("auto" resolves
    # per profile from the case_status.txt marker). Set on every profile in
    # run_analysis before parse tasks are dispatched: forkserver workers do
    # not inherit parent runtime module state, so the explicit mode must
    # travel with the pickled task.
    sync_mode: str = "auto"


@dataclass
class Sample:
    path_type: str
    profile_path: str
    operator: str
    dtype: str
    data_size: int
    ranks: int
    device_id: int
    size_index: int
    call_index: int
    measured_iteration: int
    kernel_start: Decimal
    first_boundary_start: Decimal
    first_condition_end: Decimal | None
    first_communication_start: Decimal
    last_boundary_end: Decimal
    kernel_or_next_start: Decimal
    communication_task_count: int
    condition_count: int
    kernel_start_wall_us: Decimal | None = None
    hccl_communication_us: Decimal | None = None
    api_head_us: Decimal | None = None
    api_expand_us: Decimal | None = None
    api_communication_us: Decimal | None = None
    api_tail_us: Decimal | None = None
    api_kernel_us: Decimal | None = None
    hccl_loopgroup_us: Decimal | None = None
    api_loopgroup_us: Decimal | None = None
    api_algorithm_us: Decimal | None = None
    api_kfc_head_us: Decimal | None = None
    api_kfc_load_us: Decimal | None = None
    api_kfc_signal_us: Decimal | None = None
    api_kfc_tail_us: Decimal | None = None


@dataclass(frozen=True)
class StartSkewSummary:
    node_local_p95_us: float
    node_local_max_us: float
    cross_node_residual_p95_us: float | None = None
    cross_node_residual_max_us: float | None = None


@dataclass
class Audit:
    path_type: str
    operator: str
    dtype: str
    device_id: int
    profile_path: str
    kernel_count: int
    expected_kernel_count: int
    valid_samples: int
    expected_samples: int
    anomaly_count: int
    status: str


PHASE_FIELDS = (
    "hccl_communication_us",
    "api_head_us",
    "api_expand_us",
    "api_communication_us",
    "api_tail_us",
    "api_kernel_us",
    "hccl_loopgroup_us",
    "api_loopgroup_us",
    "api_algorithm_us",
    "api_kfc_head_us",
    "api_kfc_load_us",
    "api_kfc_signal_us",
    "api_kfc_tail_us",
)


@dataclass
class MetricAccumulator:
    sample_count: int = 0
    sums: dict[str, Decimal] = field(default_factory=lambda: defaultdict(Decimal))
    counts: dict[str, int] = field(default_factory=lambda: defaultdict(int))

    def add(self, sample: Sample) -> None:
        self.sample_count += 1
        for name in PHASE_FIELDS:
            value = getattr(sample, name)
            if value is None:
                continue
            self.sums[name] += quantize_time(value)
            self.counts[name] += 1

    def mean(self, name: str) -> float | None:
        count = self.counts.get(name, 0)
        if count == 0:
            return None
        return float(self.sums[name] / count)


@dataclass
class ParsedTrace:
    events: list[Event]
    event_starts: list[Decimal]
    process_names: dict[int, str]
    hardware_pids: set[int]
    complete_event_count: int


@dataclass(frozen=True)
class ProfileParseTask:
    profile_index: int
    profile: ProfileFile
    sizes: tuple[int, ...]
    warmup_iters: int
    iters: int
    ranks: int
    hccl_has_check_call: bool = True


@dataclass(frozen=True)
class RawStarsEvent:
    task_key: int
    sqe_type: int
    start: Decimal
    end: Decimal


@dataclass(frozen=True)
class RawAicpuNode:
    stream_id: int
    task_id: int
    start: Decimal
    end: Decimal


@dataclass(frozen=True)
class RawHcclOpInfo:
    dtype: str
    task_id: int
    timestamp: int


@dataclass(frozen=True)
class RawHcclFlagTask:
    aicpu_task_key: int
    communication_task_key: int
    flag_type: int
    timestamp: int


@dataclass
class ProfileParseResult:
    profile_index: int
    profile: ProfileFile
    samples: list[Sample]
    kernel_windows: dict[int, tuple[Decimal, Decimal]]
    audit: Audit
    retained_event_count: int
    complete_event_count: int
    load_seconds: float
    phase_parse_seconds: float
    elapsed_seconds: float
    diagnostics: list[str] = field(default_factory=list)


def decimal_value(value: Any) -> Decimal:
    if isinstance(value, Decimal):
        return value
    if isinstance(value, str):
        return Decimal(value.strip())
    return Decimal(str(value))


def quantize_time(value: Decimal) -> Decimal:
    return value.quantize(TIME_QUANT, rounding=ROUND_HALF_UP)


def normalize_dtype(value: str) -> str | None:
    normalized = value.strip().replace(" ", "").upper()
    if not normalized or normalized == "N/A":
        return None
    if normalized.startswith("DT_"):
        normalized = normalized[3:]
    if normalized.lower() in SUPPORTED_DTYPES:
        return normalized.lower()
    return PROFILER_DTYPE_MAP.get(normalized)


def canonical_operator(value: str) -> str | None:
    return HCCL_OP_MAP.get(value.replace("_", "").upper())


def iter_json_array(path: Path) -> Iterable[Any]:
    """Stream one value at a time from a top-level JSON array."""

    decoder = json.JSONDecoder(parse_float=Decimal)
    try:
        handle = path.open("r", encoding="utf-8")
    except OSError as exc:
        raise AnalysisError(f"failed to open profiling JSON {path}: {exc}") from exc

    with handle:
        buffer = ""
        cursor = 0
        eof = False
        item_index = 0

        def read_more() -> bool:
            nonlocal buffer, cursor, eof
            if eof:
                return False
            chunk = handle.read(JSON_STREAM_CHUNK_CHARS)
            buffer = buffer[cursor:] + chunk
            cursor = 0
            if not chunk:
                eof = True
                return False
            return True

        def skip_whitespace() -> None:
            nonlocal cursor
            while True:
                while cursor < len(buffer) and buffer[cursor].isspace():
                    cursor += 1
                if cursor < len(buffer) or eof:
                    return
                read_more()

        if not read_more():
            raise AnalysisError(f"profiling JSON is empty: {path}")
        skip_whitespace()
        if cursor >= len(buffer) or buffer[cursor] != "[":
            raise AnalysisError(f"profiling JSON root must be an array: {path}")
        cursor += 1
        first_item = True

        while True:
            skip_whitespace()
            if cursor >= len(buffer):
                raise AnalysisError(f"unterminated profiling JSON array: {path}")
            if buffer[cursor] == "]":
                cursor += 1
                break

            if not first_item:
                if buffer[cursor] != ",":
                    raise AnalysisError(
                        f"invalid profiling JSON array delimiter before item "
                        f"{item_index + 1}: {path}"
                    )
                cursor += 1
                skip_whitespace()
                if cursor >= len(buffer):
                    raise AnalysisError(f"unterminated profiling JSON array: {path}")

            while True:
                try:
                    value, end = decoder.raw_decode(buffer, cursor)
                except json.JSONDecodeError as exc:
                    if eof:
                        raise AnalysisError(
                            f"failed to decode profiling JSON item "
                            f"{item_index + 1} in {path}: {exc}"
                        ) from exc
                    if not read_more() and cursor >= len(buffer):
                        raise AnalysisError(
                            f"unterminated profiling JSON item {item_index + 1}: {path}"
                        ) from exc
                    continue
                cursor = end
                item_index += 1
                first_item = False
                yield value
                break

        skip_whitespace()
        while not eof:
            read_more()
            skip_whitespace()
        if cursor < len(buffer):
            raise AnalysisError(f"trailing data after profiling JSON array: {path}")


def raw_event_task_type(raw: Mapping[str, Any]) -> str:
    args = raw.get("args")
    if isinstance(args, Mapping):
        for key in ("Task Type", "task type"):
            if key in args:
                return str(args[key])
    return str(raw.get("name", ""))


def should_retain_complete_event(raw: Mapping[str, Any]) -> bool:
    name = str(raw.get("name", ""))
    if name == "HcclLaunchAicpuKernel" or API_KERNEL_RE.fullmatch(name):
        return True
    task_type = raw_event_task_type(raw)
    return bool(
        DATA_TASK_RE.search(name)
        or DATA_TASK_RE.search(task_type)
        or NOTIFY_TASK_RE.search(name)
        or NOTIFY_TASK_RE.search(task_type)
        or CONDITION_TASK_RE.search(name)
        or CONDITION_TASK_RE.search(task_type)
    )


def load_trace(path: Path, retain_event=None) -> ParsedTrace:
    process_names: dict[int, str] = {}
    events: list[Event] = []
    complete_event_count = 0
    try:
        raw_events = iter_json_array(path)
        for raw in raw_events:
            if not isinstance(raw, dict):
                continue
            if raw.get("ph") == "M" and raw.get("name") == "process_name":
                args = raw.get("args") or {}
                if isinstance(args, dict) and "name" in args:
                    process_names[int(raw.get("pid", 0))] = str(args["name"])
                continue
            if raw.get("ph") != "X" or "ts" not in raw or "dur" not in raw:
                continue
            complete_event_count += 1
            if not (retain_event or should_retain_complete_event)(raw):
                continue
            try:
                event = Event(
                    name=str(raw.get("name", "")),
                    pid=int(raw.get("pid", 0)),
                    tid=int(raw.get("tid", 0)),
                    start=decimal_value(raw["ts"]),
                    duration=decimal_value(raw["dur"]),
                    args=raw.get("args") if isinstance(raw.get("args"), dict) else {},
                )
            except (TypeError, ValueError, InvalidOperation):
                continue
            if event.duration < 0:
                continue
            events.append(event)
    except OSError as exc:
        raise AnalysisError(f"failed to read profiling JSON {path}: {exc}") from exc

    if not events:
        raise AnalysisError(
            f"profiling JSON contains no relevant complete events: {path}"
        )
    events.sort(key=lambda event: (event.start, event.end, event.pid, event.tid))
    event_starts = [event.start for event in events]
    hardware_pids = {
        pid
        for pid, name in process_names.items()
        if name.strip().lower() == "ascend hardware"
    }
    return ParsedTrace(
        events=events,
        event_starts=event_starts,
        process_names=process_names,
        hardware_pids=hardware_pids,
        complete_event_count=complete_event_count,
    )


def event_task_type(event: Event) -> str:
    for key in ("Task Type", "task type"):
        if key in event.args:
            return str(event.args[key])
    return event.name


def is_condition_event(event: Event) -> bool:
    return bool(
        CONDITION_TASK_RE.search(event.name)
        or CONDITION_TASK_RE.search(event_task_type(event))
    )


def is_data_event(event: Event) -> bool:
    return bool(
        DATA_TASK_RE.search(event.name) or DATA_TASK_RE.search(event_task_type(event))
    )


def is_communication_event(event: Event) -> bool:
    task = event_task_type(event)
    return bool(
        is_data_event(event)
        or NOTIFY_TASK_RE.search(event.name)
        or NOTIFY_TASK_RE.search(task)
        or is_condition_event(event)
    )


def events_in_window(
    trace: ParsedTrace,
    start: Decimal,
    end: Decimal,
    allowed_pids: set[int] | None = None,
) -> list[Event]:
    left = bisect.bisect_left(trace.event_starts, start)
    right = bisect.bisect_left(trace.event_starts, end, left)
    return [
        event
        for event in trace.events[left:right]
        if allowed_pids is None or event.pid in allowed_pids
    ]


def relative_display_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(Path.cwd().resolve()))
    except ValueError:
        return str(resolved)


def lexical_absolute(path: Path) -> Path:
    """Return an absolute path without resolving input-view symlinks."""
    return Path(os.path.abspath(path))


def slice_series_and_index(path: Path) -> tuple[str, int, int]:
    """Return (series stem, numeric series id, numeric slice index)."""
    match = re.search(r"\.slice_(\d+)$", path.name)
    if match:
        stem = path.name[: match.start()]
        series = re.search(r"(\d+)$", stem)
        return (
            stem,
            int(series.group(1)) if series else -1,
            int(match.group(1)),
        )
    return path.name, -1, -1


def slice_path_sort_key(path: Path) -> tuple[str, int, str, int]:
    stem, series, index = slice_series_and_index(path)
    # The stem must be part of the key so that paths sharing a series id
    # (e.g. "aicpu.data.0" and "aicpu.data.backup0") stay contiguous per
    # stem; iter_raw_additional_records groups on (stem, series).
    return (str(path.parent), series, stem, index)


def raw_data_slices(device_dir: Path, prefix: str) -> list[Path]:
    return sorted(
        (
            path
            for path in (device_dir / "data").glob(f"{prefix}*.slice_*")
            if not path.name.endswith(".done")
        ),
        key=slice_path_sort_key,
    )


def _record_origin(path: Path, offset: int, carry_len: int) -> str:
    """Describe where a parsed stream offset came from.

    A record that starts inside the tail carried over from the previous
    slice has no non-negative offset inside the current file, so name the
    carried tail instead of reporting a misleading negative file offset.
    """
    if carry_len and offset < carry_len:
        return (
            f"the tail carried over from the previous slice of {path} "
            f"(stream offset {offset})"
        )
    return (
        f"file offset {offset - carry_len} while parsing {path} "
        f"(stream offset {offset})"
    )


def iter_raw_additional_records(
    device_dir: Path,
) -> Iterable[tuple[int, int, int, bytes]]:
    # Slices of one series (e.g. aicpu.data.0.slice_*) are contiguous
    # fragments of a single byte stream that is cut at fixed sizes without
    # respecting 256-byte record boundaries. Parse each series as one
    # stream: an unconsumed slice tail is carried into the next slice, and
    # only the very end of the stream can hold a truncated record.
    for _, slices in itertools.groupby(
        raw_data_slices(device_dir, "aicpu.data."),
        key=lambda path: slice_series_and_index(path)[:2],
    ):
        carry = b""
        last_path = Path(device_dir)
        prev_index: int | None = None
        magic_skips = 0
        misaligned = False
        for path in slices:
            index = slice_series_and_index(path)[2]
            if prev_index is None:
                if index != 0:
                    log_message(
                        "WARNING",
                        f"raw AICPU data series of {path} starts at "
                        f"slice {index} instead of slice 0; records from "
                        "the missing earlier slices cannot be recovered "
                        "and the surviving records may be misaligned and "
                        "lack the additional-info magic",
                    )
                    misaligned = True
            elif index != prev_index + 1:
                if carry:
                    log_message(
                        "WARNING",
                        f"raw AICPU data series of {path} skipped from "
                        f"slice {prev_index} to slice {index}; dropping "
                        f"{len(carry)} bytes of carried data because the "
                        "missing slice may have changed the stream "
                        "alignment; the records that follow may also be "
                        "misaligned and lack the additional-info magic",
                    )
                    carry = b""
                else:
                    log_message(
                        "WARNING",
                        f"raw AICPU data series of {path} skipped from "
                        f"slice {prev_index} to slice {index}; records "
                        "from the missing slice cannot be recovered and "
                        "the records that follow may be misaligned and "
                        "lack the additional-info magic",
                    )
                misaligned = True
            prev_index = index
            try:
                data = carry + path.read_bytes()
            except OSError as exc:
                raise AnalysisError(
                    f"failed to read raw AICPU data {path}: {exc}"
                ) from exc
            last_path = path

            carry_len = len(carry)
            complete_size = (
                len(data) // RAW_ADDITIONAL_RECORD_BYTES * RAW_ADDITIONAL_RECORD_BYTES
            )
            for offset in range(0, complete_size, RAW_ADDITIONAL_RECORD_BYTES):
                magic, _level, record_type, _thread_id, data_len, timestamp = (
                    struct.unpack_from("<HHIIIQ", data, offset)
                )
                if magic != RAW_ADDITIONAL_MAGIC:
                    if magic_skips < RAW_MAGIC_SKIP_WARNING_LIMIT:
                        reason = (
                            "; the series has missing slices, so it is "
                            "probably misaligned rather than corrupted"
                            if misaligned
                            else ""
                        )
                        log_message(
                            "WARNING",
                            f"raw AICPU record at {_record_origin(path, offset, carry_len)} "
                            f"lacks the additional-info magic{reason}; "
                            "skipping it",
                        )
                    magic_skips += 1
                    continue
                if data_len > RAW_ADDITIONAL_RECORD_BYTES - 24:
                    raise AnalysisError(
                        f"invalid additional-info dataLen {data_len} at "
                        f"{_record_origin(path, offset, carry_len)}"
                    )
                payload = data[offset + 24 : offset + 24 + data_len]
                yield record_type, data_len, timestamp, payload
            carry = data[complete_size:]

        if magic_skips > RAW_MAGIC_SKIP_WARNING_LIMIT:
            summary = (
                f"raw AICPU data series of {last_path} had "
                f"{magic_skips - RAW_MAGIC_SKIP_WARNING_LIMIT} further "
                "records lacking the additional-info magic that were "
                "skipped without individual warnings"
            )
            if misaligned:
                summary += (
                    "; the series has missing slices, so they are "
                    "probably misaligned rather than corrupted"
                )
            log_message("WARNING", summary)

        if not carry:
            continue
        context = last_path
        if len(carry) < 24:
            log_message(
                "WARNING",
                f"raw AICPU data stream of {context} ends with "
                f"{len(carry)} bytes, shorter than an additional-info "
                "header; ignoring them",
            )
            continue

        magic, _level, record_type, _thread_id, data_len, timestamp = (
            struct.unpack_from("<HHIIIQ", carry, 0)
        )
        if magic != RAW_ADDITIONAL_MAGIC or data_len > len(carry) - 24:
            log_message(
                "WARNING",
                f"raw AICPU data stream of {context} ends with "
                f"{len(carry)} bytes that do not hold a complete "
                "additional-info record; ignoring them",
            )
            continue

        payload = carry[24 : 24 + data_len]
        log_message(
            "WARNING",
            f"raw AICPU data stream of {context} ends with a truncated "
            f"{RAW_ADDITIONAL_RECORD_BYTES}-byte record; accepting its "
            f"complete {data_len}-byte payload and ignoring "
            f"{RAW_ADDITIONAL_RECORD_BYTES - len(carry)} bytes of padding",
        )
        yield record_type, data_len, timestamp, payload


def raw_profile_device_dir(profile_root: Path) -> tuple[Path, int]:
    device_dirs = sorted(
        path
        for path in profile_root.iterdir()
        if path.is_dir() and re.fullmatch(r"device_\d+", path.name)
    )
    if len(device_dirs) != 1:
        raise AnalysisError(
            f"expected one device_N directory under {profile_root}, found "
            f"{[path.name for path in device_dirs]}"
        )
    device_id = int(device_dirs[0].name.split("_", 1)[1])
    return device_dirs[0], device_id


def raw_profile_process_info(profile_root: Path) -> tuple[int | None, str]:
    info_path = profile_root / "host" / "info.json"
    try:
        with info_path.open("r", encoding="utf-8") as handle:
            info = json.load(handle)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise AnalysisError(
            f"failed to read raw profile metadata {info_path}: {exc}"
        ) from exc
    process_id: int | None
    try:
        process_id = int(info["pid"])
    except (KeyError, TypeError, ValueError):
        process_id = None
    return process_id, str(info.get("pid_name", ""))


def parse_api_sample(
    trace: ParsedTrace,
    kernel: Event,
    profile: ProfileFile,
    operator: str,
    dtype: str,
    data_size: int,
    size_index: int,
    call_index: int,
    measured_iteration: int,
    ranks: int,
) -> Sample:
    window = events_in_window(trace, kernel.start, kernel.end, trace.hardware_pids)
    conditions = [event for event in window if is_condition_event(event)]
    if len(conditions) < 2:
        raise AnalysisError(
            f"expected at least two CONDITION tasks, found {len(conditions)}"
        )
    conditions.sort(key=lambda event: (event.start, event.end))
    first_condition = conditions[0]
    last_condition = max(conditions[1:], key=lambda event: event.end)
    communication_streams = {event.tid for event in conditions}

    communication_events = [
        event
        for event in window
        if event.tid in communication_streams
        and not is_condition_event(event)
        and is_communication_event(event)
        and event.start >= first_condition.end
        and event.start < last_condition.end
    ]
    if not communication_events:
        raise AnalysisError("no API communication task between CONDITION tasks")
    first_communication = min(communication_events, key=lambda event: event.start)

    head = first_condition.start - kernel.start
    expand = first_communication.start - first_condition.end
    communication = last_condition.end - first_communication.start
    tail = kernel.end - last_condition.end
    kernel_duration = kernel.end - kernel.start
    if min(head, expand, communication, tail, kernel_duration) < 0:
        raise AnalysisError("invalid API phase boundaries")

    return Sample(
        path_type="api",
        profile_path=relative_display_path(profile.json_path),
        operator=operator,
        dtype=dtype,
        data_size=data_size,
        ranks=ranks,
        device_id=profile.device_id,
        size_index=size_index,
        call_index=call_index,
        measured_iteration=measured_iteration,
        kernel_start=kernel.start,
        first_boundary_start=first_condition.start,
        first_condition_end=first_condition.end,
        first_communication_start=first_communication.start,
        last_boundary_end=last_condition.end,
        kernel_or_next_start=kernel.end,
        communication_task_count=len(communication_events),
        condition_count=len(conditions),
        api_head_us=head,
        api_expand_us=expand,
        api_communication_us=communication,
        api_tail_us=tail,
        api_kernel_us=kernel_duration,
    )


def parse_raw_aicpu_records(
    device_dir: Path,
) -> tuple[
    list[RawAicpuNode],
    list[RawHcclOpInfo],
    list[RawHcclFlagTask],
]:
    nodes: list[RawAicpuNode] = []
    hccl_ops: list[RawHcclOpInfo] = []
    flags: list[RawHcclFlagTask] = []
    for record_type, data_len, timestamp, payload in iter_raw_additional_records(
        device_dir
    ):
        if record_type == RAW_AICPU_NODE_TYPE:
            if data_len < 128:
                raise AnalysisError(
                    f"AICPU node record in {device_dir} has dataLen {data_len}, "
                    "expected at least 128"
                )
            stream_id, task_id = struct.unpack_from("<HH", payload, 0)
            run_start_tick = struct.unpack_from("<Q", payload, 16)[0]
            run_end_tick = struct.unpack_from("<Q", payload, 56)[0]
            if run_start_tick == 0 or run_end_tick == 0:
                raise AnalysisError(
                    f"AICPU node {stream_id}/{task_id} in {device_dir} lacks "
                    "Device counter timestamps"
                )
            if run_end_tick < run_start_tick:
                raise AnalysisError(
                    f"AICPU node {stream_id}/{task_id} in {device_dir} has "
                    f"runEndTick {run_end_tick} before runStartTick "
                    f"{run_start_tick}"
                )
            # Match STARS sysCount in the Device CNTVCT time domain. The
            # adjacent runStartTime/runEndTime fields are derived with
            # ``cntvct * 1000000ULL / cntfrq`` in Runtime and their absolute
            # values can wrap when the uint64 multiplication overflows.
            run_start = Decimal(run_start_tick) / RAW_CYCLES_PER_US
            run_end = Decimal(run_end_tick) / RAW_CYCLES_PER_US
            nodes.append(
                RawAicpuNode(
                    stream_id=stream_id,
                    task_id=task_id,
                    start=run_start,
                    end=run_end,
                )
            )
            continue
        if record_type == RAW_HCCL_OP_INFO_TYPE:
            if data_len < 36:
                raise AnalysisError(
                    f"HCCL OpInfo in {device_dir} has dataLen {data_len}, expected 36"
                )
            dtype_enum = payload[1]
            dtype = HCCL_DTYPE_BY_ENUM.get(dtype_enum)
            if dtype is None:
                raise AnalysisError(
                    f"unsupported hcom dataType enum {dtype_enum} in {device_dir}"
                )
            task_id = struct.unpack_from("<I", payload, 32)[0]
            hccl_ops.append(
                RawHcclOpInfo(
                    dtype=dtype,
                    task_id=task_id,
                    timestamp=timestamp,
                )
            )
            continue
        if record_type == RAW_HCCL_FLAG_TASK_TYPE:
            if data_len < 16:
                raise AnalysisError(
                    f"HCCL flag task in {device_dir} has dataLen {data_len}, "
                    "expected 16"
                )
            (
                aicpu_task_low,
                aicpu_task_high,
                communication_task_low,
                communication_task_high,
                flag_type,
                _reserve0,
                _reserve1,
                _reserve2,
            ) = struct.unpack_from("<8H", payload, 0)
            flags.append(
                RawHcclFlagTask(
                    aicpu_task_key=((aicpu_task_high << 16) | aicpu_task_low),
                    communication_task_key=(
                        (communication_task_high << 16) | communication_task_low
                    ),
                    flag_type=flag_type,
                    timestamp=timestamp,
                )
            )
    nodes.sort(key=lambda node: (node.start, node.end))
    hccl_ops.sort(key=lambda item: item.timestamp)
    flags.sort(key=lambda item: item.timestamp)
    return nodes, hccl_ops, flags


def parse_raw_stars_events(
    device_dir: Path,
) -> tuple[list[RawStarsEvent], int, list[str]]:
    pending: dict[tuple[int, int], list[Decimal]] = defaultdict(list)
    events: list[RawStarsEvent] = []
    diagnostics: list[str] = []
    complete_records = 0
    for path in raw_data_slices(device_dir, "stars_soc.data."):
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise AnalysisError(f"failed to read raw STARS data {path}: {exc}") from exc
        if len(data) % RAW_STARS_RECORD_BYTES != 0:
            raise AnalysisError(
                f"raw STARS data size {len(data)} in {path} is not aligned "
                f"to {RAW_STARS_RECORD_BYTES} bytes"
            )
        for offset in range(0, len(data), RAW_STARS_RECORD_BYTES):
            (
                first_word,
                magic,
                task_low,
                task_high,
                sys_count_low,
                sys_count_high,
            ) = struct.unpack_from("<HHHHII", data, offset)
            if magic != RAW_STARS_MAGIC:
                continue
            log_type = first_word & 0x3F
            if log_type not in {RAW_STARS_TASK_START, RAW_STARS_TASK_END}:
                continue
            complete_records += 1
            sqe_type = (first_word >> 10) & 0x3F
            task_key = (task_high << 16) | task_low
            cycle = (sys_count_high << 32) | sys_count_low
            timestamp_us = Decimal(cycle) / RAW_CYCLES_PER_US
            pending_key = (task_key, sqe_type)
            if log_type == RAW_STARS_TASK_START:
                pending[pending_key].append(timestamp_us)
                continue
            starts = pending.get(pending_key)
            if not starts:
                diagnostics.append(
                    f"STARS task {task_key:#x} sqeType={sqe_type} has an "
                    f"end record without a start record in {path}"
                )
                continue
            start = starts.pop(0)
            if timestamp_us < start:
                diagnostics.append(
                    f"STARS task {task_key:#x} sqeType={sqe_type} ends before "
                    f"it starts in {path}"
                )
                continue
            events.append(
                RawStarsEvent(
                    task_key=task_key,
                    sqe_type=sqe_type,
                    start=start,
                    end=timestamp_us,
                )
            )
    unmatched = sum(len(starts) for starts in pending.values())
    if unmatched:
        diagnostics.append(
            f"{unmatched} STARS task start records have no matching end record "
            f"under {device_dir}"
        )
    events.sort(key=lambda event: (event.start, event.end, event.task_key))
    return events, complete_records, diagnostics


def raw_device_time_to_epoch_offset_us(profile_root: Path, device_dir: Path) -> Decimal:
    start_info_paths = sorted(
        path
        for path in device_dir.glob("start_info.*")
        if not path.name.endswith(".done")
    )
    dev_start_paths = sorted(
        path
        for path in device_dir.glob("dev_start.log.*")
        if not path.name.endswith(".done")
    )
    host_start_paths = sorted(
        path
        for path in device_dir.glob("host_start.log.*")
        if not path.name.endswith(".done")
    )
    if (
        len(start_info_paths) != 1
        or len(dev_start_paths) != 1
        or len(host_start_paths) != 1
    ):
        raise AnalysisError(
            f"cannot build device-to-wall clock mapping for {profile_root}: "
            f"start_info={start_info_paths}, dev_start={dev_start_paths}, "
            f"host_start={host_start_paths}"
        )
    try:
        with start_info_paths[0].open("r", encoding="utf-8") as handle:
            start_info = json.load(handle)
        collection_begin_us = Decimal(str(start_info["collectionTimeBegin"]))
        collection_monotonic_ns = Decimal(str(start_info["clockMonotonicRaw"]))
        dev_start_text = dev_start_paths[0].read_text(
            encoding="utf-8", errors="replace"
        )
        host_start_text = host_start_paths[0].read_text(
            encoding="utf-8", errors="replace"
        )
    except (OSError, KeyError, ValueError, InvalidOperation) as exc:
        raise AnalysisError(
            f"failed to read raw clock mapping under {device_dir}: {exc}"
        ) from exc

    device_counter_match = re.search(
        r"^cntvct:\s*(\d+)\s*$",
        dev_start_text,
        re.MULTILINE,
    )
    if device_counter_match is None:
        raise AnalysisError(f"missing cntvct in {dev_start_paths[0]}")
    host_monotonic_match = re.search(
        r"^clock_monotonic_raw:\s*(\d+)\s*$",
        host_start_text,
        re.MULTILINE,
    )
    if host_monotonic_match is None:
        raise AnalysisError(f"missing clock_monotonic_raw in {host_start_paths[0]}")

    device_counter_us = Decimal(device_counter_match.group(1)) / RAW_CYCLES_PER_US
    host_anchor_monotonic_us = Decimal(host_monotonic_match.group(1)) / Decimal(1000)
    collection_monotonic_us = collection_monotonic_ns / Decimal(1000)

    # start_info pairs the collection wall clock with a Host monotonic
    # timestamp. host_start.log and dev_start.log are emitted from the same
    # GetHostAndDeviceTime sample and pair another Host monotonic timestamp
    # with the Device cntvct. Bridge both pairs before applying the offset to
    # raw AICPU/STARS timestamps, which are already expressed in Device
    # cntvct microseconds.
    return (
        collection_begin_us
        + host_anchor_monotonic_us
        - collection_monotonic_us
        - device_counter_us
    )


def raw_events_in_window(
    events: Sequence[RawStarsEvent],
    starts: Sequence[Decimal],
    begin: Decimal,
    end: Decimal,
) -> list[RawStarsEvent]:
    left = bisect.bisect_left(starts, begin)
    right = bisect.bisect_right(starts, end)
    return [
        event
        for event in events[left:right]
        if event.start >= begin and event.end <= end
    ]


def raw_api_kernel_boundaries(
    kernel: RawAicpuNode,
    stars_events: Sequence[RawStarsEvent],
    starts: Sequence[Decimal],
) -> tuple[
    RawStarsEvent,
    RawStarsEvent,
    RawStarsEvent,
    list[RawStarsEvent],
    int,
]:
    window = [
        event
        for event in raw_events_in_window(
            stars_events,
            starts,
            kernel.start,
            kernel.end,
        )
        if event.task_key & RAW_DEVICE_TASK_ID_HIGH_BIT
    ]
    conditions = sorted(
        (event for event in window if event.sqe_type == RAW_SQE_CONDITION),
        key=lambda event: (event.start, event.end, event.task_key),
    )
    if len(conditions) < 2:
        raise AnalysisError(
            f"expected at least two CONDITION tasks, found {len(conditions)}"
        )

    first_condition = conditions[0]
    last_condition = max(
        conditions[1:],
        key=lambda event: (event.end, event.start, event.task_key),
    )
    communication_events = [
        event
        for event in window
        if event.sqe_type in RAW_COMMUNICATION_SQE_TYPES
        and event.start >= first_condition.end
        and event.start < last_condition.end
    ]
    if not communication_events:
        raise AnalysisError("no UBDMA/SDMA/Notify task between CONDITION tasks")
    first_communication = min(
        communication_events,
        key=lambda event: (event.start, event.end, event.task_key),
    )

    ordered_boundaries = (
        kernel.start <= first_condition.start
        and first_condition.start <= first_condition.end
        and first_condition.end <= first_communication.start
        and first_communication.start < last_condition.end
        and last_condition.end <= kernel.end
    )
    if not ordered_boundaries:
        raise AnalysisError(
            "invalid API raw phase order: "
            f"kernel=[{kernel.start},{kernel.end}], "
            f"first_condition=[{first_condition.start},"
            f"{first_condition.end}], "
            f"first_communication={first_communication.start}, "
            f"last_condition_end={last_condition.end}"
        )
    return (
        first_condition,
        first_communication,
        last_condition,
        communication_events,
        len(conditions),
    )


def parse_raw_api_profile(
    task: ProfileParseTask,
    nodes: Sequence[RawAicpuNode],
    stars_events: Sequence[RawStarsEvent],
    complete_event_count: int,
    diagnostics: list[str],
    wall_offset_us: Decimal | None,
) -> ProfileParseResult:
    profile = task.profile
    starts = [event.start for event in stars_events]
    kernels: list[RawAicpuNode] = []
    boundary_rejection_counts: dict[str, int] = defaultdict(int)
    for node in nodes:
        try:
            raw_api_kernel_boundaries(node, stars_events, starts)
            kernels.append(node)
        except AnalysisError as exc:
            reason = str(exc)
            if reason.startswith("invalid API raw phase order:"):
                reason = "invalid API raw phase order"
            boundary_rejection_counts[reason] += 1
            continue

    calls_per_size = task.warmup_iters + task.iters
    expected_kernel_count = len(task.sizes) * calls_per_size
    expected_samples = len(task.sizes) * task.iters
    align_extra = len(task.sizes) * task.iters
    if (
        profile.path_type == "api"
        and resolved_sync_mode(profile) == "device"
        and len(kernels) == expected_kernel_count + align_extra
    ):
        # -s device：每档 warmup 前缀之后为 [对齐,被测] 交替，剔除对齐 kernel。
        # path_type 判断为防御性显式化：本函数经 parse_raw_profile_task 的
        # else 分支只应收到 api 路径，不依赖调用点分发这一隐式契约。
        kernels = _drop_align_entries(
            kernels, task.sizes, task.warmup_iters, task.iters, calls_per_size
        )
        log_message(
            "INFO",
            f"api device {profile.device_id}: align-op pairing detected, "
            f"dropped {align_extra} align kernels",
        )
    samples: list[Sample] = []
    kernel_windows: dict[int, tuple[Decimal, Decimal]] = {}
    anomaly_count = 0
    if len(kernels) == expected_kernel_count:
        for size_index, data_size in enumerate(task.sizes):
            base = size_index * calls_per_size
            first_node = kernels[base]
            last_node = kernels[base + calls_per_size - 1]
            if wall_offset_us is not None:
                kernel_windows[data_size] = (
                    first_node.start + wall_offset_us,
                    last_node.end + wall_offset_us,
                )
            for measured_index in range(task.iters):
                call_index = task.warmup_iters + measured_index
                kernel = kernels[base + call_index]
                try:
                    (
                        first_condition,
                        first_communication,
                        last_condition,
                        communication_events,
                        condition_count,
                    ) = raw_api_kernel_boundaries(
                        kernel,
                        stars_events,
                        starts,
                    )
                    head = first_condition.start - kernel.start
                    expand = first_communication.start - first_condition.end
                    communication = last_condition.end - first_communication.start
                    tail = kernel.end - last_condition.end
                    kernel_duration = kernel.end - kernel.start
                    if (
                        min(
                            head,
                            expand,
                            communication,
                            tail,
                            kernel_duration,
                        )
                        < 0
                    ):
                        raise AnalysisError("invalid API raw phase boundaries")
                    samples.append(
                        Sample(
                            path_type="api",
                            profile_path=relative_display_path(
                                profile.raw_profile_root or profile.json_path
                            ),
                            operator=profile.operator,
                            dtype=profile.dtype,
                            data_size=data_size,
                            ranks=task.ranks,
                            device_id=profile.device_id,
                            size_index=size_index,
                            call_index=call_index,
                            measured_iteration=measured_index + 1,
                            kernel_start=kernel.start,
                            first_boundary_start=first_condition.start,
                            first_condition_end=first_condition.end,
                            first_communication_start=first_communication.start,
                            last_boundary_end=last_condition.end,
                            kernel_or_next_start=kernel.end,
                            communication_task_count=len(communication_events),
                            condition_count=condition_count,
                            kernel_start_wall_us=(
                                kernel.start + wall_offset_us
                                if wall_offset_us is not None
                                else None
                            ),
                            api_head_us=head,
                            api_expand_us=expand,
                            api_communication_us=communication,
                            api_tail_us=tail,
                            api_kernel_us=kernel_duration,
                        )
                    )
                except (AnalysisError, IndexError) as exc:
                    anomaly_count += 1
                    diagnostics.append(
                        f"API device={profile.device_id}, size={data_size}, "
                        f"iteration={measured_index + 1}: {exc}"
                    )
    else:
        anomaly_count = expected_samples
        diagnostics.append(
            f"API device={profile.device_id} custom kernel count "
            f"{len(kernels)} != expected {expected_kernel_count}; all samples "
            "from this profile are skipped"
        )
        if boundary_rejection_counts:
            rejection_summary = ", ".join(
                f"{reason} ({count})"
                for reason, count in sorted(
                    boundary_rejection_counts.items(),
                    key=lambda item: (-item[1], item[0]),
                )
            )
            diagnostics.append(
                f"API device={profile.device_id} AICPU node boundary "
                f"rejections: {rejection_summary}"
            )

    status = (
        "PASS"
        if len(kernels) == expected_kernel_count
        and len(samples) == expected_samples
        and anomaly_count == 0
        else "FAIL"
    )
    audit = Audit(
        path_type="api",
        operator=profile.operator,
        dtype=profile.dtype,
        device_id=profile.device_id,
        profile_path=relative_display_path(
            profile.raw_profile_root or profile.json_path
        ),
        kernel_count=len(kernels),
        expected_kernel_count=expected_kernel_count,
        valid_samples=len(samples),
        expected_samples=expected_samples,
        anomaly_count=anomaly_count,
        status=status,
    )
    return ProfileParseResult(
        profile_index=task.profile_index,
        profile=profile,
        samples=samples,
        kernel_windows=kernel_windows,
        audit=audit,
        retained_event_count=len(stars_events),
        complete_event_count=complete_event_count,
        load_seconds=0.0,
        phase_parse_seconds=0.0,
        elapsed_seconds=0.0,
        diagnostics=diagnostics,
    )


def build_start_skews(
    observations: Mapping[
        tuple[str, str, str, str, int, int],
        Mapping[int, Mapping[str, Mapping[int, Decimal]]],
    ],
    ranks: int,
    iters: int,
) -> dict[tuple[str, str, str, int, int], StartSkewSummary]:
    """Build single-node skew or multi-node local skew and clock residuals.

    Multi-node Device timestamps are first mapped to each Host wall clock.
    A fixed Host clock offset must not be interpreted as launch skew, so the
    signed median offset for each node is learned across the complete logical
    MPI command (execution scope, path, operator and dtype).  Each iteration
    then reports both the worst within-node rank spread and the residual spread
    among node median starts after subtracting those fixed offsets.
    """

    def percentile_and_max(values: Sequence[Decimal]) -> tuple[float, float]:
        ordered = sorted(values)
        percentile_index = max(0, math.ceil(0.95 * len(ordered)) - 1)
        return (
            float(quantize_time(ordered[percentile_index])),
            float(quantize_time(ordered[-1])),
        )

    command_groups: dict[
        tuple[str, str, str, str, int],
        list[
            tuple[
                tuple[str, str, str, str, int, int],
                Mapping[int, Mapping[str, Mapping[int, Decimal]]],
            ]
        ],
    ] = defaultdict(list)
    for key, by_iteration in observations.items():
        execution_scope, operator, dtype, path_type, _data_size, key_ranks = key
        command_groups[(execution_scope, operator, dtype, path_type, key_ranks)].append(
            (key, by_iteration)
        )

    result: dict[tuple[str, str, str, int, int], StartSkewSummary] = {}
    conflicted: set[tuple[str, str, str, int, int]] = set()
    expected_all_ranks = list(range(ranks))

    for command_key, groups in command_groups.items():
        execution_scope, operator, dtype, path_type, command_ranks = command_key
        if command_ranks != ranks:
            log_message(
                "ERROR",
                f"cannot calculate {path_type} start skew for "
                f"{operator}/{dtype}: command ranks={command_ranks}, "
                f"expected={ranks}",
            )
            continue

        rank_nodes: dict[int, str] = {}
        membership_error = ""
        for _key, by_iteration in groups:
            for by_node in by_iteration.values():
                for node_key, by_rank in by_node.items():
                    for rank in by_rank:
                        previous = rank_nodes.get(rank)
                        if previous is not None and previous != node_key:
                            membership_error = (
                                f"rank {rank} maps to both {previous} and {node_key}"
                            )
                            break
                        rank_nodes[rank] = node_key
                    if membership_error:
                        break
                if membership_error:
                    break
            if membership_error:
                break
        if not membership_error and sorted(rank_nodes) != expected_all_ranks:
            membership_error = f"ranks={sorted(rank_nodes)}"
        if membership_error:
            log_message(
                "ERROR",
                f"cannot establish node membership for {path_type} "
                f"{operator}/{dtype} scope={execution_scope}: "
                f"{membership_error}",
            )
            continue

        node_ranks: dict[str, list[int]] = defaultdict(list)
        for rank, node_key in rank_nodes.items():
            node_ranks[node_key].append(rank)
        for node_key in node_ranks:
            node_ranks[node_key].sort()
        node_keys = sorted(node_ranks)
        reference_node = node_keys[0]

        complete_iterations: dict[
            tuple[str, str, str, str, int, int],
            list[tuple[int, dict[str, Decimal], dict[str, Decimal]]],
        ] = defaultdict(list)
        command_invalid = False
        for key, by_iteration in groups:
            data_size = key[4]
            missing: list[str] = []
            for iteration in range(1, iters + 1):
                by_node = by_iteration.get(iteration, {})
                flattened = sorted(
                    rank for by_rank in by_node.values() for rank in by_rank
                )
                node_membership = {
                    node_key: sorted(by_node.get(node_key, {}))
                    for node_key in node_keys
                }
                if (
                    flattened != expected_all_ranks
                    or set(by_node) != set(node_keys)
                    or any(
                        node_membership[node_key] != node_ranks[node_key]
                        for node_key in node_keys
                    )
                ):
                    missing.append(
                        f"iteration {iteration}: ranks={flattened}, "
                        f"nodes={node_membership}"
                    )
                    continue
                node_medians: dict[str, Decimal] = {}
                node_skews: dict[str, Decimal] = {}
                for node_key in node_keys:
                    starts = list(by_node[node_key].values())
                    node_medians[node_key] = statistics.median(starts)
                    node_skews[node_key] = max(starts) - min(starts)
                complete_iterations[key].append((iteration, node_medians, node_skews))
            if missing or len(complete_iterations[key]) != iters:
                log_message(
                    "ERROR",
                    f"cannot calculate {path_type} start skew for "
                    f"{operator}/{dtype}/{data_size} scope={execution_scope}: "
                    + "; ".join(missing[:5]),
                )
                command_invalid = True
        if command_invalid:
            continue

        node_offset_samples: dict[str, list[Decimal]] = defaultdict(list)
        for entries in complete_iterations.values():
            for _iteration, node_medians, _node_skews in entries:
                reference_start = node_medians[reference_node]
                for node_key in node_keys:
                    node_offset_samples[node_key].append(
                        node_medians[node_key] - reference_start
                    )
        node_offsets = {
            node_key: statistics.median(node_offset_samples[node_key])
            for node_key in node_keys
        }
        if len(node_keys) > 1:
            log_message(
                "INFO",
                f"multi-node start baseline for {path_type} "
                f"{operator}/{dtype} scope={execution_scope}, "
                f"reference={reference_node}: "
                + ", ".join(
                    f"{node_key}={quantize_time(node_offsets[node_key])}us"
                    for node_key in node_keys
                ),
            )

        for key, entries in complete_iterations.items():
            _scope, key_operator, key_dtype, key_path, data_size, key_ranks = key
            node_local_skews: list[Decimal] = []
            cross_node_residuals: list[Decimal] = []
            for _iteration, node_medians, node_skews in entries:
                node_local_skews.append(max(node_skews.values()))
                if len(node_keys) > 1:
                    corrected_starts = [
                        node_medians[node_key] - node_offsets[node_key]
                        for node_key in node_keys
                    ]
                    cross_node_residuals.append(
                        max(corrected_starts) - min(corrected_starts)
                    )
            node_local_p95, node_local_max = percentile_and_max(node_local_skews)
            cross_p95: float | None = None
            cross_max: float | None = None
            if cross_node_residuals:
                cross_p95, cross_max = percentile_and_max(cross_node_residuals)
            summary_key = (
                key_operator,
                key_dtype,
                key_path,
                data_size,
                key_ranks,
            )
            summary = StartSkewSummary(
                node_local_p95_us=node_local_p95,
                node_local_max_us=node_local_max,
                cross_node_residual_p95_us=cross_p95,
                cross_node_residual_max_us=cross_max,
            )
            previous = result.get(summary_key)
            if previous is not None and previous != summary:
                log_message(
                    "ERROR",
                    f"conflicting start-skew summaries for {summary_key}: "
                    f"{previous} versus {summary}",
                )
                result.pop(summary_key, None)
                conflicted.add(summary_key)
            elif summary_key not in conflicted:
                result[summary_key] = summary
    return result


# Copied from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7 (API path only).
def parse_raw_profile_task(task: ProfileParseTask) -> ProfileParseResult:
    started = time.monotonic()
    profile_root = task.profile.raw_profile_root
    if profile_root is None:
        raise AnalysisError("raw profile task is missing raw_profile_root")
    device_dir, _device_id = raw_profile_device_dir(profile_root)
    load_started = time.monotonic()
    nodes, hccl_ops, flags = parse_raw_aicpu_records(device_dir)
    stars_events, complete_event_count, diagnostics = parse_raw_stars_events(device_dir)
    load_seconds = time.monotonic() - load_started
    try:
        wall_offset_us = raw_device_time_to_epoch_offset_us(profile_root, device_dir)
    except AnalysisError as exc:
        diagnostics.append(
            f"clock mapping unavailable; cross-rank start skew will be blank: {exc}"
        )
        wall_offset_us = None

    phase_started = time.monotonic()
    # The benchmark runs the API path; the HCCL baseline is a separate tool.
    if task.profile.path_type != "api":
        raise AnalysisError("API benchmark cannot parse an HCCL baseline capture")
    result = parse_raw_api_profile(
        task, nodes, stars_events, complete_event_count, diagnostics, wall_offset_us
    )
    result.load_seconds = load_seconds
    result.phase_parse_seconds = time.monotonic() - phase_started
    result.elapsed_seconds = time.monotonic() - started
    return result
