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

"""Bounded local/SSH commands and verified, immutable benchmark file transfers.

This module transports one run's payload and node-local artifacts. It never
installs an SDK/operator package, changes SSH configuration, or deletes source
data. MPI launch policy and the run identifier belong to the caller.
"""

from dataclasses import dataclass
import inspect
import itertools
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import socket
import subprocess
import tempfile


_HOST = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\Z")
_USER = re.compile(r"[A-Za-z_][A-Za-z0-9_.-]*\$?\Z")
_ENVIRONMENT_KEY = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
_MPI_MAX = 2**31 - 1


class ClusterError(RuntimeError):
    """A local or remote command/transfer did not complete successfully."""


@dataclass(frozen=True)
class Host:
    name: str
    slots: int


def _validate_host(name):
    if not isinstance(name, str) or not _HOST.fullmatch(name):
        raise ValueError(
            "host must be an IPv4 address or plain hostname: " + repr(name)
        )


def _positive_integer(value, field):
    if type(value) is not int or not 0 < value <= _MPI_MAX:
        raise ValueError(field + " must be a positive MPI integer")


def parse_hostfile(path, ranks, devices_per_node):
    """Read a symmetric hostfile; no DNS/SSH connection is made here.

    Accepted lines are ``host:slots``, ``host slots=N``, or ``host`` (which
    means devices_per_node slots), with optional trailing ``#`` comments.
    IPv4 addresses and plain hostnames are accepted; IPv6 literals are not.
    """
    _positive_integer(ranks, "ranks")
    _positive_integer(devices_per_node, "devices_per_node")
    if ranks % devices_per_node:
        raise ValueError("ranks must be divisible by devices_per_node")
    hosts = []
    seen = set()
    for number, raw in enumerate(
        Path(path).read_text(encoding="utf-8").splitlines(), 1
    ):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        match = re.fullmatch(
            r"([A-Za-z0-9][A-Za-z0-9_.-]*)(?::([0-9]+)|[ \t]+slots=([0-9]+))?",
            line,
        )
        if match is None:
            raise ValueError("invalid hostfile line {}: {!r}".format(number, raw))
        name = match.group(1)
        key = name.rstrip(".").casefold()
        if key in seen:
            raise ValueError("duplicate hostfile host: " + name)
        seen.add(key)
        text = match.group(2) or match.group(3)
        if text is not None and len(text.lstrip("0")) > 10:
            raise ValueError("hostfile slot count exceeds MPI integer")
        slots = int(text) if text is not None else devices_per_node
        if slots != devices_per_node:
            raise ValueError("hostfile slots must equal devices_per_node")
        hosts.append(Host(name, slots))
    if not hosts:
        raise ValueError("hostfile contains no hosts")
    if sum(host.slots for host in hosts) != ranks:
        raise ValueError("hostfile slot total must equal ranks")
    return hosts


def _addresses(name):
    try:
        return {item[4][0].split("%", 1)[0] for item in socket.getaddrinfo(name, None)}
    except (OSError, UnicodeError):
        return set()


def is_local_host(name):
    """Recognize localhost, this machine's hostname, and its resolved addresses."""
    _validate_host(name)
    key = name.rstrip(".").casefold()
    if key in {"localhost", "127.0.0.1"}:
        return True
    local_names = {socket.gethostname(), socket.getfqdn()}
    if key in {item.rstrip(".").casefold() for item in local_names}:
        return True
    local_addresses = {"127.0.0.1", "::1"}
    for item in local_names:
        local_addresses.update(_addresses(item))
    return bool(local_addresses.intersection(_addresses(name)))


