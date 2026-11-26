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

"""CCU extraction from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7."""

from __future__ import annotations
import os
import re
import shutil
from decimal import Decimal
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence

from . import api_core as common
from . import ccu_probe_markers as markers


# Probe pairing follows hccl_api_test PR43 (3c699a8); summaries retain PR42's rules.
API_PROBE_COLUMNS = (
    ("api_algorithm_us", "api纯算法耗时(us)"),
    ("api_kfc_head_us", "api KFC head耗时(us)"),
    ("api_kfc_load_us", "api KFC 参数加载及判定耗时(us)"),
    ("api_kfc_signal_us", "api KFC 完成信号段耗时(us)"),
    ("api_kfc_tail_us", "api KFC 队列收尾耗时(us)"),
)
API_PROBE_COVERAGE_HEADERS = (
    "api探针完整样本数(卡×轮)",
    "api探针预期样本数(卡×轮)",
    "api探针完整率(%)",
)
API_PROBE_COVERAGE_FIELDS = (
    "api_probe_complete_samples",
    "api_probe_expected_samples",
    "api_probe_complete_pct",
)

_common_should_retain = common.should_retain_complete_event

CCU_TEXT_RE = re.compile(
    r"(?:CCU|Ccu|ccu)(?=[A-Z0-9_\-\s]|mission\b|task\b|kernel\b|$)"
)


LOOPGROUP_RE = re.compile(r"^LoopGroup$", re.I)


FUSION_TEXT_RE = re.compile(r"fusion", re.I)


HOST_API_EVENT_RE = re.compile(r"^AscendCL@")


CUSTOM_KERNEL_RE = re.compile(
    r"^(AllGather|AllReduce|ReduceScatter|AlltoAllvWrite|AllToAllV|AllToAll)"
    r"CustomV\d+.*$",
    re.I,
)

WRITE_BENCH_KERNEL_RE = re.compile(
    r"^(?:AllToAllVWriteBench(?:Kernel)?|all_to_all_v_write_bench(?:Kernel)?)$", re.I
)

# Native benchmark package names are exact operator identities.  In particular,
# AllToAllV must not be accepted as AllToAll by a prefix comparison.
NATIVE_BENCH_NAMES = {
    "AllReduce": ("AllReduceCcuBench", "all_reduce_ccu_bench"),
    "AllGather": ("AllGatherCcuBench", "all_gather_ccu_bench"),
    "ReduceScatter": ("ReduceScatterCcuBench", "reduce_scatter_ccu_bench"),
    "AllToAll": ("AllToAllCcuBench", "all_to_all_ccu_bench"),
    "AllToAllV": ("AllToAllVCcuBench", "all_to_all_v_ccu_bench"),
}


def native_bench_operator(name: str) -> str | None:
    # The legacy CustomV* matcher covers both op and opKernel spellings.
    # Apply the same device-kernel suffix rule to the isolated package names.
    normalized = name.lower()
    if normalized.endswith("kernel"):
        normalized = normalized[: -len("kernel")]
    for operator, names in NATIVE_BENCH_NAMES.items():
        if normalized in {item.lower() for item in names}:
            return operator
    return None


CUSTOM_KERNEL_TEXT_RE = re.compile(
    r"(?:all[_\s-]?gather|all[_\s-]?reduce|reduce[_\s-]?scatter|"
    r"all[_\s-]?to[_\s-]?all[_\s-]?v?(?:[_\s-]?write)?)"
    r"[_\s-]*custom[_\s-]*v?\d+",
    re.I,
)


MC2_CCU_CLIENT_RE = re.compile(
    r"^mc2_(?:all_gather|all_reduce|reduce_scatter|all_to_all_v_write|all_to_all_v|all_to_all)_client$",
    re.I,
)


API_PRINT_HEADER_RE = re.compile(
    r"=+\s*API\s+(?P<dtype>[A-Za-z0-9_]+)\s*=+",
    re.IGNORECASE,
)


API_PRINT_DIRECT_HEADER_RE = re.compile(
    r"=+\s*mc2_ccu_direct\s+\S+\s+(?P<dtype>[A-Za-z0-9_]+)\s+range=",
    re.IGNORECASE,
)


