# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Bind native submission facts to one collector, never by call order or time."""

import copy
from decimal import Decimal
import math

from . import api_core as api


def require(condition, message):
    if not condition:
        raise ValueError(message)


def uint(value, field):
    require(type(value) is int and value >= 0, field + " must be uint")
    return value


def collector(profiling_root, manifest):
    """Find a collector from process/device metadata, not a PROF name pattern."""
    matches = []
    for metadata in sorted(profiling_root.rglob("host/info.json")):
        profile = metadata.parent.parent
        require(
            profiling_root in profile.resolve().parents,
            "native PROF metadata escapes collected profiling directory",
        )
        pid, _ = api.raw_profile_process_info(profile)
        if pid != manifest["backend_pid"]:
            continue
        _, device = api.raw_profile_device_dir(profile)
        matches.append((profile, device))
    require(
        len(matches) == 1,
        "native capture requires exactly one single-device PROF matching collector PID; found "
        + str(len(matches)),
    )
    return matches[0]


def as_index(capture, profiling_root, manifest):
    """Translate only raw facts; computed performance metrics never enter indexes."""
    require(
        capture.get("kind") == "native_profile_capture",
        "native capture kind is invalid",
    )
    require(
        capture.get("measurement_mode") in {"full", "comm_only"},
        "native profiling requires full or comm_only measurement mode",
    )
    require(
        uint(capture.get("logical_device"), "capture.logical_device")
        == manifest["logical_device"],
        "native capture logical device differs from rank manifest",
    )
    engine = capture.get("engine")
    if engine != "aicpu_ts":
        require(
            capture["measurement_mode"] == "full",
            "native CCU profiling requires full measurement mode",
        )
        require(
            isinstance(capture.get("soc_name"), str)
            and capture["soc_name"].startswith("Ascend950")
            and type(capture.get("cycles_per_us")) in {int, float}
            and capture["cycles_per_us"] == 1000,
            "native CCU Wait clock requires Ascend950 and cycles_per_us=1000",
        )
    profile, device = collector(profiling_root, manifest)
    index = copy.deepcopy(capture)
    index["collector_device_id"] = device
    index["collector_profile"] = str(profile.relative_to(profiling_root))
    require(isinstance(index.get("entries"), list), "capture.entries must be an array")
    for entry in index["entries"]:
        submissions = entry.pop("submissions", None)
        require(isinstance(submissions, list), "entry.submissions must be an array")
        entry["samples"] = [
            {
                "phase": submission.get("phase"),
                "iteration": submission.get("iteration"),
                "profile": str(profile.relative_to(profiling_root)),
                "selector": {
                    "stream_id": submission.get("stream_id"),
                    "task_id": submission.get("task_id"),
                },
                "native_submission": submission,
            }
            for submission in submissions
        ]
    return index


def wait_duration_ns(submission):
    """Read the legacy duration, or a previously archived raw-cycle report."""
    if "wait_duration_ns" in submission:
        require(
            "wait_begin_cycle" not in submission and "wait_end_cycle" not in submission,
            "native CCU Wait report mixes duration and raw-cycle layouts",
        )
        duration = uint(submission["wait_duration_ns"], "wait_duration_ns")
        require(duration > 0, "native CCU Wait duration must be positive")
        return duration
    begin = uint(submission.get("wait_begin_cycle"), "wait_begin_cycle")
    end = uint(submission.get("wait_end_cycle"), "wait_end_cycle")
    require(end > begin, "native CCU Wait interval must be positive and ordered")
    # Archived captures require the same validated 1 ns/cycle clock below.
    return end - begin


def validate_submission(submission, engine, cycles_per_us=None):
    require(isinstance(submission, dict), "native submission must be an object")
    phase = submission.get("phase")
    require(phase in {"warmup", "measured"}, "unknown native submission phase")
    iteration = uint(submission.get("iteration"), "submission.iteration")
    require(phase != "measured" or iteration > 0, "measured iteration starts at one")
    for field in ("stream_id", "task_id"):
        uint(submission.get(field), "submission." + field)
    require(
        type(submission.get("wait_report_present")) is bool,
        "native submission requires explicit wait_report_present",
    )
    if phase == "measured":
        event = submission.get("acl_event_us")
        require(
            type(event) in {int, float} and math.isfinite(event) and event > 0,
            "native measured submission has no positive ACL Event interval",
        )
        if engine != "aicpu_ts":
            require(submission["wait_report_present"], "native CCU Wait report missing")
            require(
                cycles_per_us == 1000,
                "native CCU submission has no validated cycle conversion",
            )
            duration = wait_duration_ns(submission)
            require(
                Decimal(str(event)) * Decimal(1000) >= duration,
                "native CCU Event-minus-Wait interval is negative",
            )


def wait_and_framework(entry, cycles_per_us):
    """Return rank-zero means of measured Wait and Event-minus-Wait intervals."""
    measured = [
        value["native_submission"]
        for value in entry["samples"]
        if value["phase"] == "measured"
    ]
    require(
        len(measured) == entry["iterations"], "native CCU measured coverage missing"
    )
    waits, frameworks = [], []
    for submission in measured:
        validate_submission(submission, "ccu_sched", cycles_per_us)
        wait = Decimal(wait_duration_ns(submission)) / Decimal(1000)
        framework = Decimal(str(submission["acl_event_us"])) - wait
        require(framework >= 0, "native CCU Event-minus-Wait interval is negative")
        waits.append(wait)
        frameworks.append(framework)
    return (
        float(sum(waits) / len(waits)),
        float(sum(frameworks) / len(frameworks)),
    )