def _tree_manifest(path):
    # This function is also serialized into the remote Python command. Keep it
    # self-contained, using only the standard library and built-in exceptions.
    import hashlib
    import os
    from pathlib import Path
    import stat

    root = Path(path)
    if not root.exists() and not root.is_symlink():
        return None
    if root.is_symlink() or not root.is_dir():
        raise ValueError("transfer root is not an ordinary directory: " + str(root))
    result = {"files": {}, "directories": []}

    def fail(error):
        raise error

    for parent, directories, files in os.walk(
        str(root), onerror=fail, followlinks=False
    ):
        directories.sort()
        files.sort()
        for name in directories:
            candidate = Path(parent) / name
            if candidate.is_symlink() or not stat.S_ISDIR(candidate.lstat().st_mode):
                raise ValueError(
                    "transfer contains a linked/special directory: " + str(candidate)
                )
            result["directories"].append(candidate.relative_to(root).as_posix())
        for name in files:
            candidate = Path(parent) / name
            metadata = candidate.lstat()
            if not stat.S_ISREG(metadata.st_mode):
                raise ValueError(
                    "transfer contains a linked/special file: " + str(candidate)
                )
            digest = hashlib.sha256()
            flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
            descriptor = os.open(str(candidate), flags)
            with os.fdopen(descriptor, "rb") as stream:
                opened = os.fstat(stream.fileno())
                if not stat.S_ISREG(opened.st_mode):
                    raise ValueError("transfer file changed type: " + str(candidate))
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            result["files"][candidate.relative_to(root).as_posix()] = {
                "sha256": digest.hexdigest(),
                "mode": stat.S_IMODE(metadata.st_mode),
            }
    return result


def _publish_tree(staging, destination, expected):
    # Every publisher uses this destination-specific lock. Existing directories
    # are reusable only when the complete manifest agrees, never overwritten.
    import os
    from pathlib import Path
    import shutil

    staging = Path(staging)
    destination = Path(destination)
    lock = destination.parent / ("." + destination.name + ".mc2-transfer-lock")
    lock.mkdir()
    try:
        present = _tree_manifest(destination)
        if present is not None:
            if present != expected:
                raise FileExistsError(
                    "refusing to replace existing transfer destination: "
                    + str(destination)
                )
            shutil.rmtree(str(staging))
            return
        if _tree_manifest(staging) != expected:
            raise ValueError("staged transfer SHA256/mode verification failed")
        os.rename(str(staging), str(destination))
    finally:
        lock.rmdir()


_TREE_SCRIPT = (
    inspect.getsource(_tree_manifest)
    + "\nimport json, sys\nprint(json.dumps(_tree_manifest(sys.argv[1]), sort_keys=True))\n"
)
_PUBLISH_SCRIPT = (
    inspect.getsource(_tree_manifest)
    + "\n"
    + inspect.getsource(_publish_tree)
    + "\nimport json, sys\n_publish_tree(sys.argv[1], sys.argv[2], json.loads(sys.argv[3]))\n"
)
_STAGING_SCRIPT = """import json, sys, tempfile
from pathlib import Path
destination = Path(sys.argv[1])
destination.parent.mkdir(parents=True, exist_ok=True)
staging = tempfile.mkdtemp(prefix='.' + destination.name + '.mc2-transfer-', dir=str(destination.parent))
print(json.dumps(staging))
"""