API_PRINT_OPERATOR_RE = re.compile(
    r"=+\s*(?P<operator>"
    r"AllGather|AllReduce|ReduceScatter|AlltoAllvWrite|AllToAllV|AllToAll"
    r")\s*=+",
    re.IGNORECASE,
)


API_PRINT_DIRECT_OPERATOR_RE = re.compile(
    r"mc2_ccu_direct_bench\s+\((?P<operator>"
    r"AllGather|AllReduce|ReduceScatter|AlltoAllvWrite|AllToAllV|AllToAll"
    r"),\s*direct launch\)",
    re.IGNORECASE,
)


API_PRINT_RESULT_RE = re.compile(
    r"^\s*(?P<size>\d+)\s*\|\s*"
    r"(?P<mean_time>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<p50>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<p95>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<spread>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<bandwidth>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<result>[A-Za-z]+)\s*$"
)


API_PRINT_FRAMEWORK_RESULT_RE = re.compile(
    r"^\s*(?P<size>\d+)\s*\|\s*"
    r"(?P<mean_time>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<p50>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<p95>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<spread>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<framework>[-+]?[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<bandwidth>[0-9]+(?:\.[0-9]+)?)\s*\|\s*"
    r"(?P<result>[A-Za-z]+)\s*$"
)


def has_custom_kernel_text(text: str) -> bool:
    """Return whether profiler text identifies one of this suite's custom ops."""
    return CUSTOM_KERNEL_TEXT_RE.search(text) is not None


def has_mc2_ccu_client_text(text: str) -> bool:
    """Identify the direct benchmark's AIV client kernel."""
    return MC2_CCU_CLIENT_RE.search(text) is not None


def has_exported_timeline(profile_root: Path) -> bool:
    return any(profile_root.glob("mindstudio_profiler_output/msprof_*.json")) or any(
        profile_root.glob("device_*/mindstudio_profiler_output/msprof_*.json")
    )


def resolve_msprof() -> str | None:
    configured = os.environ.get("MSPROF_BIN", "").strip()
    if configured:
        path = Path(configured).expanduser()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path.resolve())
        raise common.AnalysisError(
            f"MSPROF_BIN is not an executable file: {configured}"
        )
    discovered = shutil.which("msprof")
    if discovered:
        return discovered
    ascend_home = os.environ.get("ASCEND_HOME_PATH", "").strip()
    if ascend_home:
        candidate = Path(ascend_home) / "tools" / "profiler" / "bin" / "msprof"
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def raw_text(raw: Mapping[str, Any]) -> str:
    parts = [str(raw.get("name", "")), str(raw.get("cat", ""))]
    args = raw.get("args")
    if isinstance(args, Mapping):
        parts.extend(str(value) for value in args.values())
    return " ".join(parts)


def should_retain_ccu_event(raw: Mapping[str, Any]) -> bool:
    text = raw_text(raw)
    name = str(raw.get("name", ""))
    args = raw.get("args")
    return bool(
        _common_should_retain(raw)
        or CUSTOM_KERNEL_RE.fullmatch(name)
        or WRITE_BENCH_KERNEL_RE.fullmatch(name)
        or native_bench_operator(name)
        or MC2_CCU_CLIENT_RE.fullmatch(name)
        or has_custom_kernel_text(text)
        or has_mc2_ccu_client_text(text)
        or CCU_TEXT_RE.search(text)
        or FUSION_TEXT_RE.search(text)
        or LOOPGROUP_RE.fullmatch(name)
        or (
            name == "Wait"
            and isinstance(args, Mapping)
            and "Notify Instruction ID" in args
        )
    )


def is_api_launcher(trace: Any, event: Any, operator: str) -> bool:
    """Identify the device-side custom or direct client kernel."""
    if HOST_API_EVENT_RE.match(event.name):
        return False
    return bool(
        CUSTOM_KERNEL_RE.fullmatch(event.name)
        or WRITE_BENCH_KERNEL_RE.fullmatch(event.name)
        or native_bench_operator(event.name)
        or MC2_CCU_CLIENT_RE.fullmatch(event.name)
    )


