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

"""Per-rank entry: validate MPI placement, isolate evidence, and run a backend."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import uuid


RANK_KEYS = ("PMI_RANK", "PMIX_RANK", "OMPI_COMM_WORLD_RANK")
WORLD_KEYS = ("PMI_SIZE", "PMIX_SIZE", "OMPI_COMM_WORLD_SIZE")
LOCAL_KEYS = (
    "MPI_LOCALRANKID",
    "PMI_LOCAL_RANK",
    "PMIX_LOCAL_RANK",
    "OMPI_COMM_WORLD_LOCAL_RANK",
)
LOCAL_SIZE_KEYS = (
    "MPI_LOCALNRANKS",
    "MPI_LOCAL_SIZE",
    "PMI_LOCAL_SIZE",
    "PMIX_LOCAL_SIZE",
    "OMPI_COMM_WORLD_LOCAL_SIZE",
)
SDK_EXEC = r"""
set -e
readonly mc2_bench_runtime_cann=$1
readonly mc2_bench_runtime_vendor=$2
readonly mc2_bench_runtime_query_directory=$3
readonly mc2_bench_runtime_hccl=${HCCL_DIR:-$1}
readonly mc2_bench_runtime_mpi=${MPI_HOME:-${MPI_PATH:-}}
readonly mc2_bench_runtime_path=${PATH:-}
readonly mc2_bench_runtime_libraries=${LD_LIBRARY_PATH:-}
readonly mc2_bench_runtime_profiling_configured=${PROFILING_MODE+x}
readonly mc2_bench_runtime_profiling_mode=${PROFILING_MODE-}
readonly mc2_bench_runtime_profiling_options=${PROFILING_OPTIONS-}
readonly mc2_bench_runtime_log_path=${ASCEND_PROCESS_LOG_PATH:-}
shift 3
mc2_bench_runtime_argv=("$@")
readonly -a mc2_bench_runtime_argv
set --
set +e
if [ -f "$mc2_bench_runtime_cann/set_env.sh" ]; then
    source "$mc2_bench_runtime_cann/set_env.sh" </dev/null
elif [ -f "$mc2_bench_runtime_cann/../set_env.sh" ]; then
    source "$mc2_bench_runtime_cann/../set_env.sh" </dev/null
else
    echo 'CANN set_env.sh not found at backend startup' >&2
    exit 2
fi
mc2_bench_runtime_environment_status=$?
set -e
if [ "$mc2_bench_runtime_environment_status" -ne 0 ]; then
    exit "$mc2_bench_runtime_environment_status"
fi
export ASCEND_HOME_PATH="$mc2_bench_runtime_cann"
export ASCEND_CANN_PACKAGE_PATH="$mc2_bench_runtime_cann"
export ASCEND_OPP_PATH="$mc2_bench_runtime_cann/opp"
if [ -n "$mc2_bench_runtime_log_path" ]; then
    export ASCEND_PROCESS_LOG_PATH="$mc2_bench_runtime_log_path"
fi
if [ -n "$mc2_bench_runtime_vendor" ]; then
    export ASCEND_CUSTOM_OPP_PATH="$mc2_bench_runtime_vendor"
else
    unset ASCEND_CUSTOM_OPP_PATH
fi
export HCCL_DIR="$mc2_bench_runtime_hccl"
if [ -n "$mc2_bench_runtime_mpi" ]; then
    export MPI_HOME="$mc2_bench_runtime_mpi"
fi
export PATH="$mc2_bench_runtime_path${PATH:+:$PATH}"
export LD_LIBRARY_PATH="$mc2_bench_runtime_hccl/lib64:$mc2_bench_runtime_cann/lib64${mc2_bench_runtime_libraries:+:$mc2_bench_runtime_libraries}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -n "$mc2_bench_runtime_vendor" ]; then
    export LD_LIBRARY_PATH="$mc2_bench_runtime_vendor/op_api/lib:$LD_LIBRARY_PATH"
