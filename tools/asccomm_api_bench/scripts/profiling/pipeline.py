# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Associate real device events with selected benchmark entries, without guessing."""

import copy
import csv
from dataclasses import asdict, replace
from decimal import Decimal
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from . import api_core as api
from . import ccu_core as ccu
from . import native_capture
from . import legacy_capture
from . import algorithm_evidence

OPERATORS = {
    "allreduce": "AllReduce",
    "allgather": "AllGather",
    "reducescatter": "ReduceScatter",
    "alltoall": "AllToAll",
    "alltoallv": "AllToAllV",
    "alltoallvwrite": "AlltoAllvWrite",
}
SUCCESS = {"hit", "fallback", "algorithm_unconfirmed"}
SHAPE_FIELDS = {
    "requested_bytes",
    "input_count",
    "output_count",
    "input_bytes",
    "output_bytes",
    "bandwidth_bytes",
    "per_rank_count",
    "per_peer_count",
}
DTYPE_BYTES = {
    **dict.fromkeys(("int8", "uint8", "hif8", "fp8e4m3", "fp8e5m2", "fp8e8m0"), 1),
    **dict.fromkeys(("int16", "uint16", "fp16", "bfp16", "bf16"), 2),
    **dict.fromkeys(("int32", "uint32", "fp32"), 4),
    **dict.fromkeys(("int64", "uint64", "fp64"), 8),
}


class AssociationError(ValueError):
    """An input cannot be safely associated with this job."""


class NoMeasurements(Exception):
    """The plan has no successfully executed entries requiring device samples."""


def require(condition, message):
    if not condition:
        raise AssociationError(message)


def uint(value, name, positive=False):
    require(
        type(value) is int and value >= (1 if positive else 0), name + " must be uint"
    )
    return value


def validate_shape(shape, operator, dtype, ranks):
    require(dtype in DTYPE_BYTES, "unsupported actual-shape dtype")
    width = DTYPE_BYTES[dtype]
    for direction in ("input", "output"):
        require(
            shape[direction + "_count"] > 0
            and shape[direction + "_bytes"] == shape[direction + "_count"] * width,
            "actual shape byte/count/dtype mismatch",
        )
    if operator == "AllGather":
        require(
            shape["input_count"] == shape["per_rank_count"]
            and shape["output_count"] == ranks * shape["per_rank_count"]
            and shape["per_peer_count"] == 0,
            "actual AllGather rank shape mismatch",
        )
    elif operator == "ReduceScatter":
        require(
            shape["output_count"] == shape["per_rank_count"]
            and shape["input_count"] == ranks * shape["per_rank_count"]
            and shape["per_peer_count"] == 0,
            "actual ReduceScatter rank shape mismatch",
        )
    elif operator in {"AllToAll", "AllToAllV"}:
        require(
            shape["input_count"]
            == shape["output_count"]
            == ranks * shape["per_peer_count"]
            and shape["per_rank_count"] == 0,
            "actual AllToAll rank shape mismatch",
        )
    elif operator == "AlltoAllvWrite":
        # Full shape/layout checks are shared with the record renderer below.
        require(
            shape["input_count"]
            == shape["output_count"]
            == ranks * shape["per_peer_count"]
            and shape["per_rank_count"] == 0,
            "actual AlltoAllvWrite window shape mismatch",
        )
    else:
        require(
            shape["input_count"] == shape["output_count"]
            and shape["per_rank_count"] == shape["per_peer_count"] == 0,
            "actual AllReduce shape mismatch",
        )
    require(
        shape["bandwidth_bytes"]
        == shape["output_bytes" if operator == "AllGather" else "input_bytes"],
        "actual bandwidth byte convention mismatch",
    )


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def read_json(path):
    with Path(path).open(encoding="utf-8") as stream:
        return json.load(stream, object_pairs_hook=unique_pairs)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def contained(root, relative):
    require(
        isinstance(relative, str) and relative,
        "profile path must be a nonempty relative path",
    )
    require(not Path(relative).is_absolute(), "absolute profiling path is forbidden")
    path = (root / relative).resolve()
    require(
        path != root and root in path.parents, "profiling path escapes rank directory"
    )
    require(path.exists(), "missing collected profiling input: " + relative)
    return path


def hash_input(path, rank_dir, hashes):
    if path.is_dir():
        files = sorted(item for item in path.rglob("*") if item.is_file())
        require(files, "empty raw profiling directory")
    else:
        files = [path]
    for item in files:
        require(
            rank_dir in item.resolve().parents,
            "symlink escapes collected rank directory",
        )
        hashes[str(item.relative_to(rank_dir))] = sha256(item)


