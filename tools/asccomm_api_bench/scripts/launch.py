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

"""Build, run and report AICPU/CCU benchmarks, or launch a prepared MPICH backend."""

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import getpass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import uuid

from cluster import Cluster, Host, is_local_host, parse_hostfile


TOOL_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RUN_ROOT = TOOL_ROOT / "build_out" / "runs"
BACKEND_CACHE_ROOT = TOOL_ROOT / "build_out" / "backends"
ENVIRONMENT_KEYS = (
    "PATH",
    "LD_LIBRARY_PATH",
    "ASCEND_HOME_PATH",
    "ASCEND_CANN_PACKAGE_PATH",
    "ASCEND_OPP_PATH",
    "ASCEND_CUSTOM_OPP_PATH",
    "HCCL_DIR",
    "MPI_HOME",
    "MPI_PATH",
    "HCCL_SOCKET_IFNAME",
    "HCCL_SOCKET_FAMILY",
    "HCCL_TOPO_FILE_PATH",
    "HCCL_OP_RETRY_ENABLE",
    "ASCEND_SLOG_PRINT_TO_STDOUT",
    "ASCEND_GLOBAL_LOG_LEVEL",
    "ASCEND_RT_VISIBLE_DEVICES",
    "ASCEND_VISIBLE_DEVICES",
    "PROFILING_MODE",
    "PROFILING_OPTIONS",
)
REMOTE_PREPARE = """
import json, pathlib, sys
root, run_id = pathlib.Path(sys.argv[1]), sys.argv[2]
root.mkdir(parents=True, exist_ok=True)
owner = root / 'run_owner.json'
if owner.exists():
    if owner.is_symlink() or json.loads(owner.read_text()) != {'run_id': run_id}:
        raise SystemExit('run directory belongs to another run')
else:
    if any(root.iterdir()):
        raise SystemExit('refusing a nonempty unowned run directory')
    with owner.open('x') as stream:
        json.dump({'run_id': run_id}, stream)
"""
REMOTE_IDENTITY = """
import json, socket, sys
if sys.version_info < (3, 9):
    raise SystemExit('Python >=3.9 is required')
print(json.dumps({'hostname': socket.gethostname(), 'python': sys.version.split()[0]}))
"""


def log_benchmark_phase(args, phase):
    # Adapt PR37 28513c23's BENCHMARK_PHASE diagnostics to the existing
    # Python launcher stages; the execution and error-handling paths stay shared.
    if (
        args.native_backend
        and args.native_profile == "ccu"
        and not args.query_only
        and not args.dry_run
    ):
        args.benchmark_phase = phase
        print("Benchmark phase: " + phase, flush=True)


def log_benchmark_exit(args, status):
    phase = getattr(args, "benchmark_phase", None)
    if phase is not None:
        print(
            "Benchmark exit: status={} phase={} profile=ccu profiling={}".format(
                status,
                phase,
                "on" if args.native_profiling else "off",
            ),
            file=sys.stdout if status == 0 else sys.stderr,
            flush=True,
        )


def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp." + uuid.uuid4().hex)
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    os.replace(str(temporary), str(path))