def overlap(left: Any, right: Any) -> Decimal:
    return max(Decimal(0), min(left.end, right.end) - max(left.start, right.start))


def deduplicate_launchers(candidates: Sequence[Any]) -> list[Any]:
    """Keep the widest event when Fusion and its AI Core child overlap."""
    retained: list[Any] = []
    for candidate in sorted(
        candidates,
        key=lambda event: (-event.duration, event.start, event.end),
    ):
        if any(
            overlap(candidate, existing)
            >= min(candidate.duration, existing.duration) * Decimal("0.9")
            for existing in retained
        ):
            continue
        retained.append(candidate)
    return sorted(retained, key=lambda event: (event.start, event.end))


def loopgroup_events_in_launcher(trace: Any, launcher: Any) -> list[Any]:
    """Return device-side LoopGroup hardware events inside a launcher window."""
    return sorted(
        (
            event
            for event in common.events_in_window(trace, launcher.start, launcher.end)
            if event.end <= launcher.end and LOOPGROUP_RE.fullmatch(event.name)
        ),
        key=lambda event: (event.start, event.end),
    )


def wall_clock_start(_profile: Any, start: Decimal) -> Decimal:
    """Return the start on msprof's already normalized trace timebase.
    ``raw_device_time_to_epoch_offset_us`` is for raw AICPU/STARS device
    counters.  Applying it to an exported ``msprof_*.json`` timestamp shifts
    each rank by its collection-time offset and creates multi-second false
    start skew.  CCU parses exported timelines only, so use ``ts`` directly.
    """
    return start


def parse_api_print_results(
    root: Path,
) -> dict[tuple[str, str, int], tuple[float, float, float | None]]:
    """Parse printed API benchmark results from ``prof_api.txt``.
    Returns a dict mapping ``(operator, dtype, data_size)`` to
    ``(mean_time_us, alg_bandwidth_gbps, api_framework_time_us)``.  When the
    printed row carries the ``api_framework_time(us)`` column, ``mean_time_us``
    is the kernel-measured ``hccl.Wait`` duration (the new API communication
    time) and ``api_framework_time_us`` is the original ACL-Event time minus
    that Wait-based time.  Legacy seven-column rows keep ACL-Event timing and
    report ``api_framework_time_us`` as ``None``.
    """
    prof_api_path = root / "prof_api.txt"
    if not prof_api_path.is_file():
        return {}
    results: dict[tuple[str, str, int], tuple[float, float, float | None]] = {}
    current_dtype: str | None = None
    current_operator: str | None = None
    try:
        text = prof_api_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    for line in text.splitlines():
        header_match = API_PRINT_HEADER_RE.search(line)
        if header_match is None:
            header_match = API_PRINT_DIRECT_HEADER_RE.search(line)
        if header_match is not None:
            dtype = common.normalize_dtype(header_match.group("dtype"))
            current_dtype = dtype if dtype is not None else current_dtype
            current_operator = None
            continue
        op_match = API_PRINT_OPERATOR_RE.search(line)
        direct_op_match = API_PRINT_DIRECT_OPERATOR_RE.search(line)
        if direct_op_match is not None:
            op_match = direct_op_match
        if op_match is not None:
            op = common.canonical_operator(op_match.group("operator"))
            current_operator = op if op is not None else current_operator
            continue
        framework: float | None = None
        result_match = API_PRINT_FRAMEWORK_RESULT_RE.search(line)
        if result_match is not None:
            framework = float(result_match.group("framework"))
        else:
            result_match = API_PRINT_RESULT_RE.search(line)
        if result_match is not None and current_dtype and current_operator:
            size = int(result_match.group("size"))
            mean_time = float(result_match.group("mean_time"))
            bandwidth = float(result_match.group("bandwidth"))
            results[(current_operator, current_dtype, size)] = (
                mean_time,
                bandwidth,
                framework,
            )
    return results