def package_provenance(run_dir, owner, job, audit):
    """Bind offline analysis to the package actually verified for this run."""
    metadata = job.setdefault("metadata", {})
    require(isinstance(metadata, dict), "job.metadata must be an object")
    declared = metadata.get("measurement_mode", "unknown")
    require(
        declared in {"unknown", "full", "comm_only"}, "invalid job measurement_mode"
    )
    preparation = owner.get("package_preparation", {})
    require(isinstance(preparation, dict), "package_preparation must be an object")
    manifest_path = run_dir / "deploy" / "manifest.json"
    requested = preparation.get("requested") is True
    if not requested and not manifest_path.is_file():
        audit["measurement_mode"] = declared
        metadata["measurement_mode"] = declared
        return declared
    from package_deploy import (
        CCU_PACKAGE_MARKER,
        PACKAGE_MARKER,
        SOURCE_COMMIT,
        marker_values,
    )

    deployment = read_json(manifest_path)
    require(isinstance(deployment, dict), "deployment manifest must be an object")
    claimed_marker = deployment.get("marker")
    require(isinstance(claimed_marker, dict), "deployment has no package marker fields")
    package = claimed_marker.get("package")
    require(
        package in {"asccomm_api_bench", "asccomm_api_bench_ccu"},
        "unsupported deployment package",
    )
    profile = "ccu" if package == "asccomm_api_bench_ccu" else "aicpu"
    engine = job["options"]["engine"]
    if profile == "ccu":
        require(
            engine in {"ccu_sched", "ccu_ms"},
            "CCU package preparation requires a CCU engine",
        )
    else:
        require(
            engine == "aicpu_ts", "AICPU package preparation cannot label a CCU run"
        )
    if requested:
        require(
            preparation.get("profile", profile) == profile,
            "launcher/deployment package profiles conflict",
        )
    marker_name = CCU_PACKAGE_MARKER if profile == "ccu" else PACKAGE_MARKER
    library_name = (
        "libasccomm_api_bench_ccu_opapi.so" if profile == "ccu" else "libcust_opapi.so"
    )
    require(
        deployment.get("run_id") == audit["run_id"]
        and deployment.get("source_commit") == SOURCE_COMMIT,
        "deployment manifest belongs to another run/source",
    )
    require(
        deployment.get("status") == "verified"
        and deployment.get("mpi_allowed") is True,
        "package deployment did not permit this MPI run",
    )
    if requested:
        require(
            preparation.get("status") == "verified",
            "launcher package preparation was not verified",
        )
    mode = deployment.get("measurement_mode")
    require(mode in {"full", "comm_only"}, "deployment measurement_mode is unknown")
    require(
        profile != "ccu" or mode == "full", "CCU package measurement_mode must be full"
    )
    require(declared in {"unknown", mode}, "job/deployment measurement modes conflict")
    require(
        preparation.get("measurement_mode", mode) == mode,
        "launcher/deployment measurement modes conflict",
    )
    nodes = deployment.get("nodes")
    require(
        isinstance(nodes, dict)
        and nodes
        and all(status == "verified" for status in nodes.values()),
        "deployment has unverified nodes",
    )
    if isinstance(owner.get("hosts"), list):
        require(
            set(nodes) == {host["name"] for host in owner["hosts"]},
            "deployment/launcher node sets differ",
        )
    snapshot = deployment.get("vendor_files")
    require(
        isinstance(snapshot, dict) and snapshot, "deployment has no vendor snapshot"
    )
    require(
        all(
            isinstance(name, str)
            and isinstance(value, str)
            and len(value) == 64
            and all(char in "0123456789abcdef" for char in value)
            for name, value in snapshot.items()
        ),
        "invalid deployment vendor SHA256 snapshot",
    )
    marker_hash = snapshot.get(marker_name)
    require(
        marker_hash and any(Path(name).name == library_name for name in snapshot),
        "deployment snapshot lacks package marker or operator library",
    )
    marker_text = deployment.get("marker_text")
    require(
        isinstance(marker_text, str)
        and hashlib.sha256(marker_text.encode("utf-8")).hexdigest() == marker_hash,
        "archived marker text differs from installed vendor snapshot",
    )
    with tempfile.TemporaryDirectory(prefix="mc2-marker-") as temporary:
        marker_path = Path(temporary) / marker_name
        marker_path.write_bytes(marker_text.encode("utf-8"))
        marker = marker_values(
            marker_path, mode, profile=profile, allow_archived_ccu=True
        )
    require(
        deployment.get("marker") == marker, "deployment marker fields/text conflict"
    )
    node_markers = deployment.get("node_markers")
    require(
        isinstance(node_markers, dict) and set(node_markers) == set(nodes),
        "deployment lacks per-node marker evidence",
    )
    require(
        all(
            item.get("sha256") == marker_hash and item.get("marker") == marker
            for item in node_markers.values()
        ),
        "installed marker evidence differs across nodes",
    )
    mode_kind = deployment.get("mode")
    require(mode_kind in {"install", "verify_existing"}, "unknown deployment mode")
    installer_hash = deployment.get("installer_sha256")
    if mode_kind == "install":
        installer = run_dir / "deploy" / "payload" / "api" / "custom.run"
        require(
            installer.is_file() and sha256(installer) == installer_hash,
            "archived installer digest differs from deployment",
        )
        require(
            any(
                item.get("payload") == "custom.run"
                and item.get("sha256") == installer_hash
                for item in deployment.get("files", [])
            ),
            "deployment installer file receipt is inconsistent",
        )
        audit["inputs"][str(installer)] = installer_hash
    build_dir = run_dir / ("ccu_build" if profile == "ccu" else "build")
    build_path = build_dir / "build_manifest.json"
    if build_path.is_file():
        build = read_json(build_path)
        if profile == "ccu":
            require(
                build.get("profile") == "collectives_ccu",
                "CCU build receipt has an incompatible package profile",
            )
        require(
            build.get("status") in {"built", "reused"}
            and build.get("measurement_mode") == mode
            and build.get("marker") == marker,
            "build/deployment marker or mode conflict",
        )
        if mode_kind == "install":
            require(
                build.get("installer", {}).get("sha256") == installer_hash,
                "build/deployment installer digest conflict",
            )
        archived_marker = build_dir / marker_name
        require(
            archived_marker.is_file() and sha256(archived_marker) == marker_hash,
            "built/installed package marker digest differs",
        )
        audit["inputs"][str(build_path)] = sha256(build_path)
        audit["inputs"][str(archived_marker)] = marker_hash
    manifest_hash = sha256(manifest_path)
    audit["inputs"][str(manifest_path)] = manifest_hash
    audit["measurement_mode"] = mode
    audit["package"] = {
        "profile": profile,
        "manifest_sha256": manifest_hash,
        "run_id": deployment["run_id"],
        "mode": mode_kind,
        "measurement_mode": mode,
        "marker": marker,
        "marker_sha256": marker_hash,
        "installer_sha256": installer_hash,
        "vendor_files": snapshot,
        "node_markers": node_markers,
    }
    metadata.update(
        package_profile=profile,
        measurement_mode=mode,
        package_manifest_sha256=manifest_hash,
        package_marker_sha256=marker_hash,
    )
    return mode


def matching_event(trace, selector):
    require(isinstance(selector, dict), "event selector must be an object")
    require(
        set(selector) <= {"name", "pid", "tid", "ts", "dur", "args"},
        "unknown event selector fields",
    )
    require(
        {"name", "pid", "tid", "ts", "dur"} <= set(selector),
        "selector requires exact name/pid/tid/ts/dur",
    )
    require(
        isinstance(selector["name"], str) and selector["name"],
        "selector has no event name",
    )
    uint(selector["pid"], "selector.pid")
    uint(selector["tid"], "selector.tid")
    start, duration = (
        api.decimal_value(selector["ts"]),
        api.decimal_value(selector["dur"]),
    )
    require(
        start.is_finite() and duration.is_finite() and duration > 0,
        "invalid selector time",
    )
    args = selector.get("args", {})
    require(isinstance(args, dict), "selector.args must be an object")
    matched = [
        event
        for event in trace.events
        if event.name == selector["name"]
        and event.pid == selector["pid"]
        and event.tid == selector["tid"]
        and event.start == start
        and event.duration == duration
        and all(event.args.get(key) == value for key, value in args.items())
    ]
    require(
        len(matched) == 1,
        "selector must identify exactly one real event; found " + str(len(matched)),
    )
    return matched[0]