def digest(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def tree_digest(root):
    rows = []
    for path in sorted(Path(root).rglob("*")):
        if path.is_symlink():
            raise ValueError("symlink in rank evidence: " + str(path))
        if path.is_file():
            rows.append((str(path.relative_to(root)), digest(path)))
        elif not path.is_dir():
            raise ValueError("unsupported rank evidence file: " + str(path))
    return hashlib.sha256(json.dumps(rows, separators=(",", ":")).encode()).hexdigest()


def positive_integer(value):
    if not value.isascii() or not value.isdecimal() or not 1 <= int(value) <= 2**31 - 1:
        raise argparse.ArgumentTypeError("expected a positive MPI integer")
    return int(value)


def positive_seconds(value):
    try:
        number = float(value)
    except ValueError:
        number = float("nan")
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("expected finite positive seconds")
    return number


class LaunchInterrupted(Exception):
    pass


def interrupted(signum, _frame):
    # Finish bounded process-group cleanup and evidence collection once.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    raise LaunchInterrupted("launcher interrupted by signal " + str(signum))


def parse_arguments(argv):
    argv = list(argv)
    if "--" in argv:
        index = argv.index("--")
        launcher_args, backend_args = argv[:index], argv[index + 1 :]
    else:
        launcher_args, backend_args = argv, []
    parser = argparse.ArgumentParser(
        prog="bash run_alg_bench.sh",
        usage="%(prog)s [options] --op OPERATOR",
        allow_abbrev=False,
        description="Build, run and report AscComm API benchmarks.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Test arguments (may be mixed with launch options):\n"
            "  --op allgather|allreduce|reducescatter|alltoall|alltoallv|alltoallvwrite (required)\n"
            "  -a aicpu_ts|ccu_sched (default: aicpu_ts)\n"
            "  -d TYPE (default: fp16; bfp16 aliases bf16)\n"
            "     AICPU: int8,int64,uint64,fp16,fp32,fp64,bf16; CCU: int8,fp16,fp32,bf16\n"
            "  -o sum|prod|max|min (default: sum; SDK support required)\n"
            "  -b BEGIN -e END (default: 32K to 256M)\n"
            "  -f FACTOR (default: 2) or -i BYTE_STEP; mutually exclusive\n"
            "  --alg 'complete-candidate-name' (repeatable; omit to test all candidates)\n\n"
            "Example:\n"
            "  bash run_alg_bench.sh -n 2 -p 2 --op allreduce -b 32K -e 32K"
        ),
    )
    parser.add_argument(
        "-n",
        "--ranks",
        type=positive_integer,
        default=8,
        help="total ranks (default: 8)",
    )
    parser.add_argument(
        "-p",
        "--devices-per-node",
        type=positive_integer,
        default=8,
        help="devices per node (default: 8)",
    )
    parser.add_argument(
        "--hostfile", type=Path, help="host list for multi-node execution"
    )
    parser.add_argument(
        "--query-only",
        action="store_true",
        help="query candidate algorithms without operator packages, measurements or profiling",
    )
    parser.add_argument("--mpirun", default="mpirun", help=argparse.SUPPRESS)
    parser.add_argument("--python", default="python3", help=argparse.SUPPRESS)
    parser.add_argument("--ssh-user", help=argparse.SUPPRESS)
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="new run directory; native mode allocates one under build_out/runs",
    )
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--timeout", type=positive_seconds, default=600, help=argparse.SUPPRESS
    )
    parser.add_argument(
        "--transport-timeout", type=positive_seconds, default=60, help=argparse.SUPPRESS
    )
    parser.add_argument("--dry-run", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--prepare-aicpu",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--prepare-ccu",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--ccu-package-source",
        type=Path,
        default=TOOL_ROOT / "custom_op_ccu",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--package-source",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "custom_op_aicpu",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--cann-root",
        type=Path,
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--vendor-root", type=Path, help=argparse.SUPPRESS)
    measurement = parser.add_mutually_exclusive_group()
    measurement.add_argument(
        "--measurement-mode", choices=("full", "comm_only"), help=argparse.SUPPRESS
    )
    measurement.add_argument(
        "--comm-only",
        action="store_const",
        dest="measurement_mode",
        const="comm_only",
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--ccu-probes", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--cmake", default="cmake", help=argparse.SUPPRESS)
    parser.add_argument("--makeself", help=argparse.SUPPRESS)
    parser.add_argument("--ascendc-util-dir", type=Path, help=argparse.SUPPRESS)
    parser.add_argument(
        "--package-build-timeout",
        type=positive_seconds,
        default=1800,
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--mpi-root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--hccl-root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument(
        "--query-library",
        type=Path,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--cxx11-abi",
        choices=("0", "1"),
        default=os.environ.get("MC2_BENCH_CXX11_ABI", "0"),
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--backend-extra-include",
        action="append",
        default=[],
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--backend-extra-library",
        action="append",
        default=[],
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--backend-build-timeout",
        type=positive_seconds,
        default=1800,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--analyze",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--no-profiling",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--records", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--msprof", help=argparse.SUPPRESS)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--native-backend",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    mode.add_argument(
        "--check-topology",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    mode.add_argument(
        "--executable",
        type=Path,
        help=argparse.SUPPRESS,
    )
    # Recognized launcher options may appear anywhere. Keep the old explicit
    # separator for external backends and archived commands; the remaining
    # arguments retain their order and go to the existing test parser.
    args, test_args = parser.parse_known_args(launcher_args)
    backend_args = test_args + backend_args
    args.native_backend = args.native_backend or not (
        args.check_topology or args.executable
    )
    # PR42's CCU --comm-only enables probes. AICPU retains its measurement mode.
    if "--comm-only" in launcher_args and backend_option(
        backend_args, "-a", "aicpu_ts"
    ) in {"ccu_sched", "ccu_ms"}:
        args.ccu_probes = True
        args.measurement_mode = "full"
    if args.measurement_mode is None:
        args.measurement_mode = (
            "comm_only"
            if args.native_backend
            and not args.query_only
            and backend_option(backend_args, "-a", "aicpu_ts") == "aicpu_ts"
            else "full"
        )
    args.native_profile = None
    if args.query_only and not args.native_backend:
        raise ValueError("--query-only requires the bundled query backend")
    args.native_profiling = (
        args.native_backend and not args.no_profiling and not args.query_only
    )
    if (
        args.native_profiling
        and backend_option(backend_args, "-a", "aicpu_ts") == "ccu_sched"
    ):
        args.ccu_probes = True
    if args.ccu_probes and not args.native_profiling:
        raise ValueError("CCU probes require a native CCU benchmark with profiling")
    if args.no_profiling and not args.native_backend:
        raise ValueError("--no-profiling requires the bundled native backend")
    if args.no_profiling and args.analyze:
        raise ValueError("--no-profiling cannot be combined with --analyze")
    if args.ranks % args.devices_per_node:
        raise ValueError("total ranks must be divisible by devices per node")
    if args.check_topology and backend_args:
        raise ValueError("topology checks do not accept backend arguments")
    for value in backend_args:
        if value in {"--ranks", "--devices-per-node"} or value.startswith(
            ("-n", "-p", "--ranks=", "--devices-per-node=")
        ):
            raise ValueError("backend arguments must not override launcher -n/-p")
    if args.ssh_user is not None and not re.fullmatch(
        r"[A-Za-z_][A-Za-z0-9_.-]*\$?", args.ssh_user
    ):
        raise ValueError("invalid SSH user")
    if args.output_dir is None:
        if not args.native_backend:
            raise ValueError(
                "--output-dir is required for topology checks and external backends"
            )
        args.output_dir = DEFAULT_RUN_ROOT / ("run-" + uuid.uuid4().hex)
    args.output_dir = args.output_dir.expanduser().resolve()
    if args.output_dir == Path(args.output_dir.anchor):
        raise ValueError("output directory must be a new run directory")
    if args.hostfile is not None:
        args.hostfile = args.hostfile.expanduser().resolve()
        hosts = parse_hostfile(args.hostfile, args.ranks, args.devices_per_node)
    elif args.ranks == args.devices_per_node:
        hosts = [Host("localhost", args.devices_per_node)]
    else:
        raise ValueError("--hostfile is required for multiple nodes")
    if not args.dry_run and not is_local_host(hosts[0].name):
        raise ValueError("the first hostfile entry must be the launch node")
    environment = {
        key: os.environ[key] for key in ENVIRONMENT_KEYS if key in os.environ
    }
    for setting in args.env:
        key, separator, value = setting.partition("=")
        if (
            not separator
            or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key)
            or "\0" in value
        ):
            raise ValueError("--env requires a valid KEY=VALUE")
        if key.startswith(
            ("PMI_", "PMIX_", "OMPI_COMM_WORLD_", "MPI_LOCAL", "MC2_BENCH_")
        ):
            raise ValueError("--env must not override MPI or rank evidence identity")
        environment[key] = value
    if args.native_backend:
        configure_native(args, environment, backend_args)
    if args.executable is not None:
        args.executable = args.executable.expanduser().resolve()
        if not args.executable.is_file() or not os.access(args.executable, os.X_OK):
            raise ValueError("--executable must be a prepared executable file")
    configure_migration(args, environment, backend_args)
    return args, hosts, environment, backend_args


def configure_native(args, environment, backend_args):
    """Resolve the bundled workflow without changing files or running tools."""
    if args.query_only and (
        args.prepare_aicpu
        or args.prepare_ccu
        or args.analyze
        or args.msprof
        or args.measurement_mode != "full"
        or args.vendor_root
    ):
        raise ValueError(
            "--query-only cannot prepare operator packages, analyze profiling, select a vendor or use --comm-only"
        )
    for key, value in (
        ("HCCL_OP_RETRY_ENABLE", "L0:0, L1:0, L2:0"),
        ("ASCEND_SLOG_PRINT_TO_STDOUT", "0"),
        ("ASCEND_GLOBAL_LOG_LEVEL", "3"),
    ):
        environment.setdefault(key, value)
    if args.records is not None:
        raise ValueError(
            "the bundled workflow manages its own rank-zero records; do not override --records"
        )
    environment["MC2_BENCH_NATIVE_PROFILING"] = "1" if args.native_profiling else "0"
    engine = backend_option(backend_args, "-a", "aicpu_ts")
    if engine not in {"aicpu_ts", "ccu_sched", "ccu_ms"}:
        raise ValueError("the bundled backend requires -a aicpu_ts|ccu_sched")
    args.native_profile = "aicpu" if engine == "aicpu_ts" else "ccu"
    if args.ccu_probes and engine != "ccu_sched":
        raise ValueError("CCU probes require -a ccu_sched")
    if args.native_profile == "ccu":
        # Match PR42's enabled configuration; keep the default deterministic
        # even when a caller or SDK setup script has enabled probes elsewhere.
        environment["PROFILING_MODE"] = "true" if args.ccu_probes else "false"
        environment["PROFILING_OPTIONS"] = (
            '{"task_time":"l1"}' if args.ccu_probes else ""
        )
    operator = backend_option(backend_args, "--op")
    if operator not in {
        "allgather",
        "allreduce",
        "reducescatter",
        "alltoall",
        "alltoallv",
        "alltoallvwrite",
    }:
        raise ValueError("the bundled backend requires an explicit supported --op")
    if operator == "alltoallvwrite" and args.native_profile != "ccu":
        raise ValueError("alltoallvwrite requires an explicit CCU engine")
    if not args.query_only:
        # Match the old benchmark scripts; tensor encoding and checking remain
        # in the reused legacy executor, not in this launcher.
        dtypes = (
            ("int8", "fp16", "fp32", "bf16")
            if args.native_profile == "ccu"
            else ("int8", "int64", "uint64", "fp16", "fp32", "fp64", "bf16")
        )
        dtype = backend_option(backend_args, "-d", "fp16")
        if ("bf16" if dtype == "bfp16" else dtype) not in dtypes:
            raise ValueError(
                "{} measurements support -d {} (bfp16 aliases bf16)".format(
                    args.native_profile.upper(), ",".join(dtypes)
                )
            )
    query_stub = backend_option(backend_args, "--query-stub")
    if query_stub is not None and query_stub not in {
        "basic",
        "by-size",
        "empty",
        "query-error",
        "rank-mismatch",
    }:
        raise ValueError(
            "--query-stub must be basic|by-size|empty|query-error|rank-mismatch"
        )
    args.query_source = "mock" if query_stub is not None else "native_mc2"
    if args.query_source == "mock" and args.query_library is not None:
        raise ValueError("--query-library cannot be combined with --query-stub")
    if backend_option(backend_args, "--records") is not None:
        raise ValueError(
            "the bundled workflow manages its own rank-zero records; do not override --records"
        )
    if args.query_only:
        for option in ("--warmup", "--iterations"):
            if backend_option(backend_args, option) is not None:
                raise ValueError("--query-only does not accept " + option)
    elif args.native_profile == "ccu":
        if args.measurement_mode != "full":
            raise ValueError("--comm-only is supported only with AICPU (-a aicpu_ts)")
        if args.prepare_aicpu:
            raise ValueError("CCU native mode cannot use --prepare-aicpu")
        args.prepare_ccu = True
    else:
        if args.prepare_ccu:
            raise ValueError("AICPU native mode cannot use --prepare-ccu")
        args.prepare_aicpu = True
    args.cann_root = (
        Path(
            args.cann_root
            or environment.get("ASCEND_HOME_PATH")
            or environment.get("ASCEND_CANN_PACKAGE_PATH")
            or "/usr/local/Ascend/cann"
        )
        .expanduser()
        .resolve()
    )
    mpi = args.mpi_root or environment.get("MPI_HOME") or environment.get("MPI_PATH")
    selected_mpirun = shutil.which(args.mpirun, path=environment.get("PATH"))
    if not mpi and selected_mpirun:
        mpi = Path(selected_mpirun).resolve().parent.parent
    if not mpi:
        mpi = next(
            (
                root
                for root in ("/usr/local/mpich", "/usr/local/mpi/mpich")
                if (Path(root) / "bin/mpirun").is_file()
            ),
            "/usr/local/mpich",
        )
    args.mpi_root = Path(mpi).expanduser().resolve()
    if args.mpirun == "mpirun":
        args.mpirun = str(args.mpi_root / "bin/mpirun")
    args.hccl_root = (
        Path(args.hccl_root or environment.get("HCCL_DIR") or args.cann_root)
        .expanduser()
        .resolve()
    )
    if args.query_source == "native_mc2":
        args.query_library = (
            (args.query_library or args.cann_root / "lib64/libmc2_client.so")
            .expanduser()
            .resolve()
        )
    environment["MPI_HOME"] = str(args.mpi_root)
    environment["HCCL_DIR"] = str(args.hccl_root)
    environment["PATH"] = (
        str(args.mpi_root / "bin") + os.pathsep + environment.get("PATH", os.defpath)
    )
    libraries = [
        args.hccl_root / "lib64",
        args.mpi_root / "lib",
        args.mpi_root / "lib64",
    ]
    if args.query_source == "native_mc2":
        libraries.insert(0, args.query_library.parent)
    environment["LD_LIBRARY_PATH"] = ":".join(map(str, libraries)) + (
        ":" + environment["LD_LIBRARY_PATH"]
        if environment.get("LD_LIBRARY_PATH")
        else ""
    )


def backend_option(argv, key, default=None):
    values = []
    for index, value in enumerate(argv):
        if value == key:
            if index + 1 == len(argv):
                raise ValueError("missing backend value for " + key)
            values.append(argv[index + 1])
        elif value.startswith(key + "="):
            values.append(value.split("=", 1)[1])
    if len(values) > 1:
        raise ValueError("duplicate backend option " + key)
    return values[0] if values else default


def configure_migration(args, environment, backend_args):
    if args.query_only:
        return
    if backend_option(backend_args, "--op") == "alltoallvwrite":
        if backend_option(backend_args, "-a", "aicpu_ts") not in {
            "ccu_sched",
            "ccu_ms",
        }:
            raise ValueError(
                "alltoallvwrite requires an explicit CCU engine (-a ccu_sched)"
            )
    if args.prepare_aicpu and args.prepare_ccu:
        raise ValueError("choose one operator package profile")
    if args.check_topology and (args.prepare_aicpu or args.prepare_ccu or args.analyze):
        raise ValueError("package preparation and profiling require --executable")
    if args.skip_build and not (args.prepare_aicpu or args.prepare_ccu):
        raise ValueError("--skip-build requires --prepare-aicpu or --prepare-ccu")
    if (args.records or args.msprof) and not (args.analyze or args.native_profiling):
        raise ValueError("--records/--msprof require --analyze")
    if args.measurement_mode == "comm_only" and not args.prepare_aicpu:
        raise ValueError("--comm-only requires AICPU package preparation")
    if args.records:
        args.records = args.records.expanduser().resolve()
    if not (args.prepare_aicpu or args.prepare_ccu):
        return
    ccu = args.prepare_ccu
    if backend_option(backend_args, "-a", "aicpu_ts") not in (
        {"ccu_sched", "ccu_ms"} if ccu else {"aicpu_ts"}
    ):
        raise ValueError(
            "--prepare-ccu requires backend -a ccu_sched"
            if ccu
            else "--prepare-aicpu requires backend -a aicpu_ts"
        )
    if ccu and args.measurement_mode != "full":
        raise ValueError("CCU package preparation requires --measurement-mode full")
    supported = {
        "allgather",
        "allreduce",
        "reducescatter",
        "alltoall",
        "alltoallv",
    }
    if ccu:
        supported.add("alltoallvwrite")
    if backend_option(backend_args, "--op") not in supported:
        raise ValueError(
            "package preparation requires an explicit supported backend --op"
        )
    cann = (
        args.cann_root
        or environment.get("ASCEND_HOME_PATH")
        or environment.get("ASCEND_CANN_PACKAGE_PATH")
    )
    if not cann:
        raise ValueError(
            "--prepare-aicpu requires --cann-root or an installed CANN environment"
        )
    args.cann_root = Path(cann).expanduser().resolve()
    vendor = (
        args.vendor_root
        or (environment.get("ASCEND_CUSTOM_OPP_PATH") if not ccu else None)
        or args.cann_root
        / "opp/vendors"
        / ("asccomm_api_bench_ccu" if ccu else "customize")
    )
    if ":" in str(vendor):
        raise ValueError("--vendor-root must select one vendor installation")
    args.vendor_root = Path(vendor).expanduser().resolve()
    if any(
        re.search(r"[\s*?\[\]]", str(path))
        for path in (args.cann_root, args.vendor_root)
    ):
        raise ValueError(
            "migrated SDK/install scripts require CANN and vendor roots without whitespace or glob characters"
        )
    vendor_name = "asccomm_api_bench_ccu" if ccu else "customize"
    if args.vendor_root.parts[-2:] != ("vendors", vendor_name):
        raise ValueError("--vendor-root must end with vendors/" + vendor_name)
    args.package_source = args.package_source.expanduser().resolve()
    expected = {
        "ASCEND_HOME_PATH": str(args.cann_root),
        "ASCEND_CANN_PACKAGE_PATH": str(args.cann_root),
        "ASCEND_OPP_PATH": str(args.cann_root / "opp"),
        "ASCEND_CUSTOM_OPP_PATH": str(args.vendor_root),
    }
    explicit_keys = {setting.partition("=")[0] for setting in args.env}
    for key, value in expected.items():
        if key in explicit_keys and environment.get(key) != value:
            raise ValueError("conflicting explicit --env " + key)
    environment.update(expected)
    libraries = [str(args.vendor_root / "op_api/lib"), str(args.cann_root / "lib64")]
    if environment.get("HCCL_DIR"):
        libraries.insert(1, str(Path(environment["HCCL_DIR"]) / "lib64"))
    if environment.get("LD_LIBRARY_PATH"):
        libraries.append(environment["LD_LIBRARY_PATH"])
    if args.native_backend and args.query_source == "native_mc2":
        query_directory = str(args.query_library.parent)
        if query_directory not in libraries:
            libraries.insert(0, query_directory)
    environment["LD_LIBRARY_PATH"] = ":".join(libraries)
    environment["MC2_BENCH_MEASUREMENT_MODE"] = args.measurement_mode
    environment["MC2_BENCH_PACKAGE_MARKER"] = str(
        args.vendor_root
        / (
            "asccomm_api_bench_ccu_package.info"
            if ccu
            else "asccomm_api_bench_package.info"
        )
    )


def prepare_aicpu(args, hosts, run_id, stack):
    from build_package import build_package
    from package_deploy import Deployment, make_config

    config = make_config(
        run_dir=args.output_dir,
        hosts=hosts,
        ssh_user=args.ssh_user or getpass.getuser(),
        cann_root=args.cann_root,
        vendor_root=args.vendor_root,
        measurement_mode=args.measurement_mode,
        run_id=run_id,
    )
    deployment = stack.enter_context(
        Deployment(config, verify_existing=args.skip_build)
    )
    if args.skip_build:
        if args.native_backend:
            print("Verifying installed AICPU package on every node...", flush=True)
        deployment.verify_existing()
    else:
        if args.native_backend:
            print(
                "Building AICPU package; log="
                + str(args.output_dir / "build/build.log"),
                flush=True,
            )
        installer = build_package(
            args.package_source,
            args.output_dir,
            args.cann_root,
            args.measurement_mode,
            cmake=args.cmake,
            makeself=args.makeself,
            ascendc_util_dir=args.ascendc_util_dir,
            timeout=args.package_build_timeout,
        )
        if args.native_backend:
            print("Installing AICPU package on every node...", flush=True)
        deployment.install(installer)
    if deployment.manifest.get("mpi_allowed") is not True:
        raise RuntimeError("AICPU package verification did not permit MPI execution")
    result = {
        "status": "verified",
        "skip_build": args.skip_build,
        "manifest": str(args.output_dir / "deploy/manifest.json"),
    }
    if args.native_backend:
        result["backend"] = prepare_native_backend(args)
    return result


def prepare_ccu(args, hosts, run_id, stack):
    from build_ccu_package import build_ccu_package
    from ccu_deploy import Deployment, make_config

    config = make_config(
        run_dir=args.output_dir,
        hosts=hosts,
        ssh_user=args.ssh_user or getpass.getuser(),
        cann_root=args.cann_root,
        vendor_root=args.vendor_root,
        measurement_mode=args.measurement_mode,
        run_id=run_id,
    )
    deployment = stack.enter_context(
        Deployment(config, verify_existing=args.skip_build)
    )
    if args.skip_build:
        print("Verifying installed CCU package on every node...", flush=True)
        deployment.verify_existing()
    else:
        print(
            "Building CCU package; log=" + str(args.output_dir / "ccu_build/build.log"),
            flush=True,
        )
        installer = build_ccu_package(
            args.ccu_package_source,
            args.output_dir,
            args.cann_root,
            cmake=args.cmake,
            makeself=args.makeself,
            ascendc_util_dir=args.ascendc_util_dir,
            timeout=args.package_build_timeout,
        )
        print("Installing CCU package on every node...", flush=True)
        deployment.install(installer)
    if deployment.manifest.get("mpi_allowed") is not True:
        raise RuntimeError("CCU package verification did not permit MPI execution")
    result = {
        "status": "verified",
        "skip_build": args.skip_build,
        "profile": "ccu",
        "manifest": str(args.output_dir / "deploy/manifest.json"),
    }
    if args.native_backend:
        result["backend"] = prepare_native_backend(args)
    return result


def prepare_native_backend(args):
    from build_backend import build_native_backend, verify_native_backend

    settings = {
        "cann_root": str(args.cann_root),
        "vendor_root": "" if args.query_only else str(args.vendor_root),
        "mpi_root": str(args.mpi_root),
        "hccl_root": str(args.hccl_root),
        "abi": args.cxx11_abi,
        "query_source": args.query_source,
        "profiling": args.native_profiling,
    }
    if args.query_only:
        settings["query_only"] = True
    if args.query_source == "native_mc2":
        settings["query_library"] = str(args.query_library)
    if args.native_profile == "ccu":
        settings["profile"] = "ccu"
    key = hashlib.sha256(json.dumps(settings, sort_keys=True).encode()).hexdigest()[:24]
    cache = BACKEND_CACHE_ROOT / key
    current = cache / "current.json"
    keywords = {
        "cmake": args.cmake,
        "abi": args.cxx11_abi,
        "hccl_root": args.hccl_root,
        "extra_includes": tuple(args.backend_extra_include),
        "extra_libraries": tuple(args.backend_extra_library),
        "query_source": args.query_source,
        "query_library": args.query_library,
        "profiling": args.native_profiling,
    }
    if args.query_only:
        keywords["query_only"] = True
    if args.native_profile == "ccu":
        keywords["profile"] = "ccu"
    if args.skip_build:
        if current.is_symlink() or not current.is_file():
            raise ValueError(
                "no verified native backend cache; rerun without --skip-build"
            )
        saved = json.loads(current.read_text(encoding="utf-8"))
        if (
            not isinstance(saved, dict)
            or saved.get("settings") != settings
            or not isinstance(saved.get("build_dir"), str)
        ):
            raise ValueError(
                "invalid native backend cache receipt; rerun without --skip-build"
            )
        build_dir = Path(saved["build_dir"])
        if build_dir.is_symlink() or build_dir.parent.resolve() != cache.resolve():
            raise ValueError("invalid native backend cache directory")
        print("Verifying native backend cache: " + str(build_dir), flush=True)
        backend = verify_native_backend(
            args.cann_root, args.vendor_root, args.mpi_root, build_dir, **keywords
        )
    else:
        build_dir = cache / ("build-" + uuid.uuid4().hex)
        print(
            "Building native backend; log=" + str(build_dir / "build.log"),
            flush=True,
        )
        backend = build_native_backend(
            args.cann_root,
            args.vendor_root,
            args.mpi_root,
            build_dir,
            timeout=args.backend_build_timeout,
            **keywords,
        )
    args.executable = Path(backend)
    if not args.executable.is_file() or not os.access(args.executable, os.X_OK):
        raise ValueError("native backend preparation did not produce an executable")
    if not args.skip_build:
        cache.mkdir(parents=True, exist_ok=True)
        atomic_json(current, {"build_dir": str(build_dir), "settings": settings})
    return {
        "status": "verified" if args.skip_build else "built",
        "build_dir": str(build_dir),
        "manifest": str(build_dir / "build_manifest.json"),
        "executable": str(args.executable),
    }


def render_native_report(args, summary, *, profiling_error=None):
    from console_report import print_native_report
    from report import query_policy_messages, read_records, write_report

    records = args.output_dir / "collected/ranks/rank_000000/records.jsonl"
    directory = args.output_dir / "report"
    if not records.is_file() and not summary["mpi_started"]:
        print("NO MEASUREMENTS: preparation failed before MPI execution.", flush=True)
        return {"status": "not_run", "directory": str(directory)}
    from profiling.algorithm_evidence import enrich

    values = read_records(records)
    algorithm_audit = enrich(values, args.output_dir)
    report_summary = write_report(values, directory)
    atomic_json(directory / "algorithm_audit.json", algorithm_audit)
    for message in query_policy_messages(report_summary):
        print(message, file=sys.stderr)
    report_summary.update(
        cluster_status=summary["status"], profiling_association_verified=False
    )
    if profiling_error is not None:
        report_summary.update(
            profiling_analysis_requested=True,
            profiling_analysis_error=profiling_error,
            native_event_records=str(records),
        )
    if summary["status"] != "PASS" or profiling_error is not None:
        report_summary["failed"] = True
    atomic_json(directory / "summary.json", report_summary)
    print_native_report(directory, report_summary)
    print("Report: " + str(directory / "report.csv"))
    return {
        "status": "FAIL" if report_summary["failed"] else "PASS",
        "directory": str(directory),
    }


def analyze_collected(args, summary):
    from profiling.pipeline import analyze_run
    from report import query_policy_messages, read_records, write_report

    records = (
        args.records or args.output_dir / "collected/ranks/rank_000000/records.jsonl"
    )
    msprof = args.msprof
    if args.native_backend and msprof is None:
        candidate = args.cann_root / "tools/profiler/bin/msprof"
        if candidate.is_file() and os.access(candidate, os.X_OK):
            msprof = str(candidate)
    result = analyze_run(
        args.output_dir,
        records,
        args.output_dir / "profile_analysis",
        msprof=msprof,
    )
    enriched = Path(result["enriched_records"])
    audit_path = Path(result["audit_path"])
    audit = json.loads(audit_path.read_text(encoding="utf-8"))
    actual_digest = digest(enriched)
    if (
        actual_digest != result["enriched_records_sha256"]
        or actual_digest != audit["enriched_records_sha256"]
    ):
        raise ValueError("profiling report input digest differs from parser audit")
    verified = (
        result["profiling_association_verified"] is True
        and audit["profiling_association_verified"] is True
    )
    report_summary = write_report(read_records(enriched), args.output_dir / "report")
    for message in query_policy_messages(report_summary):
        print(message, file=sys.stderr)
    report_summary.update(
        profiling_association_verified=verified,
        profiling_audit=str(audit_path),
        enriched_records_sha256=actual_digest,
        cluster_status=summary["status"],
    )
    if args.native_backend:
        report_summary.update(
            profiling_analysis_requested=True,
            native_event_records=str(records),
        )
    if (
        summary["status"] != "PASS"
        or result["returncode"]
        or (args.native_backend and report_summary["measured"] and not verified)
    ):
        report_summary["failed"] = True
    atomic_json(args.output_dir / "report/summary.json", report_summary)
    if args.native_backend:
        from console_report import print_native_report

        print_native_report(args.output_dir / "report", report_summary)
        print("Report: " + str(args.output_dir / "report/report.csv"))
    return {
        **result,
        "report_failed": report_summary["failed"],
        "profiling_association_verified": verified,
    }


def complete_native_report(args, summary):
    """Analyze successful native measurements once, retaining raw Event evidence."""
    if args.query_only:
        from query_report import write_query_report

        return write_query_report(args.output_dir, summary)
    from report import build_report, read_records

    records = args.output_dir / "collected/ranks/rank_000000/records.jsonl"
    if args.native_profiling and summary["mpi_started"]:
        # Empty candidate sets and failed operations have no successful device
        # submissions to profile. Report their original skip/failure evidence.
        preliminary = build_report(read_records(records))[2]
        if preliminary["measured"]:
            log_benchmark_phase(args, "profiling_analysis")
            print("Analyzing collected profiling...", flush=True)
            try:
                analysis = analyze_collected(args, summary)
            except Exception as exc:
                error = str(exc)
                summary["profiling_analysis"] = {"returncode": 1, "error": error}
                print(
                    "Profiling analysis failed: " + error,
                    file=sys.stderr,
                    flush=True,
                )
                # Do not lose valid ACL Event measurements when the analyzer
                # cannot publish a validated enriched report. The fallback is
                # explicitly failed and retains its original timing source.
                return render_native_report(args, summary, profiling_error=error)
            summary["profiling_analysis"] = analysis
            summary["profiling_association_verified"] = analysis[
                "profiling_association_verified"
            ]
            return {
                "status": "FAIL"
                if analysis["returncode"] or analysis["report_failed"]
                else "PASS",
                "directory": str(args.output_dir / "report"),
            }
        summary["profiling_analysis"] = {
            "returncode": 0,
            "status": "not_run",
            "reason": "no successful device measurements",
            "profiling_association_verified": False,
        }
    return render_native_report(args, summary)


def source_revision():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=str(Path(__file__).resolve().parents[1]),
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=5,
        ).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def mpi_command(args, payload):
    command = [args.mpirun]
    if args.hostfile is not None:
        command.extend(["-f", str(args.output_dir / "hosts")])
    command.extend(
        [
            "-n",
            str(args.ranks),
            args.python,
            str(payload / "rank_entry.py"),
            "--config",
            str(payload / "config.json"),
        ]
    )
    return command