# Copied from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7.
resolved_sync_mode = common.resolved_sync_mode
_drop_align_entries = common._drop_align_entries
CCU_LAUNCH_RE = re.compile(r"^CCU_LAUNCH$", re.I)


def is_hccl_launcher(trace: Any, event: Any, operator: str) -> bool:
    """Identify the device-side CCU_LAUNCH task as the launch window."""
    if HOST_API_EVENT_RE.match(event.name):
        return False
    return bool(CCU_LAUNCH_RE.fullmatch(event.name))


def probe_times_by_launcher(
    trace: Any, launchers: Sequence[Any]
) -> dict[int, dict[str, Decimal]]:
    ccu_launches = [
        event for event in trace.events if CCU_LAUNCH_RE.fullmatch(event.name)
    ]
    waits_by_lane: dict[tuple[Any, ...], list[tuple[int, Decimal, Decimal]]] = {}
    for event in trace.events:
        if event.name != "Wait":
            continue
        args = event.args
        instruction = args.get("Notify Instruction ID")
        stream = args.get("Physic Stream Id", args.get("Stream Id"))
        task = args.get("Task Id")
        if (
            not str(instruction).isdecimal()
            or stream is None
            or task is None
            or str(args.get("Mask", 2)) != "2"
        ):
            continue
        lane = (
            event.pid,
            event.tid,
            stream,
            task,
            args.get("Die Id"),
            args.get("Mission Id"),
        )
        waits_by_lane.setdefault(lane, []).append(
            (int(instruction), event.start, event.end)
        )

    matches: dict[int, dict[str, Decimal]] = {}
    for lane, waits in waits_by_lane.items():
        groups: dict[int, list[tuple[Decimal, Decimal]]] = {}
        for kind, start, end in markers.find_probes(waits):
            groups.setdefault(kind, []).append((start, end))
        if len(groups.get(0, [])) != 1 or len(groups.get(1, [])) != 1:
            continue
        begin, finish = groups[0][0], groups[1][0]
        identity = (str(lane[2]), str(lane[3]))
        # Probe IDs belong to the CCU task, not the AIV client on another stream.
        owners = [
            event
            for event in ccu_launches
            if identity in launch_task_ids([event])
            and event.start
            <= begin[0]
            <= begin[1]
            <= finish[0]
            <= finish[1]
            <= event.end
        ]
        if len(owners) != 1:
            continue
        candidates = [
            i
            for i, launcher in enumerate(launchers)
            if launcher.start
            <= begin[0]
            <= begin[1]
            <= finish[0]
            <= finish[1]
            <= launcher.end
        ]
        if len(candidates) != 1:
            continue
        index = candidates[0]
        values = {"api_algorithm_us": finish[0] - begin[1]}
        # Finalize also loads parameters. Use the preceding load from this turn.
        load = [mark for mark in groups.get(4, []) if mark[1] <= begin[0]]
        if load:
            values["api_kfc_load_us"] = begin[0] - load[-1][1]
        head_start, head_end = groups.get(2, []), groups.get(3, [])
        if (
            len(head_start) == len(head_end) == 1
            and head_start[0][1] <= head_end[0][0] <= begin[0]
        ):
            values["api_kfc_head_us"] = head_end[0][0] - head_start[0][1]
        signal, tail = groups.get(5, []), groups.get(6, [])
        if len(signal) == 1 and finish[1] <= signal[0][0]:
            values["api_kfc_signal_us"] = signal[0][0] - finish[1]
            if len(tail) == 1 and signal[0][1] <= tail[0][0]:
                values["api_kfc_tail_us"] = tail[0][0] - signal[0][1]
        # Missions can overlap. Take each stage's longest measured duration,
        # without pairing marker endpoints from different lanes.
        stages = matches.setdefault(index, {})
        for field, duration in values.items():
            if field not in stages or duration > stages[field]:
                stages[field] = duration
    return matches


@dataclass(frozen=True)
class CcuLaunchWindow:
    """A single measured collective's device launch window."""

    start: Decimal
    end: Decimal
    source: str
    task_ids: frozenset[tuple[str, str]] = frozenset()

    @property
    def duration(self) -> Decimal:
        return self.end - self.start