class DeviceReader:
    def __init__(self, engine, operator, dtype, ranks, msprof, work_dir):
        self.engine, self.operator, self.dtype, self.ranks = (
            engine,
            operator,
            dtype,
            ranks,
        )
        self.msprof, self.work_dir = msprof, work_dir
        self.traces, self.raw = {}, {}
        self.exports = []

    def exported_traces(self, path):
        if not path.is_dir():
            if path not in self.traces:
                self.traces[path] = api.load_trace(
                    path,
                    ccu.should_retain_ccu_event if self.engine != "aicpu_ts" else None,
                )
            return [self.traces[path]]
        # Export a working copy: collected evidence remains immutable.
        if path not in self.traces:
            exports = sorted(path.glob("mindstudio_profiler_output/msprof_*.json"))
            exports += sorted(
                path.glob("device_*/mindstudio_profiler_output/msprof_*.json")
            )
            if not exports:
                executable = self.msprof or ccu.resolve_msprof()
                require(
                    executable is not None,
                    "raw PROF export requires msprof (PATH/MSPROF_BIN/--msprof)",
                )
                require(
                    Path(executable).is_file() and os.access(executable, os.X_OK),
                    "msprof is not executable",
                )
                copied = (
                    self.work_dir
                    / (hashlib.sha256(str(path).encode()).hexdigest()[:20])
                    / path.name
                )
                copied.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(path, copied)
                result = subprocess.run(
                    [str(executable), "--export=on", "--output=" + str(copied)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=300,
                    check=False,
                )
                self.exports.append(
                    {
                        "source": str(path),
                        "exporter": str(executable),
                        "exporter_sha256": sha256(executable),
                        "returncode": result.returncode,
                        "output": result.stdout[-16384:],
                    }
                )
                require(
                    result.returncode == 0,
                    "msprof export failed: " + result.stdout[-4096:],
                )
                exports = sorted(
                    copied.glob("mindstudio_profiler_output/msprof_*.json")
                )
                exports += sorted(
                    copied.glob("device_*/mindstudio_profiler_output/msprof_*.json")
                )
            require(exports, "msprof produced no exported timeline")
            if self.exports and self.exports[-1]["source"] == str(path):
                self.exports[-1]["exported_sha256"] = {
                    str(item.relative_to(copied)): sha256(item) for item in exports
                }
            self.traces[path] = [
                api.load_trace(item, ccu.should_retain_ccu_event) for item in exports
            ]
        return self.traces[path]

    def exported(self, path, selector, native=False):
        require(not native, "native captures use the copied legacy batch parser")
        matches = []
        for trace in self.exported_traces(path):
            try:
                matches.append((trace, matching_event(trace, selector)))
            except AssociationError:
                pass
        require(
            len(matches) == 1,
            "raw export selector is missing or ambiguous across timelines",
        )
        return matches[0]

    def raw_sample(
        self,
        path,
        selector,
        profile,
        backend_pid,
    ):
        if path not in self.raw:
            device_dir, local_device = api.raw_profile_device_dir(path)
            process_id, _ = api.raw_profile_process_info(path)
            require(
                process_id == backend_pid,
                "raw PROF collector PID differs from rank backend_pid",
            )
            require(
                local_device == profile.local_device_id,
                "raw PROF physical device differs from rank manifest",
            )
            nodes, _ops, _flags = api.parse_raw_aicpu_records(device_dir)
            stars, count, diagnostics = api.parse_raw_stars_events(device_dir)
            require(
                not diagnostics,
                "incomplete raw STARS records: " + "; ".join(diagnostics[:5]),
            )
            offset = api.raw_device_time_to_epoch_offset_us(path, device_dir)
            self.raw[path] = nodes, stars, count, offset
        nodes, stars, count, offset = self.raw[path]
        require(
            isinstance(selector, dict) and {"stream_id", "task_id"} <= set(selector),
            "raw selector requires stream_id/task_id",
        )
        require(
            set(selector) <= {"stream_id", "task_id", "ts", "dur"},
            "unknown raw selector fields",
        )
        for field in ("stream_id", "task_id"):
            uint(selector[field], "selector." + field)
        task_candidates = [
            node
            for node in nodes
            if node.task_id == selector["task_id"]
            and (
                "ts" not in selector or node.start == api.decimal_value(selector["ts"])
            )
            and (
                "dur" not in selector
                or node.end - node.start == api.decimal_value(selector["dur"])
            )
        ]
        selected = [
            node for node in task_candidates if node.stream_id == selector["stream_id"]
        ]
        require(len(selected) == 1, "raw selector must identify one AICPU node")
        kernel = selected[0]
        require(
            not any(
                node != kernel and node.start < kernel.end and kernel.start < node.end
                for node in nodes
            ),
            "overlapping raw AICPU kernels have ambiguous communication ownership",
        )
        task = api.ProfileParseTask(0, profile, (1,), 0, 1, self.ranks)
        parsed = api.parse_raw_api_profile(task, selected, stars, count, [], offset)
        require(
            parsed.audit.status == "PASS" and len(parsed.samples) == 1,
            "raw API boundary parsing failed: " + "; ".join(parsed.diagnostics),
        )
        # Keep the original raw identity for the whole-job event reuse check.
        # Resolving a missing stream must not create a second identity.
        return parsed.samples[0], (
            "raw",
            kernel.stream_id,
            kernel.task_id,
            str(kernel.start),
            str(kernel.end),
        )

    def sample(
        self, path, selector, manifest, requested_bytes, iteration, native=False
    ):
        require(not native, "native captures require the legacy batch reader")
        profile = api.ProfileFile(
            "api",
            manifest["global_rank"],
            path,
            None,
            operator=self.operator,
            dtype=self.dtype,
            raw_profile_root=path if path.is_dir() else None,
            local_device_id=(
                manifest["collector_device_id"]
                if native
                else manifest["logical_device"]
            ),
            node_key=str(manifest["node_index"]),
            sync_mode="mpi",
        )
        if self.engine == "aicpu_ts" and path.is_dir() and not native:
            sample, identity = self.raw_sample(
                path, selector, profile, manifest["backend_pid"]
            )
            return replace(
                sample, data_size=requested_bytes, measured_iteration=iteration
            ), identity
        trace, kernel = self.exported(path, selector, native)
        if self.engine == "aicpu_ts":
            require(
                kernel.pid in trace.hardware_pids,
                "selected API kernel is not an Ascend Hardware event",
            )
            require(
                api.API_KERNEL_RE.fullmatch(kernel.name)
                and kernel.name.startswith(self.operator + "Custom"),
                "kernel does not match requested API operator",
            )
            candidates = [
                event
                for event in trace.events
                if api.API_KERNEL_RE.fullmatch(event.name)
            ]
        else:
            require(
                ccu.is_api_launcher(trace, kernel, self.operator),
                "selected event is not a device-side CCU API launcher",
            )
            if self.operator == "AlltoAllvWrite":
                require(
                    kernel.pid in trace.hardware_pids,
                    "selected AlltoAllvWrite launcher is not an Ascend Hardware event",
                )
            direct_names = {
                "AllReduce": "all_reduce",
                "AllGather": "all_gather",
                "ReduceScatter": "reduce_scatter",
                "AllToAll": "all_to_all",
                "AllToAllV": "all_to_all_v",
                "AlltoAllvWrite": "all_to_all_v_write",
            }
            custom = ccu.CUSTOM_KERNEL_RE.fullmatch(kernel.name)
            require(
                (
                    custom is not None
                    and custom.group(1).lower() == self.operator.lower()
                )
                or (
                    self.operator == "AlltoAllvWrite"
                    and ccu.WRITE_BENCH_KERNEL_RE.fullmatch(kernel.name)
                )
                or ccu.native_bench_operator(kernel.name) == self.operator
                or kernel.name.lower()
                == "mc2_" + direct_names[self.operator] + "_client",
                "CCU launcher operator differs from job",
            )
            candidates = [
                event
                for event in trace.events
                if ccu.is_api_launcher(trace, event, self.operator)
            ]
        require(
            not any(
                event is not kernel
                and event.start < kernel.end
                and kernel.start < event.end
                for event in candidates
            ),
            "overlapping collective launchers cannot be assigned by time window",
        )
        if self.engine == "aicpu_ts":
            sample = api.parse_api_sample(
                trace,
                kernel,
                profile,
                self.operator,
                self.dtype,
                requested_bytes,
                0,
                iteration - 1,
                iteration,
                self.ranks,
            )
            sample.kernel_start_wall_us = kernel.start
        else:
            loopgroups = ccu.loopgroup_events_in_launcher(trace, kernel)
            # As in the legacy parser, an empty window contributes zero and keeps its sample.
            # LoopGroup is cumulative instruction duration, not wall-clock algorithm time.
            sample = api.Sample(
                "api",
                str(path),
                self.operator,
                self.dtype,
                requested_bytes,
                self.ranks,
                manifest["global_rank"],
                0,
                iteration - 1,
                iteration,
                kernel.start,
                kernel.start,
                None,
                kernel.start,
                kernel.end,
                kernel.end,
                len(loopgroups),
                0,
                kernel_start_wall_us=ccu.wall_clock_start(profile, kernel.start),
                api_loopgroup_us=sum(
                    (event.duration for event in loopgroups), Decimal(0)
                ),
            )
        return sample, (
            "json",
            kernel.name,
            kernel.pid,
            kernel.tid,
            str(kernel.start),
            str(kernel.duration),
        )


def printed_result(
    rank_dir, descriptor, operator, dtype, requested_bytes, bandwidth_bytes, hashes
):
    require(
        isinstance(descriptor, dict) and set(descriptor) == {"path", "sha256"},
        "printed_result requires path and sha256",
    )
    path = contained(rank_dir, descriptor["path"])
    require(
        path.is_file() and sha256(path) == descriptor["sha256"],
        "printed result SHA256 mismatch",
    )
    hash_input(path, rank_dir, hashes)
    text = path.read_text(encoding="utf-8")
    rows = [
        ccu.API_PRINT_FRAMEWORK_RESULT_RE.search(line)
        or ccu.API_PRINT_RESULT_RE.search(line)
        for line in text.splitlines()
    ]
    rows = [row for row in rows if row]
    require(
        len(rows) == 1
        and rows[0].group("result").upper() in {"PASS", "SUCCESS", "NULL"},
        "printed result must contain exactly one non-failed measured point",
    )
    # Reuse the source parser on a private one-point view; never let its
    # (operator,dtype,length) dictionary combine separate algorithms/batches.
    with tempfile.TemporaryDirectory(prefix="mc2-printed-") as temporary:
        copied = Path(temporary) / "prof_api.txt"
        copied.write_text(text, encoding="utf-8")
        values = ccu.parse_api_print_results(Path(temporary))
    require(
        set(values) == {(operator, dtype, requested_bytes)},
        "printed result identity differs from entry",
    )
    communication, old_bandwidth, framework = values[(operator, dtype, requested_bytes)]
    require(
        math.isfinite(communication) and communication > 0,
        "invalid printed communication time",
    )
    require(
        math.isfinite(old_bandwidth) and old_bandwidth >= 0, "invalid printed bandwidth"
    )
    # The original binaries print mean to .2f and bandwidth to .5f. Validate
    # their product as an interval, against actual payload bytes (AG output,
    # RS input, etc.); the first printed column alone is requested_bytes.
    minimum_bytes = (
        max(0, communication - 0.005) * max(0, old_bandwidth - 0.000005) * 1000
    )
    maximum_bytes = (communication + 0.005) * (old_bandwidth + 0.000005) * 1000
    require(
        minimum_bytes <= bandwidth_bytes <= maximum_bytes,
        "printed communication/bandwidth differs from actual shape.bandwidth_bytes",
    )
    require(
        framework is None or (math.isfinite(framework) and framework >= 0),
        "invalid Event-minus-Wait; signed diagnostic cannot be a performance metric",
    )
    return communication, framework


def load_records(path):
    # The renderer owns the shared schema validation, including query/plan/result
    # and one selected result per entry. It does not infer profiling association.
    import report

    records = report.read_records(path)
    report.build_report(records)
    require(
        sum(record.get("type") == "job" for record in records) == 1,
        "exactly one job header is required",
    )
    return records


def analyze_run(run_dir, records_path, output_dir, *, msprof=None):
    """Analyze one archived launcher job; return paths and a process-style status.

    Successful entries survive failures elsewhere. All complete rank/iteration
    sets are required before a selected result receives any performance metric.
    """
    run_dir, records_path, output_dir = (
        Path(value).resolve() for value in (run_dir, records_path, output_dir)
    )
    collected_root = run_dir / "collected"
    require(
        output_dir != collected_root and collected_root not in output_dir.parents,
        "analysis output must not modify collected profiling evidence",
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    audit_path, enriched_path = (
        output_dir / "profiling_audit.json",
        output_dir / "enriched_records.jsonl",
    )
    audit = {
        "schema_version": 1,
        "profiling_association_verified": False,
        "job_id": None,
        "run_id": None,
        "inputs": {},
        "coverage": [],
        "issues": [],
        "measurement_mode": "unknown",
        "source": "hccl_api_test@" + legacy_capture.SOURCE_COMMIT,
        "cross_node_clock_interpretation": "signed median baseline residual; not absolute cross-host launch skew",
    }
    records, samples, per_rank, entry_rows, cross_rows = [], [], [], [], []
    try:
        require(records_path != enriched_path, "input records cannot be overwritten")
        records = copy.deepcopy(load_records(records_path))
        job = records[0]
        job_id, options = job["job_id"], job["options"]
        ranks, engine = options["ranks"], options["engine"]
        operator, dtype = OPERATORS[options["op"]], options["dtype"]
        native = job.get("metadata", {}).get("profiling_index") == "native_capture"
        if native:
            job["metadata"].update(
                profiling_parser="hccl_api_test_legacy_batch",
                profiling_parser_source=legacy_capture.SOURCE_COMMIT,
                sample_association="complete_capture_order",
            )
            audit["native_identity"] = {
                "collector": "unique collector PID and physical device; job/rank/shape verified",
                "samples": "legacy raw AICPU or CCU launcher order with complete count checks",
                "client_server_association": "not_used",
            }
            job["metadata"].setdefault(
                "native_event_metrics",
                {
                    str(record["entry"]["id"]): copy.deepcopy(record.get("metrics", {}))
                    for record in records
                    if record.get("type") == "result"
                },
            )
            job["metadata"].setdefault(
                "native_event_timing_source", job["metadata"].get("timing_source")
            )
            job["metadata"].setdefault(
                "native_event_timing_scope", job["metadata"].get("timing_scope")
            )
        owner_path = run_dir / "launch_manifest.json"
        if not owner_path.is_file():
            owner_path = run_dir / "run_owner.json"
        owner = read_json(owner_path)
        run_id = owner.get("run_id")
        require(isinstance(run_id, str) and run_id, "launcher run_id is missing")
        audit.update(job_id=job_id, run_id=run_id, engine=engine, world_size=ranks)
        audit["inputs"][str(records_path)] = sha256(records_path)
        audit["inputs"][str(owner_path)] = sha256(owner_path)
        if engine != "aicpu_ts":
            configuration = owner.get("ccu_probes", {})
            require(isinstance(configuration, dict), "invalid CCU probe configuration")
            requested = configuration.get("requested", False)
            require(type(requested) is bool, "invalid CCU probe request flag")
            audit["ccu_probes"] = {
                "requested": requested,
                "source_commit": ccu.markers.SOURCE_COMMIT,
                "source_pr": 42,
                "configuration": configuration,
                "entries": [],
            }
            job.setdefault("metadata", {})["ccu_probes"] = audit["ccu_probes"]
        package_provenance(run_dir, owner, job, audit)
        if native and audit["measurement_mode"] == "comm_only":
            require(
                audit.get("package", {}).get("profile") == "aicpu",
                "native comm_only analysis requires verified AICPU package deployment",
            )
        if operator == "AlltoAllvWrite":
            require(
                engine in {"ccu_sched", "ccu_ms"},
                "AlltoAllvWrite requires a CCU engine",
            )
            job.setdefault("metadata", {}).update(
                write_wire_dtype=options["dtype"],
                write_offset_unit="bytes",
                write_output_buffer="legacy_output_tensor",
                write_bandwidth_scope="per_rank_input_including_self",
            )
            audit["operator_contract"] = {
                "wire_dtype": options["dtype"],
                "offset_unit": "bytes",
                "output_buffer": "legacy_output_tensor",
                "bandwidth_scope": "per_rank_input_including_self",
                "pattern": "balanced",
                "mission_count": 2,
            }
        selected = {
            record["entry"]["id"]: record
            for record in records
            if record["type"] == "result"
            and record["status"] in SUCCESS
            and record["ret"] == 0
        }
        expected_keys = {(entry, record["batch"]) for entry, record in selected.items()}
        phases = [
            record
            for record in records
            if record["type"] == "phase" and record.get("phase") == "measurement"
        ]
        expected_keys.update(
            (record["entry_id"], record["batch"])
            for record in phases
            if record["entry_id"] in selected
        )
        if not selected:
            audit["no_measured_entries"] = True
            raise NoMeasurements()
        by_key, errors, rank_indices = {}, {}, {}

        def issue(key, message, rank=None, stage="parse_failed"):
            errors.setdefault(key, []).append(
                {"rank": rank, "stage": stage, "error": message}
            )

        rank_root = run_dir / "collected" / "ranks"
        rank_dirs = sorted(path for path in rank_root.glob("rank_*") if path.is_dir())
        manifests = {}
        for rank_dir in rank_dirs:
            try:
                manifest = read_json(rank_dir / "manifest.json")
                rank = uint(manifest.get("global_rank"), "manifest.global_rank")
                require(
                    rank < ranks and rank not in manifests,
                    "unexpected or duplicate global rank",
                )
                require(
                    rank_dir.name == "rank_" + str(rank).zfill(6),
                    "rank directory/manifest mismatch",
                )
                require(
                    manifest.get("run_id") == run_id
                    and manifest.get("world_size") == ranks,
                    "rank manifest belongs to another run/layout",
                )
                uint(manifest.get("backend_pid"), "manifest.backend_pid", True)
                for name in ("local_rank", "node_index", "logical_device"):
                    uint(manifest.get(name), "manifest." + name)
                require(
                    isinstance(manifest.get("hostname"), str) and manifest["hostname"],
                    "missing rank hostname",
                )
                uint(manifest.get("local_size"), "manifest.local_size", True)
                require(
                    manifest["local_rank"] < manifest["local_size"],
                    "invalid local rank layout",
                )
                status = read_json(rank_dir / "status.json")
                require(
                    status.get("run_id") == run_id
                    and status.get("global_rank") == rank
                    and status.get("backend_pid") == manifest["backend_pid"]
                    and status.get("exit_code") == 0,
                    "rank backend failed or completion identity is inconsistent",
                )
                require(
                    status.get("backend_returncode", 0) == 0,
                    "rank backend_returncode is not successful",
                )
                manifests[rank] = manifest
                index_path = (
                    rank_dir
                    / "profiling"
                    / ("native_capture.json" if native else "profiling_index.json")
                )
                if not native and not index_path.is_file():
                    index_path = rank_dir / "profiling_index.json"
                index = read_json(index_path)
                hashes = {
                    str(index_path.relative_to(rank_dir)): sha256(index_path),
                    "manifest.json": sha256(rank_dir / "manifest.json"),
                    "status.json": sha256(rank_dir / "status.json"),
                }
                audit["inputs"][rank_dir.name] = hashes
                if native:
                    index = native_capture.as_index(
                        index, rank_dir / "profiling", manifest
                    )
                    audit.setdefault("native_collectors", {})[str(rank)] = {
                        "logical_device": index["logical_device"],
                        "collector_device_id": index["collector_device_id"],
                        "profile": index["collector_profile"],
                    }
                require(
                    index.get("schema_version") == 1
                    and index.get("run_id") == run_id
                    and index.get("job_id") == job_id
                    and index.get("global_rank") == rank
                    and index.get("world_size") == ranks
                    and index.get("backend_pid") == manifest["backend_pid"]
                    and index.get("engine") == engine
                    and index.get("measurement_mode", audit["measurement_mode"])
                    == audit["measurement_mode"],
                    "profiling index belongs to another job/rank/backend/engine",
                )
                entries = index.get("entries")
                require(
                    isinstance(entries, list),
                    "profiling index.entries must be an array",
                )
                seen = set()
                for entry in entries:
                    key = (
                        uint(entry.get("entry_id"), "entry_id"),
                        uint(entry.get("batch"), "batch"),
                    )
                    if key not in expected_keys:
                        audit["issues"].append(
                            {
                                "rank": rank,
                                "error": "unplanned entry/batch in profiling index",
                                "key": list(key),
                            }
                        )
                        continue
                    if key in seen:
                        issue(key, "duplicate entry/batch in rank index", rank)
                        continue
                    seen.add(key)
                    record = selected[key[0]]
                    try:
                        require(
                            entry.get("requested_bytes")
                            == record["entry"]["requested_bytes"],
                            "index length differs from plan",
                        )
                        shape = record.get("shape", {})
                        require(
                            set(shape) == SHAPE_FIELDS and entry.get("shape") == shape,
                            "index shape differs from complete actual result shape",
                        )
                        for name, value in shape.items():
                            uint(value, "shape." + name)
                        require(
                            shape["requested_bytes"] == entry["requested_bytes"]
                            and shape["bandwidth_bytes"] > 0,
                            "invalid actual data shape",
                        )
                        validate_shape(shape, operator, dtype, ranks)
                        if operator == "AlltoAllvWrite":
                            from report import (
                                validate_write_layout,
                                validate_write_plan,
                            )

                            layout = validate_write_layout(record, options)
                            validate_write_plan(entry.get("write_plan"), layout, rank)
                        iterations = uint(entry.get("iterations"), "iterations", True)
                        configured = job.get("metadata", {}).get(
                            "measurement_iterations"
                        )
                        require(
                            configured is None or str(iterations) == str(configured),
                            "formal iteration count differs from job metadata",
                        )
                        by_key.setdefault(key, {})[rank] = (
                            rank_dir,
                            manifest,
                            entry,
                            hashes,
                        )
                    except (ValueError, TypeError, KeyError, AttributeError) as exc:
                        issue(key, str(exc), rank)
                for key in expected_keys - seen:
                    issue(
                        key,
                        "rank index is missing expected entry/batch",
                        rank,
                        "collection_failed",
                    )
                rank_indices[rank] = index
            except (OSError, ValueError, TypeError, KeyError, AttributeError) as exc:
                audit["issues"].append(
                    {"rank_directory": str(rank_dir), "error": str(exc)}
                )
                for key in expected_keys:
                    issue(key, str(exc), stage="collection_failed")
        for rank in set(range(ranks)) - set(rank_indices):
            for key in expected_keys:
                issue(
                    key,
                    "missing valid rank index " + str(rank),
                    rank,
                    "collection_failed",
                )
        # Node and local-rank identities must remain stable across repeated local
        # physical device IDs and repeated process IDs on different machines.
        membership = {}
        node_members = {}
        for rank, manifest in manifests.items():
            local = (manifest["node_index"], manifest["local_rank"])
            require(local not in membership, "duplicate node/local-rank membership")
            membership[local] = rank
            node_members.setdefault(manifest["node_index"], []).append(manifest)
        for node, members in node_members.items():
            require(
                len({member["logical_device"] for member in members}) == len(members),
                "duplicate physical device on node " + str(node),
            )
            require(
                len({member["backend_pid"] for member in members}) == len(members),
                "duplicate backend PID on node " + str(node),
            )
            require(
                len({(member["hostname"], member["local_size"]) for member in members})
                == 1,
                "inconsistent host/local-size on node " + str(node),
            )
            # Missing ranks are already recorded as collection failures, so do
            # not invalidate independent available evidence a second time here.
            if len(rank_indices) == ranks:
                require(
                    sorted(member["local_rank"] for member in members)
                    == list(range(members[0]["local_size"])),
                    "incomplete local-rank membership on node " + str(node),
                )
        with tempfile.TemporaryDirectory(prefix="mc2-profiling-") as temporary:
            reader = DeviceReader(
                engine, operator, dtype, ranks, msprof, Path(temporary)
            )
            audit["exports"] = reader.exports
            legacy_reader = legacy_capture.LegacyCaptureReader(reader)
            if native:
                audit["legacy_parsers"] = legacy_reader.audit
            expected_order = [
                (record["entry"]["id"], record["batch"]) for record in selected.values()
            ]
            used = {}
            fingerprints = {}
            parsed = {}
            for key in sorted(expected_keys):
                coverage = {
                    "entry_id": key[0],
                    "batch": key[1],
                    "ranks": [],
                    "status": "FAIL",
                }
                audit["coverage"].append(coverage)
                declarations = by_key.get(key, {})
                counts = {value[2]["iterations"] for value in declarations.values()}
                if len(counts) != 1:
                    issue(key, "formal iteration declarations disagree across ranks")
                    continue
                iterations = next(iter(counts))
                coverage["expected_iterations"] = iterations
                for rank in range(ranks):
                    item = {
                        "rank": rank,
                        "expected": iterations,
                        "measured": 0,
                        "excluded": 0,
                        "status": "FAIL",
                    }
                    coverage["ranks"].append(item)
                    if rank not in declarations:
                        continue
                    rank_dir, manifest, entry, hashes = declarations[rank]
                    formal = []
                    try:
                        listed = entry.get("samples")
                        require(
                            isinstance(listed, list), "entry.samples must be an array"
                        )
                        if native:
                            warmup = uint(entry.get("warmup"), "entry.warmup")
                            require(
                                sorted(
                                    value.get("iteration")
                                    for value in listed
                                    if value.get("phase") == "warmup"
                                )
                                == list(range(warmup)),
                                "native warmup declarations are missing or duplicated",
                            )
                        for descriptor in listed:
                            if native:
                                native_capture.validate_submission(
                                    descriptor.get("native_submission"),
                                    engine,
                                    rank_indices[rank].get("cycles_per_us"),
                                )
                            phase = descriptor.get("phase")
                            require(
                                phase in {"measured", "warmup", "align"},
                                "unknown profiling phase",
                            )
                            iteration = uint(
                                descriptor.get("iteration"),
                                "sample.iteration",
                                phase == "measured",
                            )
                            path = contained(
                                rank_dir / "profiling", descriptor.get("profile")
                            )
                            if path not in fingerprints:
                                hash_input(path, rank_dir, hashes)
                                if path.is_file():
                                    fingerprints[path] = hashes[
                                        str(path.relative_to(rank_dir))
                                    ]
                                else:
                                    evidence = {
                                        str(item.relative_to(path)): hashes[
                                            str(item.relative_to(rank_dir))
                                        ]
                                        for item in sorted(path.rglob("*"))
                                        if item.is_file()
                                    }
                                    fingerprints[path] = hashlib.sha256(
                                        json.dumps(evidence, sort_keys=True).encode()
                                    ).hexdigest()
                            sample_manifest = (
                                dict(
                                    manifest,
                                    collector_device_id=rank_indices[rank][
                                        "collector_device_id"
                                    ],
                                )
                                if native
                                else manifest
                            )
                            if native:
                                if phase != "measured":
                                    item["excluded"] += 1
                                    continue
                                sample, identity = legacy_reader.sample(
                                    path,
                                    rank_indices[rank],
                                    sample_manifest,
                                    expected_order,
                                    key,
                                    iteration,
                                )
                            else:
                                sample, identity = reader.sample(
                                    path,
                                    descriptor.get("selector"),
                                    sample_manifest,
                                    entry["requested_bytes"],
                                    iteration,
                                )
                            unique = (rank, fingerprints[path], identity)
                            if unique in used:
                                issue(
                                    used[unique],
                                    "same device event reused by another entry/batch/phase",
                                    rank,
                                )
                                raise AssociationError(
                                    "same device event reused by multiple entries/batches/phases"
                                )
                            used[unique] = key
                            if phase != "measured":
                                item["excluded"] += 1
                                continue
                            require(
                                iteration <= iterations,
                                "formal iteration exceeds declaration",
                            )
                            require(
                                iteration not in [value[0] for value in formal],
                                "duplicate formal iteration",
                            )
                            formal.append((iteration, sample, descriptor))
                        require(
                            sorted(value[0] for value in formal)
                            == list(range(1, iterations + 1)),
                            "missing formal iterations; missing samples are never zero",
                        )
                        expected_phase = [
                            value
                            for value in phases
                            if value["entry_id"] == key[0]
                            and value["batch"] == key[1]
                            and value["rank"] == rank
                        ]
                        if expected_phase:
                            for event in ("begin", "end"):
                                require(
                                    sorted(
                                        value["iteration"]
                                        for value in expected_phase
                                        if value["event"] == event
                                    )
                                    == list(range(1, iterations + 1)),
                                    "phase declarations do not cover formal iterations",
                                )
                        parsed.setdefault(key, {})[rank] = [
                            value[1] for value in sorted(formal)
                        ]
                        item.update(measured=len(formal), status="PASS")
                        for iteration, sample, descriptor in sorted(formal):
                            row = asdict(sample)
                            row.update(
                                entry_id=key[0],
                                batch=key[1],
                                global_rank=rank,
                                node_index=manifest["node_index"],
                                backend_pid=manifest["backend_pid"],
                                logical_device=manifest["logical_device"],
                                run_id=run_id,
                                job_id=job_id,
                                selector=descriptor["selector"],
                            )
                            if native:
                                row["association_method"] = (
                                    "legacy_raw_aicpu_order"
                                    if engine == "aicpu_ts"
                                    else "legacy_ccu_launcher_order"
                                )
                                row["selector_role"] = "submission_diagnostic_only"
                                row["native_submission"] = descriptor[
                                    "native_submission"
                                ]
                                row["collector_device_id"] = rank_indices[rank][
                                    "collector_device_id"
                                ]
                            samples.append(row)
                        accumulator = api.MetricAccumulator()
                        for _, sample, _ in formal:
                            accumulator.add(sample)
                        per_rank.append(
                            {
                                "entry_id": key[0],
                                "batch": key[1],
                                "rank": rank,
                                "samples": len(formal),
                                "node_index": manifest["node_index"],
                                "means_us": {
                                    name: accumulator.mean(name)
                                    for name in api.PHASE_FIELDS
                                    if accumulator.counts.get(name)
                                },
                            }
                        )
                    except (
                        OSError,
                        ValueError,
                        TypeError,
                        KeyError,
                        AttributeError,
                        api.AnalysisError,
                        ArithmeticError,
                        subprocess.SubprocessError,
                    ) as exc:
                        issue(key, str(exc), rank)
                        item["error"] = str(exc)
                if errors.get(key):
                    continue
                require(
                    set(parsed.get(key, {})) == set(range(ranks)),
                    "internal rank coverage mismatch",
                )
                coverage["status"] = "PASS"
            # Preserve the upstream signed median baseline formula across complete
            # lengths/formal iterations in one algorithm+MPI-batch scope. Never
            # pool independent batches or different requested algorithms.
            skew_groups = {}
            clock_keys = set()
            for key, by_rank in parsed.items():
                if errors.get(key) or len(by_rank) != ranks:
                    continue
                if native and any(
                    sample.kernel_start_wall_us is None
                    for rank_samples in by_rank.values()
                    for sample in rank_samples
                ):
                    audit.setdefault("warnings", []).append(
                        {
                            "entry_id": key[0],
                            "batch": key[1],
                            "warning": "legacy raw parser has no clock anchor; phase metrics retained, start skew unavailable",
                        }
                    )
                    continue
                clock_keys.add(key)
                entry = selected[key[0]]["entry"]
                count = len(by_rank[0])
                scope = (entry["algorithm"], key[1], count)
                group = skew_groups.setdefault(scope, {})
                observation = group.setdefault(
                    (
                        str(scope),
                        operator,
                        dtype,
                        "api",
                        entry["requested_bytes"],
                        ranks,
                    ),
                    {},
                )
                for rank, rank_samples in by_rank.items():
                    for sample in rank_samples:
                        require(
                            sample.kernel_start_wall_us is not None,
                            "sample lacks clock provenance",
                        )
                        observation.setdefault(
                            sample.measured_iteration, {}
                        ).setdefault(str(manifests[rank]["node_index"]), {})[
                            rank
                        ] = sample.kernel_start_wall_us
            summaries = {}
            for scope, observations in skew_groups.items():
                result = api.build_start_skews(observations, ranks, scope[2])
                for key in parsed:
                    if key not in clock_keys:
                        continue
                    entry = selected[key[0]]["entry"]
                    if (
                        entry["algorithm"],
                        key[1],
                        len(parsed[key].get(0, [])),
                    ) != scope:
                        continue
                    skew = result.get(
                        (operator, dtype, "api", entry["requested_bytes"], ranks)
                    )
                    if skew is None:
                        issue(
                            key,
                            "upstream skew aggregation rejected rank/iteration coverage",
                        )
                    else:
                        summaries[key] = skew
                        cross_rows.append(
                            dict(entry_id=key[0], batch=key[1], **asdict(skew))
                        )
            for key in sorted(expected_keys):
                if errors.get(key):
                    continue
                values = [
                    sample
                    for rank_samples in parsed[key].values()
                    for sample in rank_samples
                ]
                accumulator = api.MetricAccumulator()
                for sample in values:
                    accumulator.add(sample)
                skew = summaries.get(key)
                metrics = {}
                if skew is not None:
                    metrics.update(
                        api_kernel_skew_p95_us=skew.node_local_p95_us,
                        api_kernel_skew_max_us=skew.node_local_max_us,
                    )
                source = "aicpu_condition_boundaries"
                if engine == "aicpu_ts":
                    for target, field in (
                        ("api_head_us", "api_head_us"),
                        ("api_expand_us", "api_expand_us"),
                        ("api_comm_us", "api_communication_us"),
                        ("api_tail_us", "api_tail_us"),
                        ("api_kernel_us", "api_kernel_us"),
                    ):
                        metrics[target] = accumulator.mean(field)
                else:
                    metrics["api_loopgroup_us"] = accumulator.mean("api_loopgroup_us")
                    # Use PR42's summary builder, including its partial-sample
                    # means and denominator, rather than a separate statistic.
                    probe_metrics = ccu.probe_summary_metrics(
                        accumulator,
                        operator,
                        dtype,
                        selected[key[0]]["entry"]["requested_bytes"],
                        ranks,
                        len(parsed[key][0]),
                    )
                    metrics.update(probe_metrics)
                    complete = probe_metrics["api_probe_complete_samples"]
                    expected = probe_metrics["api_probe_expected_samples"]
                    coverage = {
                        "entry_id": key[0],
                        "batch": key[1],
                        "complete_samples": complete,
                        "expected_samples": expected,
                        "excluded_samples": expected - complete,
                        "complete_pct": probe_metrics["api_probe_complete_pct"],
                        "status": "complete"
                        if complete == expected
                        else "partial"
                        if complete
                        else "unavailable",
                    }
                    audit["ccu_probes"]["entries"].append(coverage)
                    if audit["ccu_probes"]["requested"] and complete != expected:
                        audit.setdefault("warnings", []).append(
                            dict(
                                coverage,
                                warning="CCU probe means use complete five-stage samples only",
                            )
                        )
                        if complete == 0:
                            # Match PR42's enabled-path summary check: an empty
                            # probe result is an issue, but preserve base timings.
                            audit["issues"].append(
                                dict(
                                    entry_id=key[0],
                                    batch=key[1],
                                    stage="ccu_probes",
                                    error="CCU profiling has no complete five-stage samples",
                                )
                            )
                    try:
                        # The original tool's printed Event/Wait aggregate belongs
                        # to rank zero; report that scope explicitly, not as an
                        # invented average over ranks lacking those measurements.
                        rank_dir, _manifest, entry, hashes = by_key[key][0]
                        if native:
                            communication, framework = (
                                native_capture.wait_and_framework(
                                    entry, rank_indices[0]["cycles_per_us"]
                                )
                            )
                            event = (
                                selected[key[0]].get("metrics", {}).get("api_kernel_us")
                            )
                            if event is not None:
                                metrics["api_kernel_us"] = event
                        else:
                            communication, framework = printed_result(
                                rank_dir,
                                entry.get("printed_result"),
                                operator,
                                dtype,
                                selected[key[0]]["entry"]["requested_bytes"],
                                selected[key[0]]["shape"]["bandwidth_bytes"],
                                hashes,
                            )
                        metrics["api_comm_us"] = communication
                        if framework is not None:
                            metrics["api_framework_us"] = framework
                        source = (
                            "rank0_wait_and_event_minus_wait"
                            if framework is not None
                            else "rank0_acl_event"
                        )
                    except (OSError, ValueError, KeyError, TypeError) as exc:
                        issue(
                            key,
                            "CCU Event/Wait measurement missing or invalid: "
                            + str(exc),
                        )
                        continue
                communication = metrics["api_comm_us"]
                if communication is None or communication <= 0:
                    issue(
                        key,
                        "nonpositive communication interval after source time quantization",
                    )
                    continue
                metrics["api_comm_gbps"] = (
                    selected[key[0]]["shape"]["bandwidth_bytes"]
                    / communication
                    / 1000.0
                )
                if not all(
                    type(value) in {int, float} and math.isfinite(value) and value >= 0
                    for value in metrics.values()
                ):
                    issue(key, "nonfinite or invalid aggregated performance metric")
                    continue
                entry_rows.append(
                    {
                        "entry_id": key[0],
                        "batch": key[1],
                        "samples": len(values),
                        "ranks": ranks,
                        "metrics": metrics,
                        "communication_source": source,
                        "measurement_mode": audit["measurement_mode"],
                        **(
                            {
                                "write_layout": selected[key[0]]["write_layout"],
                                "operator_contract": audit["operator_contract"],
                            }
                            if operator == "AlltoAllvWrite"
                            else {}
                        ),
                        "package_manifest_sha256": audit.get("package", {}).get(
                            "manifest_sha256"
                        ),
                        "loopgroup_source": "sum_of_instructions_may_overlap"
                        if engine != "aicpu_ts"
                        else None,
                    }
                )
                if key[1] == selected[key[0]]["batch"]:
                    selected[key[0]]["metrics"] = metrics
        for key, failures in errors.items():
            audit["issues"].extend(
                dict(entry_id=key[0], batch=key[1], **failure) for failure in failures
            )
            for coverage in audit["coverage"]:
                if (coverage["entry_id"], coverage["batch"]) == key:
                    coverage["status"] = "FAIL"
            if key[1] == selected[key[0]]["batch"]:
                result = selected[key[0]]
                result.update(
                    metrics={},
                    status="collection_failed"
                    if any(
                        failure["stage"] == "collection_failed" for failure in failures
                    )
                    else "parse_failed",
                    failure_stage="profiling",
                    error="; ".join(failure["error"] for failure in failures),
                    ret=1,
                )
        if audit["issues"]:
            records.append(
                {
                    "schema_version": job["schema_version"],
                    "type": "failure",
                    "job_id": job_id,
                    "stage": "profiling",
                    "ret": 1,
                    "error": "profiling association or coverage failed; see profiling_audit.json",
                }
            )
        audit["profiling_association_verified"] = not audit["issues"] and bool(
            entry_rows
        )
        if native and entry_rows:
            job["metadata"]["timing_source"] = "profiling"
            if engine == "aicpu_ts":
                job["metadata"]["timing_scope"] = (
                    "aicpu_server_profiling_mean_of_rank_iterations"
                )
    except NoMeasurements:
        pass
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        AttributeError,
        RuntimeError,
        api.AnalysisError,
        ArithmeticError,
        subprocess.SubprocessError,
    ) as exc:
        audit["issues"].append({"error": str(exc), "stage": "analysis"})
        for record in records:
            if record.get("type") == "result" and record.get("status") in SUCCESS:
                record.update(
                    status="parse_failed",
                    metrics={},
                    ret=1,
                    failure_stage="profiling",
                    error=str(exc),
                )
        if records:
            records.append(
                {
                    "schema_version": records[0]["schema_version"],
                    "type": "failure",
                    "job_id": records[0]["job_id"],
                    "stage": "profiling",
                    "ret": 1,
                    "error": str(exc),
                }
            )
    if records:
        audit["algorithms"] = algorithm_evidence.enrich(records, run_dir)
    enriched_path.write_text(
        "".join(
            json.dumps(record, ensure_ascii=False, allow_nan=False) + "\n"
            for record in records
        ),
        encoding="utf-8",
    )
    audit["enriched_records_sha256"] = sha256(enriched_path)
    write_json(audit_path, audit)
    write_json(output_dir / "per_rank_summary.json", per_rank)
    write_json(output_dir / "entry_summary.json", entry_rows)
    write_json(output_dir / "cross_node_diagnostics.json", cross_rows)
    with (output_dir / "raw_samples.csv").open(
        "w", encoding="utf-8", newline=""
    ) as stream:
        fields = (
            list(samples[0])
            if samples
            else [
                "run_id",
                "job_id",
                "entry_id",
                "batch",
                "global_rank",
                "measured_iteration",
            ]
        )
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in samples:
            writer.writerow(
                {
                    name: (
                        json.dumps(value, sort_keys=True)
                        if isinstance(value, dict)
                        else str(value)
                        if isinstance(value, Decimal)
                        else value
                    )
                    for name, value in row.items()
                }
            )
    return {
        "returncode": 0 if not audit["issues"] else 1,
        "enriched_records": str(enriched_path),
        "audit_path": str(audit_path),
        "enriched_records_sha256": audit["enriched_records_sha256"],
        "profiling_association_verified": audit["profiling_association_verified"],
        "entry_count": len(entry_rows),
        "issues": len(audit["issues"]),
    }