def kill_job(process, grace=3):
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.poll()
        return
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


def execute_mpi(command, run_dir, timeout, *, environment=None):
    if environment is not None:
        from process_output import capture_process_output

        with capture_process_output(
            command,
            run_dir / "mpi_stdout.log",
            run_dir / "mpi_stderr.log",
            cwd=str(run_dir),
            env={**os.environ, **environment},
            echo_stdout=True,
            echo_stderr=True,
            cleanup=kill_job,
        ) as process:
            try:
                return process.wait(timeout=timeout), False
            except subprocess.TimeoutExpired:
                kill_job(process)
                return process.returncode, True
    process = None
    try:
        with (
            (run_dir / "mpi_stdout.log").open("xb") as stdout,
            (run_dir / "mpi_stderr.log").open("xb") as stderr,
        ):
            process = subprocess.Popen(
                command,
                cwd=str(run_dir),
                stdout=stdout,
                stderr=stderr,
                env={**os.environ, **environment} if environment is not None else None,
                start_new_session=True,
            )
            try:
                return process.wait(timeout=timeout), False
            except subprocess.TimeoutExpired:
                kill_job(process)
                return process.returncode, True
    finally:
        kill_job(process)


def audit_rank_artifacts(
    run_dir, sources, run_id, mode, ranks, devices_per_node, node_identities
):
    observed, successful, failed, errors, duplicates = {}, [], [], [], []
    process_ids = {}
    published = run_dir / "collected" / "ranks"
    published.mkdir(parents=True, exist_ok=True)
    for source_index, source in sources:
        for violation in sorted((source / "entry_errors").glob("*.json")):
            errors.append(
                "rank entry rejected on node %d: %s" % (source_index, violation.name)
            )
        for directory in sorted(source.glob("rank_*")):
            try:
                if directory.is_symlink() or not directory.is_dir():
                    raise ValueError("rank evidence must be a regular directory")
                match = re.fullmatch(r"rank_([0-9]{6,})", directory.name)
                if match is None:
                    raise ValueError("invalid rank directory name")
                rank = int(match.group(1))
                if not 0 <= rank < ranks:
                    raise ValueError("out-of-range rank directory")
                manifest = json.loads(
                    (directory / "manifest.json").read_text(encoding="utf-8")
                )
                status = json.loads(
                    (directory / "status.json").read_text(encoding="utf-8")
                )
                node_index = rank // devices_per_node
                expected = {
                    "schema_version": 1,
                    "run_id": run_id,
                    "mode": mode,
                    "global_rank": rank,
                    "world_size": ranks,
                    "local_rank": rank % devices_per_node,
                    "local_size": devices_per_node,
                    "node_index": node_index,
                    "logical_device": rank % devices_per_node,
                    "hostname": node_identities[node_index]["hostname"],
                }
                if any(
                    type(manifest.get(key)) is not type(value)
                    or manifest.get(key) != value
                    for key, value in expected.items()
                ):
                    raise ValueError(
                        "rank identity/layout does not match the launch plan"
                    )
                if type(manifest.get("pid")) is not int or manifest["pid"] < 1:
                    raise ValueError("invalid rank PID")
                for key in ("schema_version", "run_id", "mode", "global_rank"):
                    if (
                        type(status.get(key)) is not type(expected[key])
                        or status.get(key) != expected[key]
                    ):
                        raise ValueError("rank completion identity mismatch")
                if (
                    type(status.get("exit_code")) is not int
                    or not 0 <= status["exit_code"] <= 255
                ):
                    raise ValueError("invalid rank exit status")
                if status.get("pid") != manifest["pid"]:
                    raise ValueError("rank PID changed between entry and completion")
                if "backend_pid" not in manifest or "backend_pid" not in status:
                    raise ValueError("missing backend PID identity")
                backend_pid = manifest["backend_pid"]
                if (
                    type(status["backend_pid"]) is not type(backend_pid)
                    or status["backend_pid"] != backend_pid
                ):
                    raise ValueError("backend PID changed between entry and completion")
                if backend_pid is not None and (
                    type(backend_pid) is not int
                    or backend_pid < 1
                    or backend_pid == manifest["pid"]
                ):
                    raise ValueError("invalid backend PID identity")
                if mode == "topology" and backend_pid is not None:
                    raise ValueError(
                        "topology check unexpectedly records a backend PID"
                    )
                if (
                    mode == "backend"
                    and status["exit_code"] == 0
                    and backend_pid is None
                ):
                    raise ValueError(
                        "successful backend completion lacks its process PID"
                    )
                if status["exit_code"] == 0 and (
                    status.get("failure_stage") or status.get("error")
                ):
                    raise ValueError("successful completion carries a failure")
                if (
                    status["exit_code"] == 0
                    and mode == "backend"
                    and status.get("backend_returncode") != 0
                ):
                    raise ValueError(
                        "successful backend completion lacks a zero backend return code"
                    )
                fingerprint = tree_digest(directory)
                if rank in observed:
                    if observed[rank] != fingerprint:
                        raise ValueError("conflicting duplicate rank evidence")
                    duplicates.append({"rank": rank, "source_node": source_index})
                    continue
                identities = [(manifest["hostname"], manifest["pid"])]
                if backend_pid is not None:
                    identities.append((manifest["hostname"], backend_pid))
                if any(identity in process_ids for identity in identities):
                    raise ValueError("one process identity claims multiple ranks")
                for identity in identities:
                    process_ids[identity] = rank
                observed[rank] = fingerprint
                destination = published / directory.name
                shutil.move(str(directory), str(destination))
                if status["exit_code"] == 0:
                    successful.append(rank)
                else:
                    failed.append(
                        {
                            "rank": rank,
                            "exit_code": status["exit_code"],
                            "failure_stage": status.get("failure_stage", ""),
                            "error": status.get("error", ""),
                        }
                    )
            except (OSError, ValueError, KeyError, TypeError) as exc:
                errors.append("%s: %s" % (directory, exc))
    return {
        "observed_ranks": sorted(observed),
        "successful_ranks": sorted(successful),
        "failed_ranks": sorted(failed, key=lambda item: item["rank"]),
        "missing_ranks": sorted(set(range(ranks)) - set(observed)),
        "identity_errors": errors,
        "identical_shared_copies": duplicates,
    }