def launch_task_ids(events: Sequence[Any]) -> frozenset[tuple[str, str]]:
    identities = set()
    for event in events:
        args = event.args
        stream = args.get("Physic Stream Id", args.get("Stream Id"))
        task = args.get("Task Id")
        if stream is not None and task is not None:
            identities.add((str(stream), str(task)))
    return frozenset(identities)


def merge_ccu_launches_by_connection(
    candidates: Sequence[Any],
) -> list[CcuLaunchWindow]:
    """Merge CCU_LAUNCH events that share the same ``connection_id``.

    HCCL may split one collective call into multiple CCU_LAUNCH tasks when
    the kernel's task args exceed the hardware SQE capacity
    (``CCU_SQE_ARGS_LEN = 13`` args per SQE).  For example, the
    ``CcuSchedAllToAllSoleMeshMultiJetty`` algorithm produces
    ``argSize = 11 + 2 * rankSize``, so an 8-rank AllToAll yields 27 args
    and 3 CCU_LAUNCH tasks per call.  All tasks from the same collective
    share the same ``connection_id`` in the profiling args; grouping by
    ``connection_id`` recovers the original call boundary.  The merged
    window spans from the first task's start to the last task's end,
    matching ``op_summary``'s Task Duration.
    """
    groups: dict[Any, list[Any]] = {}
    no_connection: list[Any] = []
    for event in candidates:
        args = getattr(event, "args", None)
        conn_id = args.get("connection_id") if isinstance(args, Mapping) else None
        if conn_id is not None:
            groups.setdefault(conn_id, []).append(event)
        else:
            no_connection.append(event)
    windows: list[CcuLaunchWindow] = []
    for events in groups.values():
        sorted_events = sorted(events, key=lambda e: (e.start, e.end))
        windows.append(
            CcuLaunchWindow(
                start=sorted_events[0].start,
                end=sorted_events[-1].end,
                source="merged-ccu-launch",
                task_ids=launch_task_ids(sorted_events),
            )
        )
    for launcher in deduplicate_launchers(no_connection):
        windows.append(
            CcuLaunchWindow(
                start=launcher.start,
                end=launcher.end,
                source="outer-launcher",
                task_ids=launch_task_ids([launcher]),
            )
        )
    if len(windows) < len(candidates):
        common.print_progress(
            f"merged {len(candidates)} CCU_LAUNCH events into "
            f"{len(windows)} call windows by connection_id "
            f"({len(groups)} grouped, {len(no_connection)} ungrouped)"
        )
    return sorted(windows, key=lambda w: (w.start, w.end))


def explicit_ccu_launch_windows(
    trace: Any,
    path_type: str,
    operator: str,
) -> list[CcuLaunchWindow]:
    """Return windows from the exported device-side launch events.

    For the HCCL path, CCU_LAUNCH events for one collective may be split
    into multiple tasks when kernel args exceed ``CCU_SQE_ARGS_LEN`` (13).
    They are grouped by ``connection_id`` to recover the original call
    boundary.
    """
    if path_type == "api":
        candidates = [
            event for event in trace.events if is_api_launcher(trace, event, operator)
        ]
        windows: list[CcuLaunchWindow] = []
        for launcher in deduplicate_launchers(candidates):
            windows.append(
                CcuLaunchWindow(
                    start=launcher.start,
                    end=launcher.end,
                    source="outer-launcher",
                    task_ids=launch_task_ids([launcher]),
                )
            )
        return windows
    candidates = [
        event for event in trace.events if is_hccl_launcher(trace, event, operator)
    ]
    return merge_ccu_launches_by_connection(candidates)