fi
if [ -n "$mc2_bench_runtime_query_directory" ]; then
    export LD_LIBRARY_PATH="$mc2_bench_runtime_query_directory:$LD_LIBRARY_PATH"
fi
if [ "$mc2_bench_runtime_profiling_configured" = x ]; then
    export PROFILING_MODE="$mc2_bench_runtime_profiling_mode"
    export PROFILING_OPTIONS="$mc2_bench_runtime_profiling_options"
fi
exec "${mc2_bench_runtime_argv[@]}"
"""


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp." + uuid.uuid4().hex)
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    os.replace(str(temporary), str(path))


def mpi_integer(keys, label):
    values = []
    for key in keys:
        if key not in os.environ:
            continue
        text = os.environ[key]
        if not text.isascii() or not text.isdecimal():
            raise ValueError("invalid " + label + " in " + key)
        values.append((key, int(text)))
    if not values:
        raise ValueError("missing MPI " + label + "; checked " + ", ".join(keys))
    if len({value for _, value in values}) != 1:
        raise ValueError("conflicting MPI " + label + " variables")
    return values[0][1]


def digest(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def mpi_pass_fds(environment):
    """Keep MPICH/Hydra's PMI connection alive across the backend exec."""
    if "PMI_FD" not in environment:
        return ()
    value = environment["PMI_FD"]
    if not value.isascii() or not value.isdecimal() or int(value) > 2**31 - 1:
        raise ValueError("invalid inherited PMI_FD")
    descriptor = int(value)
    os.fstat(descriptor)
    return (descriptor,)


def verify_payload(config_path, config):
    payload = config_path.parent
    declared = json.loads((payload / "payload.json").read_text(encoding="utf-8"))
    if declared.get("run_id") != config["run_id"]:
        raise ValueError("payload run_id differs from configuration")
    files = declared.get("files")
    if (
        not isinstance(files, dict)
        or "config.json" not in files
        or "rank_entry.py" not in files
    ):
        raise ValueError("invalid payload file manifest")
    for relative, expected in files.items():
        path = payload / relative
        if (
            Path(relative).is_absolute()
            or ".." in Path(relative).parts
            or path.is_symlink()
        ):
            raise ValueError("unsafe payload file path")
        if not path.is_file() or digest(path) != expected:
            raise ValueError("payload checksum mismatch: " + relative)
    owner = json.loads(
        (Path(config["run_dir"]) / "run_owner.json").read_text(encoding="utf-8")
    )
    if owner.get("run_id") != config["run_id"]:
        raise ValueError("run directory belongs to another run")


def stop_process(process, grace=1):
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.poll()
        return
    # A reaped group leader does not imply that its workers have exited.
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        process.poll()
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            return
        time.sleep(min(0.02, max(0, deadline - time.monotonic())))
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    if process.poll() is None:
        process.wait(timeout=3)


@contextmanager
def backend_process(
    command, rank_dir, environment, inherited_fds, *, console_output, rank
):
    if console_output:
        from process_output import capture_process_output

        with capture_process_output(
            command,
            rank_dir / "stdout.log",
            rank_dir / "stderr.log",
            cwd=rank_dir,
            env=environment,
            pass_fds=inherited_fds,
            echo_stdout=rank == 0,
            echo_stderr=True,
            prefix="[rank %d] " % rank,
            cleanup=stop_process,
        ) as process:
            yield process
    else:
        with (
            (rank_dir / "stdout.log").open("xb") as stdout,
            (rank_dir / "stderr.log").open("xb") as stderr,
        ):
            yield subprocess.Popen(
                command,
                cwd=str(rank_dir),
                env=environment,
                stdout=stdout,
                stderr=stderr,
                start_new_session=True,
                pass_fds=inherited_fds,
            )


class EntryInterrupted(Exception):
    def __init__(self, signum):
        self.signum = signum