def launch(
    args,
    hosts,
    environment,
    backend_args,
    cluster_factory=Cluster,
    mpi_runner=execute_mpi,
):
    run_id = uuid.uuid4().hex
    mode = "topology" if args.check_topology else "backend"
    payload = args.output_dir / "payload"
    command = mpi_command(args, payload)
    plan = {
        "schema_version": 1,
        "run_id": run_id,
        "mode": mode,
        "ranks": args.ranks,
        "devices_per_node": args.devices_per_node,
        "hosts": [{"name": host.name, "slots": host.slots} for host in hosts],
        "output_dir": str(args.output_dir),
        "mpi_command": command,
        "backend_args": backend_args,
        "package_preparation": {
            "requested": args.prepare_aicpu or args.prepare_ccu,
            "profile": "ccu" if args.prepare_ccu else "aicpu",
            "skip_build": args.skip_build,
            "cann_root": str(args.cann_root) if args.cann_root else None,
            "vendor_root": str(args.vendor_root) if args.vendor_root else None,
            "measurement_mode": args.measurement_mode,
        },
        "profiling_analysis_requested": args.analyze or args.native_profiling,
        "native_backend": args.native_backend,
        "native_profile": args.native_profile,
        "query_only": args.query_only,
        "native_profiling": args.native_profiling,
        "console_output": args.native_backend,
        "environment_keys": sorted(environment),
        "timeout_seconds": args.timeout,
        "source_revision": None if args.dry_run else source_revision(),
        "created_utc": datetime.now(timezone.utc).isoformat(),
    }
    if args.native_profile == "ccu":
        from profiling.ccu_probe_markers import PROBE_GAPS, SOURCE_COMMIT

        plan["ccu_probes"] = {
            "requested": args.ccu_probes,
            "source_commit": SOURCE_COMMIT,
            "source_pr": 42,
            "default_gaps": PROBE_GAPS,
            "profiling_mode": environment["PROFILING_MODE"],
            "profiling_options": environment["PROFILING_OPTIONS"],
        }
    if getattr(args, "retry_context", None):
        plan["retry_context"] = args.retry_context
    if args.dry_run:
        print(json.dumps(plan, ensure_ascii=False, indent=2, sort_keys=True))
        return 0
    log_benchmark_phase(args, "environment_setup")
    mpirun = shutil.which(
        args.mpirun, path=environment.get("PATH") if args.native_backend else None
    )
    if mpirun is None:
        raise ValueError("MPI launcher not found: " + args.mpirun)
    command[0] = (
        str(Path(mpirun).absolute())
        if args.native_backend
        else str(Path(mpirun).resolve())
    )
    args.output_dir.mkdir(parents=True, exist_ok=False)
    if args.native_backend:
        print("Run directory: " + str(args.output_dir), flush=True)
    atomic_json(args.output_dir / "run_owner.json", {"run_id": run_id})
    atomic_json(args.output_dir / "launch_manifest.json", plan)
    payload.mkdir()
    shutil.copy2(
        str(Path(__file__).with_name("rank_entry.py")), str(payload / "rank_entry.py")
    )
    if args.native_backend:
        shutil.copy2(
            str(Path(__file__).with_name("process_output.py")),
            str(payload / "process_output.py"),
        )
    executable = ""
    if args.executable is not None:
        shutil.copy2(str(args.executable), str(payload / "backend"))
        executable = str(payload / "backend")
    config = {
        "schema_version": 1,
        "run_id": run_id,
        "mode": mode,
        "run_dir": str(args.output_dir),
        "ranks": args.ranks,
        "devices_per_node": args.devices_per_node,
        "environment": environment,
        "executable": executable,
        "backend_args": backend_args,
        "backend_timeout": args.timeout,
        "console_output": args.native_backend,
        "cann_environment": {
            "cann_root": str(args.cann_root),
            "vendor_root": "" if args.query_only else str(args.vendor_root),
        }
        if args.prepare_aicpu or args.prepare_ccu or args.query_only
        else None,
    }
    if (
        args.native_backend
        and args.query_source == "native_mc2"
        and args.query_library.parent
        not in (args.cann_root / "lib64", args.hccl_root / "lib64")
    ):
        config["cann_environment"]["query_library_dir"] = str(args.query_library.parent)
    atomic_json(payload / "config.json", config)
    fingerprints = {path.name: digest(path) for path in sorted(payload.iterdir())}
    atomic_json(
        payload / "payload.json",
        {"schema_version": 1, "run_id": run_id, "files": fingerprints},
    )
    plan["payload_files"] = fingerprints
    plan["payload_sha256"] = digest(payload / "payload.json")
    atomic_json(args.output_dir / "launch_manifest.json", plan)
    if args.hostfile is not None:
        (args.output_dir / "hosts").write_text(
            "".join(host.name + ":" + str(host.slots) + "\n" for host in hosts),
            encoding="utf-8",
        )
    errors, sources, node_identities = [], [], {}
    mpi_returncode, timed_out, mpi_started = None, False, False
    transport = None
    stack = ExitStack()
    preparation = {"status": "not_requested"}
    stage = "transport"
    failure_info = (None, None, None)
    try:
        log_benchmark_phase(args, stage)
        transport = cluster_factory(
            hosts,
            ssh_user=args.ssh_user,
            timeout=args.transport_timeout,
            log_dir=args.output_dir / "transport_logs",
        )
        for index in range(len(hosts)):
            identity_result = transport.run(index, [args.python, "-c", REMOTE_IDENTITY])
            identity = json.loads(identity_result.stdout)
            if (
                not isinstance(identity.get("hostname"), str)
                or not identity["hostname"]
            ):
                raise ValueError("node did not report a valid hostname")
            if identity["hostname"] in {
                value["hostname"] for value in node_identities.values()
            }:
                raise ValueError("different hostfile entries resolve to the same node")
            node_identities[index] = identity
            transport.run(
                index, [args.python, "-c", REMOTE_PREPARE, str(args.output_dir), run_id]
            )
        if args.prepare_aicpu or args.prepare_ccu:
            stage = "package_preparation"
            log_benchmark_phase(args, stage)
            preparation = {"status": "preparing"}
            preparation = (
                prepare_ccu(args, hosts, run_id, stack)
                if args.prepare_ccu
                else prepare_aicpu(args, hosts, run_id, stack)
            )
            plan["package_preparation"].update(preparation)
        if args.query_only:
            stage = "query_backend_preparation"
            plan["query_backend"] = prepare_native_backend(args)
        if args.native_backend:
            # The generated ACLNN headers are available only after installing
            # the package. Publish the native binary after its locked build.
            shutil.copy2(str(args.executable), str(payload / "backend"))
            config["executable"] = str(payload / "backend")
            atomic_json(payload / "config.json", config)
            fingerprints = {
                path.name: digest(path)
                for path in sorted(payload.iterdir())
                if path.name != "payload.json"
            }
            atomic_json(
                payload / "payload.json",
                {"schema_version": 1, "run_id": run_id, "files": fingerprints},
            )
            plan["payload_files"] = fingerprints
            plan["payload_sha256"] = digest(payload / "payload.json")
        stage = "payload_deployment"
        log_benchmark_phase(args, stage)
        for index in range(len(hosts)):
            transport.deploy(index, payload, payload)
        plan["node_identities"] = node_identities
        atomic_json(args.output_dir / "launch_manifest.json", plan)
        mpi_started = True
        stage = "mpi"
        log_benchmark_phase(args, stage)
        if args.native_backend:
            print(
                "Running MPI topology query..."
                if args.query_only
                else "Running MPI benchmark...",
                flush=True,
            )
            mpi_returncode, timed_out = mpi_runner(
                command, args.output_dir, args.timeout, environment=environment
            )
        else:
            mpi_returncode, timed_out = mpi_runner(
                command, args.output_dir, args.timeout
            )
    except Exception as exc:
        failure_info = (type(exc), exc, exc.__traceback__)
        errors.append(stage + ": " + str(exc))
        if args.native_backend:
            print(
                ("Query failed at " if args.query_only else "Benchmark failed at ")
                + stage
                + ": "
                + str(exc),
                file=sys.stderr,
                flush=True,
            )
        if stage == "package_preparation":
            preparation = {"status": "failed", "error": str(exc)}
    except KeyboardInterrupt as exc:
        failure_info = (type(exc), exc, exc.__traceback__)
        errors.append("launcher interrupted")
    if mpi_started:
        log_benchmark_phase(args, "collection")
        for index in range(len(hosts)):
            target = args.output_dir / "staging" / ("node_%d" % index) / "ranks"
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                transport.collect(index, args.output_dir / "ranks", target)
                sources.append((index, target))
            except Exception as exc:
                errors.append("collect node %d: %s" % (index, exc))
    try:
        audit = audit_rank_artifacts(
            args.output_dir,
            sources,
            run_id,
            mode,
            args.ranks,
            args.devices_per_node,
            node_identities,
        )
    finally:
        try:
            log_benchmark_phase(args, "package_release")
            stack.__exit__(*failure_info)
        except Exception as exc:
            errors.append("package_release: " + str(exc))
    passed = (
        mpi_started
        and mpi_returncode == 0
        and not timed_out
        and not errors
        and not audit["missing_ranks"]
        and not audit["failed_ranks"]
        and not audit["identity_errors"]
        and len(audit["successful_ranks"]) == args.ranks
    )
    summary = {
        "schema_version": 1,
        "run_id": run_id,
        "mode": mode,
        "status": "PASS" if passed else "FAIL",
        "audit_scope": "rank_process_completion",
        "expected_ranks": args.ranks,
        "expected_nodes": len(hosts),
        "mpi_started": mpi_started,
        "mpi_returncode": mpi_returncode,
        "mpi_timed_out": timed_out,
        "transport_errors": errors,
        "device_execution_verified": False,
        "profiling_association_verified": False,
        "package_preparation": preparation,
        **audit,
    }
    atomic_json(args.output_dir / "cluster_summary.json", summary)
    if args.native_backend:
        try:
            log_benchmark_phase(args, "report_generation")
            summary["report"] = complete_native_report(args, summary)
            if (
                passed
                and args.native_profile == "ccu"
                and args.native_profiling
                and args.ccu_probes
                and summary.get("profiling_analysis", {}).get("enriched_records")
                and not getattr(args, "ccu_probe_retry_child", False)
            ):
                from ccu_probe_retry import retry_missing_probes

                def run_child(child_args, child_backend_args):
                    return launch(
                        child_args,
                        hosts,
                        dict(environment),
                        child_backend_args,
                        cluster_factory=cluster_factory,
                        mpi_runner=mpi_runner,
                    )

                retries = retry_missing_probes(args, summary, backend_args, run_child)
                if retries:
                    summary["ccu_probe_retries"] = {
                        key: value
                        for key, value in retries.items()
                        if key not in {"report", "analysis"}
                    }
                    if "report" in retries:
                        summary["report"] = retries["report"]
                        summary["profiling_analysis"] = retries["analysis"]
                        summary["profiling_association_verified"] = retries[
                            "profiling_association_verified"
                        ]
            passed = passed and summary["report"]["status"] == "PASS"
        except Exception as exc:
            print(
                (
                    "Query report unavailable: "
                    if args.query_only
                    else "Performance report unavailable: "
                )
                + str(exc),
                file=sys.stderr,
                flush=True,
            )
            summary["report"] = {
                "status": "FAIL",
                "directory": str(args.output_dir / "report"),
                "error": str(exc),
            }
            passed = False
        summary["status"] = "PASS" if passed else "FAIL"
        atomic_json(args.output_dir / "cluster_summary.json", summary)
    if args.analyze and not args.native_backend:
        try:
            analysis = analyze_collected(args, summary)
            summary["profiling_analysis"] = analysis
            summary["profiling_association_verified"] = analysis[
                "profiling_association_verified"
            ]
            passed = (
                passed and analysis["returncode"] == 0 and not analysis["report_failed"]
            )
        except Exception as exc:
            summary["profiling_analysis"] = {"returncode": 1, "error": str(exc)}
            passed = False
        summary["status"] = "PASS" if passed else "FAIL"
        atomic_json(args.output_dir / "cluster_summary.json", summary)
    print(
        "Cluster %s: %d/%d successful ranks; summary=%s"
        % (
            summary["status"],
            len(audit["successful_ranks"]),
            args.ranks,
            args.output_dir / "cluster_summary.json",
        )
    )
    if args.native_backend and not passed:
        for failure in audit["failed_ranks"]:
            print(
                "Rank %d failed: stage=%s exit=%s; log=%s"
                % (
                    failure["rank"],
                    failure["failure_stage"],
                    failure["exit_code"],
                    args.output_dir
                    / "collected/ranks"
                    / ("rank_%06d" % failure["rank"])
                    / "stderr.log",
                ),
                file=sys.stderr,
                flush=True,
            )
        if audit["missing_ranks"]:
            print(
                "Missing ranks: " + ", ".join(map(str, audit["missing_ranks"])),
                file=sys.stderr,
                flush=True,
            )
        if timed_out:
            print(
                "MPI benchmark timed out; overall job failed. Check the report and logs for completed entries.",
                file=sys.stderr,
                flush=True,
            )
        print(
            "Failure details: " + str(args.output_dir / "cluster_summary.json"),
            file=sys.stderr,
            flush=True,
        )
    log_benchmark_phase(args, "finished")
    return 0 if passed else 1


def main(argv=None):
    previous = {
        signum: signal.signal(signum, interrupted)
        for signum in (signal.SIGTERM, signal.SIGINT)
    }
    args, exit_code = None, 1
    try:
        parsed = parse_arguments(sys.argv[1:] if argv is None else argv)
        args = parsed[0]
        exit_code = launch(*parsed)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print("launch failed: " + str(exc), file=sys.stderr)
        exit_code = 2
    except LaunchInterrupted as exc:
        print(str(exc), file=sys.stderr)
        exit_code = 1
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)
        if args is not None:
            log_benchmark_exit(args, exit_code)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