def parse_ccu_profile(
    profile: Any,
    trace: Any,
    operator: str,
    dtype: str,
    sizes: Sequence[int],
    warmup_iters: int,
    iters: int,
    ranks: int,
    hccl_has_check_call: bool = True,
) -> tuple[list[Any], dict[int, tuple[Decimal, Decimal]], Any]:
    if profile.path_type == "api":
        calls_per_size = warmup_iters + iters
    else:
        calls_per_size = warmup_iters + iters + (1 if hccl_has_check_call else 0)
    measured_calls_per_size = iters
    measured_call_offset = warmup_iters
    expected_kernel_count = len(sizes) * calls_per_size
    expected_samples = len(sizes) * iters
    launchers = explicit_ccu_launch_windows(trace, profile.path_type, operator)
    samples: list[Any] = []
    kernel_windows: dict[int, tuple[Decimal, Decimal]] = {}
    anomaly_count = 0
    kernel_compute_events = sorted(
        (
            event
            for event in trace.events
            if CUSTOM_KERNEL_RE.fullmatch(event.name)
            or MC2_CCU_CLIENT_RE.fullmatch(event.name)
            or WRITE_BENCH_KERNEL_RE.fullmatch(event.name)
            or native_bench_operator(event.name)
        ),
        key=lambda event: (event.start, event.end),
    )
    align_extra = len(sizes) * iters
    if (
        profile.path_type == "api"
        and resolved_sync_mode(profile) == "device"
        and len(launchers) == expected_kernel_count + align_extra
    ):
        # 对齐轮模式（仅 api 路径 -s device）：每档 warmup 前缀之后为
        # [对齐,被测] 交替，剔除对齐条目后恢复常规配对。非 device 模式的
        # 异常数量保持原有计数失配失败路径，不静默减半。
        launchers = _drop_align_entries(
            launchers, sizes, warmup_iters, iters, calls_per_size
        )
        if len(kernel_compute_events) == expected_kernel_count + align_extra:
            kernel_compute_events = _drop_align_entries(
                kernel_compute_events, sizes, warmup_iters, iters, calls_per_size
            )
        common.log_message(
            "INFO",
            f"api device {profile.device_id}: align-op pairing detected, "
            f"dropped {align_extra} align entries",
        )
    # A mixed-engine run (e.g. ReduceScatter using CCU for small sizes and
    # AICPU for large sizes) produces CCU launch windows for only part of the
    # size list. Map the prefix and mark the remaining CCU-specific columns NA
    # instead of failing the whole case.
    expected_total_kernel_count = expected_kernel_count
    mapped_size_count = len(sizes)
    if (
        profile.path_type == "hccl"
        and len(launchers) > 0
        and len(launchers) % calls_per_size == 0
    ):
        candidate = len(launchers) // calls_per_size
        if 0 < candidate < len(sizes):
            mapped_size_count = candidate
            expected_kernel_count = candidate * calls_per_size
            expected_samples = candidate * iters
            common.log_message(
                "WARNING",
                f"{profile.path_type} device {profile.device_id}: mixed engine "
                f"selection detected; CCU covers {candidate}/{len(sizes)} "
                f"size(s) ({len(launchers)}/{expected_total_kernel_count} "
                "CCU windows). Non-CCU sizes keep printed HCCL time/bandwidth; "
                "CCU-specific columns are marked NA.",
            )
    use_kernel_compute_clock = (
        profile.path_type == "api"
        and len(kernel_compute_events) == expected_kernel_count
    )
    if use_kernel_compute_clock and len(launchers) != expected_kernel_count:
        common.log_message(
            "WARNING",
            f"{profile.path_type} device {profile.device_id}: launcher "
            f"count {len(launchers)} != expected "
            f"{expected_kernel_count}; API kernel-start skew will use the "
            f"{len(kernel_compute_events)} kernel-compute events so that all "
            "ranks are compared on the same device clock",
        )
    if len(launchers) == expected_kernel_count:
        probe_times = (
            probe_times_by_launcher(trace, launchers)
            if profile.path_type == "api"
            else {}
        )
        if profile.path_type == "api" and probe_times:
            complete = sum(
                len(values) == len(API_PROBE_COLUMNS) for values in probe_times.values()
            )
            if complete != len(launchers):
                common.log_message(
                    "WARNING",
                    f"api device {profile.device_id}: incomplete KFC probes: "
                    f"{complete}/{len(launchers)} launch windows have all stages; "
                    "missing stages remain blank, not zero",
                )
        # All five means use exactly the same complete rank/iteration samples.
        probe_times = {
            index: values
            for index, values in probe_times.items()
            if all(field in values for field, _ in API_PROBE_COLUMNS)
        }
        for size_index, data_size in enumerate(sizes[:mapped_size_count]):
            base = size_index * calls_per_size
            kernel_windows[data_size] = (
                launchers[base].start,
                launchers[base + calls_per_size - 1].end,
            )
            for measured_index in range(measured_calls_per_size):
                launcher_index = base + measured_call_offset + measured_index
                launcher = launchers[launcher_index]
                loopgroup_events = loopgroup_events_in_launcher(trace, launcher)
                loopgroup_us = sum(
                    (event.end - event.start for event in loopgroup_events),
                    Decimal(0),
                )
                if use_kernel_compute_clock:
                    start_wall = wall_clock_start(
                        profile,
                        kernel_compute_events[
                            base + measured_call_offset + measured_index
                        ].start,
                    )
                else:
                    start_wall = wall_clock_start(profile, launcher.start)
                common_fields = dict(
                    path_type=profile.path_type,
                    profile_path=common.relative_display_path(profile.json_path),
                    operator=operator,
                    dtype=dtype,
                    data_size=data_size,
                    ranks=ranks,
                    device_id=profile.device_id,
                    size_index=size_index,
                    call_index=measured_call_offset + measured_index,
                    measured_iteration=measured_index + 1,
                    kernel_start=launcher.start,
                    first_boundary_start=launcher.start,
                    first_condition_end=None,
                    first_communication_start=launcher.start,
                    last_boundary_end=launcher.end,
                    kernel_or_next_start=launcher.end,
                    communication_task_count=len(loopgroup_events),
                    condition_count=0,
                    kernel_start_wall_us=start_wall,
                )
                if profile.path_type == "hccl":
                    samples.append(
                        common.Sample(
                            **common_fields,
                            hccl_communication_us=launcher.duration,
                            hccl_loopgroup_us=loopgroup_us,
                        )
                    )
                else:
                    samples.append(
                        common.Sample(
                            **common_fields,
                            api_loopgroup_us=loopgroup_us,
                            **probe_times.get(launcher_index, {}),
                        )
                    )
    else:
        anomaly_count = expected_samples
        common.log_message(
            "WARNING",
            f"{profile.path_type} device {profile.device_id}: CCU launch-window "
            f"count {len(launchers)} != expected "
            f"{expected_kernel_count}",
        )
    status = (
        "PASS"
        if len(launchers) == expected_kernel_count
        and len(samples) == expected_samples
        and anomaly_count == 0
        else "FAIL"
    )
    audit = common.Audit(
        path_type=profile.path_type,
        operator=operator,
        dtype=dtype,
        device_id=profile.device_id,
        profile_path=common.relative_display_path(profile.json_path),
        kernel_count=len(launchers),
        expected_kernel_count=expected_kernel_count,
        valid_samples=len(samples),
        expected_samples=expected_samples,
        anomaly_count=anomaly_count,
        status=status,
    )
    return samples, kernel_windows, audit