class Cluster:
    def __init__(self, hosts, ssh_user=None, timeout=60, log_dir=None):
        if not hosts:
            raise ValueError("cluster must contain at least one host")
        self.hosts = tuple(hosts)
        for host in self.hosts:
            _validate_host(host.name)
            _positive_integer(host.slots, "host slots")
        if len({host.name.rstrip(".").casefold() for host in self.hosts}) != len(
            self.hosts
        ):
            raise ValueError("cluster contains duplicate hosts")
        if ssh_user is not None and not _USER.fullmatch(ssh_user):
            raise ValueError("invalid SSH user")
        if (
            isinstance(timeout, bool)
            or not isinstance(timeout, (int, float))
            or not math.isfinite(timeout)
            or timeout <= 0
        ):
            raise ValueError("command timeout must be finite and positive")
        self.ssh_user = ssh_user
        self.timeout = timeout
        self.log_dir = (
            Path(log_dir) if log_dir is not None else Path.cwd() / "cluster_logs"
        )
        self._sequence = itertools.count(1)

    def _host(self, index):
        if type(index) is not int or not 0 <= index < len(self.hosts):
            raise ValueError("node index is outside the cluster")
        return self.hosts[index]

    def _endpoint(self, index):
        host = self._host(index).name
        return (self.ssh_user + "@" if self.ssh_user else "") + host

    def _execute(self, index, command, *, cwd=None, env=None, description=None):
        host = self._host(index)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        log_path = self.log_dir / "node_{:03d}.command_{:06d}.log".format(
            index, next(self._sequence)
        )
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            log_path.write_text(
                json.dumps(description or {}, ensure_ascii=False)
                + "\n"
                + str(error)
                + "\n",
                encoding="utf-8",
            )
            raise ClusterError(
                "command failed on {} (see {}): {}".format(host.name, log_path, error)
            ) from error
        log_path.write_text(
            json.dumps(description or {}, ensure_ascii=False)
            + "\nreturncode="
            + str(result.returncode)
            + "\nstdout:\n"
            + result.stdout
            + "\nstderr:\n"
            + result.stderr,
            encoding="utf-8",
        )
        if result.returncode:
            raise ClusterError(
                "command exited {} on {} (see {})".format(
                    result.returncode, host.name, log_path
                )
            )
        return result

    def run(self, index, argv, cwd=None, env=None):
        """Run one argv without shell interpolation; raise on errors or timeout.

        Remote commands are quoted once for the login shell used by SSH. Only
        names of explicit environment overrides, not their values, enter the
        command metadata log. Existing process environment is not logged.
        """
        host = self._host(index)
        arguments = [str(item) for item in argv]
        if not arguments or any("\0" in item for item in arguments):
            raise ValueError("command argv must be nonempty and contain no NUL")
        directory = str(cwd) if cwd is not None else None
        if directory is not None and "\0" in directory:
            raise ValueError("working directory contains NUL")
        overrides = dict(env or {})
        for key, value in overrides.items():
            if not isinstance(key, str) or not _ENVIRONMENT_KEY.fullmatch(key):
                raise ValueError("invalid environment name: " + repr(key))
            if not isinstance(value, str) or "\0" in value:
                raise ValueError("environment values must be strings without NUL")
        description = {
            "node": host.name,
            "argv": arguments,
            "cwd": directory,
            "environment_keys": sorted(overrides),
        }
        if is_local_host(host.name):
            local_env = os.environ.copy()
            local_env.update(overrides)
            return self._execute(
                index, arguments, cwd=directory, env=local_env, description=description
            )
        remote_argv = (
            ["env", "--"]
            + [key + "=" + value for key, value in sorted(overrides.items())]
            + arguments
        )
        remote_command = "exec " + shlex.join(remote_argv)
        if directory is not None:
            remote_command = "cd -- " + shlex.quote(directory) + " && " + remote_command
        command = [
            "ssh",
            "-o",
            "BatchMode=yes",
            "-o",
            "ConnectTimeout=10",
            self._endpoint(index),
            remote_command,
        ]
        return self._execute(index, command, description=description)

    def _scan(self, index, path):
        output = self.run(index, ["python3", "-c", _TREE_SCRIPT, str(path)]).stdout
        try:
            manifest = json.loads(output)
        except (ValueError, TypeError) as error:
            raise ClusterError("node returned an invalid transfer manifest") from error
        if manifest is not None and (
            not isinstance(manifest, dict) or set(manifest) != {"files", "directories"}
        ):
            raise ClusterError("node returned an invalid transfer manifest")
        return manifest

    @staticmethod
    def _absolute(path):
        path = Path(os.path.abspath(os.fspath(path)))
        if "\0" in str(path) or path == Path(path.anchor):
            raise ValueError("transfer path must identify a non-root directory")
        return path

    @staticmethod
    def _check_destination(source, destination):
        # Shared filesystems can expose a source through a linked ancestor.
        # Staging inside that source would recursively copy the transfer itself.
        if (
            source in destination.parents
            or source.resolve() in destination.resolve().parents
        ):
            raise ValueError(
                "transfer destination must not be inside its source directory"
            )

    def deploy(self, index, source_dir, destination_dir):
        """Publish an immutable payload after file SHA256/mode verification.

        The caller owns its run/config sentinel. Different existing trees are
        rejected, while an identical payload on a shared filesystem is reused.
        """
        source = self._absolute(source_dir)
        destination = self._absolute(destination_dir)
        self._check_destination(source, destination)
        try:
            expected = _tree_manifest(source)
        except (OSError, ValueError) as error:
            raise ClusterError("invalid deployment source: " + str(error)) from error
        if expected is None:
            raise ClusterError("deployment source does not exist: " + str(source))
        present = self._scan(index, destination)
        if present is not None:
            if present != expected:
                raise ClusterError(
                    "refusing to replace a different existing payload: "
                    + str(destination)
                )
            return expected
        if is_local_host(self._host(index).name):
            try:
                destination.parent.mkdir(parents=True, exist_ok=True)
                staging = (
                    Path(
                        tempfile.mkdtemp(
                            prefix="." + destination.name + ".mc2-transfer-",
                            dir=str(destination.parent),
                        )
                    )
                    / "tree"
                )
                shutil.copytree(str(source), str(staging))
                _publish_tree(staging, destination, expected)
                staging.parent.rmdir()
            except (OSError, ValueError) as error:
                raise ClusterError(
                    "payload publication failed: " + str(error)
                ) from error
            return expected
        output = self.run(
            index, ["python3", "-c", _STAGING_SCRIPT, str(destination)]
        ).stdout
        try:
            staging = Path(json.loads(output))
        except (ValueError, TypeError) as error:
            raise ClusterError("node returned an invalid staging path") from error
        if staging.parent != destination.parent or not staging.name.startswith(
            "." + destination.name + ".mc2-transfer-"
        ):
            raise ClusterError(
                "node returned a staging path outside the destination parent"
            )
        command = [
            "rsync",
            "-a",
            "--protect-args",
            "-e",
            "ssh -o BatchMode=yes -o ConnectTimeout=10",
            "--",
            str(source) + "/",
            self._endpoint(index) + ":" + str(staging) + "/",
        ]
        self._execute(
            index,
            command,
            description={
                "node": self._host(index).name,
                "action": "deploy",
                "source": str(source),
                "staging": str(staging),
            },
        )
        if self._scan(index, staging) != expected:
            raise ClusterError("deployed payload SHA256/mode verification failed")
        self.run(
            index,
            [
                "python3",
                "-c",
                _PUBLISH_SCRIPT,
                str(staging),
                str(destination),
                json.dumps(expected, sort_keys=True),
            ],
        )
        return expected

    def collect(self, index, source_dir, destination_dir):
        """Collect a node tree into an immutable local staging destination.

        Sources remain untouched. A shared-filesystem tree already present at
        destination is accepted only if its entire manifest is identical.
        """
        source = self._absolute(source_dir)
        destination = self._absolute(destination_dir)
        self._check_destination(source, destination)
        expected = self._scan(index, source)
        if expected is None:
            raise ClusterError("node artifact directory does not exist: " + str(source))
        try:
            present = _tree_manifest(destination)
        except (OSError, ValueError) as error:
            raise ClusterError(
                "invalid collection destination: " + str(error)
            ) from error
        if present is not None:
            if present != expected:
                raise ClusterError(
                    "refusing to replace different collected artifacts: "
                    + str(destination)
                )
            return expected
        try:
            destination.parent.mkdir(parents=True, exist_ok=True)
            staging_parent = Path(
                tempfile.mkdtemp(
                    prefix="." + destination.name + ".mc2-transfer-",
                    dir=str(destination.parent),
                )
            )
        except OSError as error:
            raise ClusterError(
                "cannot create collection staging: " + str(error)
            ) from error
        staging = staging_parent / "tree"
        if is_local_host(self._host(index).name):
            try:
                shutil.copytree(str(source), str(staging))
            except OSError as error:
                raise ClusterError(
                    "cannot copy local artifacts: " + str(error)
                ) from error
        else:
            staging.mkdir()
            command = [
                "rsync",
                "-a",
                "--protect-args",
                "-e",
                "ssh -o BatchMode=yes -o ConnectTimeout=10",
                "--",
                self._endpoint(index) + ":" + str(source) + "/",
                str(staging) + "/",
            ]
            self._execute(
                index,
                command,
                description={
                    "node": self._host(index).name,
                    "action": "collect",
                    "source": str(source),
                    "staging": str(staging),
                },
            )
        try:
            _publish_tree(staging, destination, expected)
            staging_parent.rmdir()
        except (OSError, ValueError) as error:
            raise ClusterError(
                "collected artifacts SHA256/mode verification failed: " + str(error)
            ) from error
        return expected