def interrupted(signum, _frame):
    raise EntryInterrupted(signum)


def record_entry_error(run_dir, run_id, error):
    """An invalid or duplicate rank must not overwrite another rank's evidence."""
    errors = run_dir / "ranks" / "entry_errors"
    errors.mkdir(parents=True, exist_ok=True)
    atomic_json(
        errors / (str(os.getpid()) + "_" + uuid.uuid4().hex + ".json"),
        {
            "run_id": run_id,
            "hostname": socket.gethostname(),
            "pid": os.getpid(),
            "error": str(error),
            "time_utc": utc_now(),
        },
    )


def run(config_path):
    config_path = Path(config_path).resolve()
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("schema_version") != 1 or config.get("mode") not in {
        "topology",
        "backend",
    }:
        raise ValueError("unsupported rank configuration")
    run_dir = Path(config["run_dir"])
    if not run_dir.is_absolute() or not isinstance(config.get("run_id"), str):
        raise ValueError("invalid run directory or identity")
    rank_dir = None
    manifest = None
    process = None
    started = time.monotonic()
    status = {
        "schema_version": 1,
        "run_id": config["run_id"],
        "mode": config["mode"],
        "exit_code": 1,
        "backend_returncode": None,
        "failure_stage": "identity",
        "error": "",
        "started_utc": utc_now(),
        "pid": os.getpid(),
        "backend_pid": None,
    }
    old_signals = {}
    try:
        # Validate before opening log files, which could reuse a closed descriptor.
        inherited_fds = mpi_pass_fds(os.environ) if config["mode"] == "backend" else ()
        verify_payload(config_path, config)
        global_rank = mpi_integer(RANK_KEYS, "global rank")
        world_size = mpi_integer(WORLD_KEYS, "world size")
        local_rank = mpi_integer(LOCAL_KEYS, "local rank")
        local_size = mpi_integer(LOCAL_SIZE_KEYS, "local size")
        expected_ranks = config["ranks"]
        devices_per_node = config["devices_per_node"]
        if (
            type(expected_ranks) is not int
            or type(devices_per_node) is not int
            or expected_ranks < 1
            or devices_per_node < 1
            or expected_ranks % devices_per_node
        ):
            raise ValueError("invalid expected rank layout")
        if world_size != expected_ranks or not 0 <= global_rank < world_size:
            raise ValueError("MPI world/rank does not match requested ranks")
        if local_size != devices_per_node or not 0 <= local_rank < local_size:
            raise ValueError("MPI local rank count does not match devices per node")
        if local_rank != global_rank % devices_per_node:
            raise ValueError("MPI placement must assign consecutive ranks per node")
        node_index = global_rank // devices_per_node
        rank_dir = run_dir / "ranks" / ("rank_%06d" % global_rank)
        rank_dir.mkdir(parents=True, exist_ok=False)
        (rank_dir / "profiling").mkdir()
        (rank_dir / "logs").mkdir()
        manifest = {
            "schema_version": 1,
            "run_id": config["run_id"],
            "mode": config["mode"],
            "global_rank": global_rank,
            "world_size": world_size,
            "local_rank": local_rank,
            "local_size": local_size,
            "node_index": node_index,
            "logical_device": local_rank,
            "hostname": socket.gethostname(),
            "pid": os.getpid(),
            "backend_pid": None,
            "visible_devices": {
                key: config["environment"].get(key, os.environ.get(key, ""))
                for key in ("ASCEND_RT_VISIBLE_DEVICES", "ASCEND_VISIBLE_DEVICES")
            },
            "started_utc": status["started_utc"],
        }
        status["global_rank"] = global_rank
        atomic_json(rank_dir / "manifest.json", manifest)
        for signum in (signal.SIGTERM, signal.SIGINT):
            old_signals[signum] = signal.signal(signum, interrupted)
        status["failure_stage"] = (
            "backend" if config["mode"] == "backend" else "topology"
        )
        if config["mode"] == "backend":
            environment = os.environ.copy()
            environment.update(config["environment"])
            environment.update(
                {
                    "MC2_BENCH_RUN_ID": config["run_id"],
                    "MC2_BENCH_RANK_DIR": str(rank_dir),
                    "MC2_BENCH_PROFILING_DIR": str(rank_dir / "profiling"),
                    "MC2_BENCH_GLOBAL_RANK": str(global_rank),
                    "MC2_BENCH_LOCAL_RANK": str(local_rank),
                    "MC2_BENCH_DEVICE_ID": str(local_rank),
                    "ASCEND_PROCESS_LOG_PATH": str(rank_dir / "logs"),
                }
            )
            command = [
                config["executable"],
                "-n",
                str(expected_ranks),
                "-p",
                str(devices_per_node),
            ]
            command.extend(config["backend_args"])
            if config.get("cann_environment"):
                sdk = config["cann_environment"]
                command = [
                    "bash",
                    "--noprofile",
                    "--norc",
                    "-c",
                    SDK_EXEC,
                    "mc2-bench-sdk",
                    sdk["cann_root"],
                    sdk["vendor_root"],
                    sdk.get("query_library_dir", ""),
                    *command,
                ]
            with backend_process(
                command,
                rank_dir,
                environment,
                inherited_fds,
                console_output=config.get("console_output", False),
                rank=global_rank,
            ) as process:
                manifest["backend_pid"] = process.pid
                status["backend_pid"] = process.pid
                atomic_json(rank_dir / "manifest.json", manifest)
                try:
                    code = process.wait(timeout=config["backend_timeout"])
                except subprocess.TimeoutExpired:
                    status["failure_stage"] = "timeout"
                    status["error"] = "backend exceeded timeout"
                    status["exit_code"] = 124
                    stop_process(process)
                    code = process.returncode
                status["backend_returncode"] = code
                if status["failure_stage"] != "timeout":
                    status["exit_code"] = code if code >= 0 else 128 - code
                    if code:
                        status["error"] = "backend returned nonzero"
        else:
            (rank_dir / "stdout.log").write_text(
                "MPI placement validated; no NPU operation executed.\n",
                encoding="utf-8",
            )
            (rank_dir / "stderr.log").write_text("", encoding="utf-8")
            status["exit_code"] = 0
        if status["exit_code"] == 0:
            status["failure_stage"] = ""
    except EntryInterrupted as exc:
        status["exit_code"] = 128 + exc.signum
        status["failure_stage"] = "signal"
        status["error"] = "entry interrupted by signal " + str(exc.signum)
    except Exception as exc:
        if status["exit_code"] == 0:
            status["exit_code"] = 1
        if not status["failure_stage"]:
            status["failure_stage"] = config["mode"]
        status["error"] = str(exc)
        if manifest is None:
            record_entry_error(run_dir, config["run_id"], exc)
    finally:
        stop_process(process)
        for signum, previous in old_signals.items():
            signal.signal(signum, previous)
        status["finished_utc"] = utc_now()
        status["elapsed_seconds"] = time.monotonic() - started
        if manifest is not None:
            # Also cover interruption immediately after Popen returned.
            if process is not None:
                manifest["backend_pid"] = process.pid
                status["backend_pid"] = process.pid
                atomic_json(rank_dir / "manifest.json", manifest)
            atomic_json(rank_dir / "status.json", status)
    if status["error"]:
        prefix = (
            "[rank %d] " % status["global_rank"]
            if config.get("console_output", False) and "global_rank" in status
            else ""
        )
        print(prefix + "rank entry failed: " + status["error"], file=sys.stderr)
    return min(status["exit_code"], 255)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    args = parser.parse_args()
    try:
        return run(args.config)
    except Exception as exc:
        print("rank entry configuration failed: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