# Keep the upstream summary builder intact; the adapter below selects API probe fields.
CCU_SUMMARY_HEADERS = [
    "算子类型",
    "数据类型",
    "数据量",
    "卡数",
    "hccl通信耗时(us)",
    "hccl通信带宽(GB/s)",
    "hccl首通信任务启动偏差p95(us)",
    "hccl首通信任务启动偏差max(us)",
    "hccl loopgroup耗时(us)",
    "api通信耗时(us)",
    "api框架耗时(us)",
    "api通信带宽(GB/s)",
    "api loopgroup耗时(us)",
    *(header for _, header in API_PROBE_COLUMNS),
    *API_PROBE_COVERAGE_HEADERS,
    "api kernel启动偏差p95(us)",
    "api kernel启动偏差max(us)",
    "api算法",
    "hccl算法",
]


def ccu_build_summary_rows(
    aggregates: Mapping[tuple[Any, ...], Any],
    algorithms: Mapping[tuple[Any, ...], str],
    hccl_print_results: Mapping[tuple[str, str, int], tuple[float, float]],
    start_skews: Mapping[tuple[str, str, str, int, int], Any],
    sizes: Sequence[int],
    operator: str,
    dtype: str,
    ranks: int,
    iters: int,
    multi_node: bool,
) -> list[list[Any]]:
    expected = ranks * iters
    rows: list[list[Any]] = []
    for data_size in sizes:
        hccl_group = aggregates.get((operator, dtype, "hccl", data_size, ranks))
        api_group = aggregates.get((operator, dtype, "api", data_size, ranks))
        hccl_valid = hccl_group is not None and hccl_group.sample_count == expected
        api_valid = api_group is not None and api_group.sample_count == expected
        hccl_print = hccl_print_results.get((operator, dtype, data_size))
        hccl_communication = hccl_print[0] if hccl_print is not None else None
        hccl_bandwidth = hccl_print[1] if hccl_print is not None else None
        hccl_start_skew = start_skews.get((operator, dtype, "hccl", data_size, ranks))
        api_start_skew = start_skews.get((operator, dtype, "api", data_size, ranks))
        hccl_loopgroup = hccl_group.mean("hccl_loopgroup_us") if hccl_valid else None
        api_communication = (
            api_group.mean("api_communication_us") if api_valid else None
        )
        api_loopgroup = api_group.mean("api_loopgroup_us") if api_valid else None
        probe_means = [
            api_group.mean(field) if api_valid else None
            for field, _ in API_PROBE_COLUMNS
        ]
        probe_count = api_group.counts.get("api_algorithm_us", 0) if api_valid else 0
        row = [
            operator,
            dtype,
            data_size,
            ranks,
            hccl_communication,
            hccl_bandwidth,
            hccl_start_skew.node_local_p95_us if hccl_start_skew is not None else None,
            hccl_start_skew.node_local_max_us if hccl_start_skew is not None else None,
        ]
        if multi_node:
            row.extend(
                [
                    hccl_start_skew.cross_node_residual_p95_us
                    if hccl_start_skew is not None
                    else None,
                    hccl_start_skew.cross_node_residual_max_us
                    if hccl_start_skew is not None
                    else None,
                ]
            )
        row.extend(
            [
                hccl_loopgroup,
                api_communication,
                # api框架耗时(us): patched from the printed api_framework_time
                # column (original ACL-Event time minus hccl.Wait time).
                None,
                data_size / api_communication * 1e-3 if api_communication else None,
                api_loopgroup,
                *probe_means,
                probe_count if api_group is not None else None,
                expected if api_group is not None else None,
                100.0 * probe_count / expected
                if api_group is not None and expected
                else None,
                api_start_skew.node_local_p95_us
                if api_start_skew is not None
                else None,
                api_start_skew.node_local_max_us
                if api_start_skew is not None
                else None,
            ]
        )
        if multi_node:
            row.extend(
                [
                    api_start_skew.cross_node_residual_p95_us
                    if api_start_skew is not None
                    else None,
                    api_start_skew.cross_node_residual_max_us
                    if api_start_skew is not None
                    else None,
                ]
            )
        row.append(algorithms.get((operator, dtype, "api", data_size, ranks)))
        row.append(algorithms.get((operator, dtype, "hccl", data_size, ranks)))
        rows.append(row)
    return rows


def probe_summary_metrics(accumulator, operator, dtype, data_size, ranks, iters):
    """Adapt PR42's summary row to the framework's structured metric keys."""
    rows = ccu_build_summary_rows(
        {(operator, dtype, "api", data_size, ranks): accumulator},
        {},
        {},
        {},
        [data_size],
        operator,
        dtype,
        ranks,
        iters,
        False,
    )
    row = dict(zip(CCU_SUMMARY_HEADERS, rows[0]))
    fields = (
        *API_PROBE_COLUMNS,
        *zip(API_PROBE_COVERAGE_FIELDS, API_PROBE_COVERAGE_HEADERS),
    )
    return {field: row[title] for field, title in fields if row[title] is not None}
