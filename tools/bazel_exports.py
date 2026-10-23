#!/usr/bin/env python3
# Copyright 2026 The NATS Authors
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Refresh Unix export lists, or verify a shared library against its static build."""

import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def symbols(path, shared=False, nm="nm"):
    if sys.platform == "darwin":
        command = [nm, "-gU", str(path)]
    else:
        command = [nm, "--defined-only", "-g"]
        if shared:
            command.append("-D")
        command.append(str(path))
    output = subprocess.check_output(command, text=True)
    names = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and len(fields[-2]) == 1:
            name = fields[-1]
            names.add(name[1:] if sys.platform == "darwin" else name)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true", help="rewrite both checked-in export lists")
    parser.add_argument("--header", type=Path, default=ROOT / "src/nats.h", help="public API header")
    parser.add_argument("--nm", default="nm", help="symbol tool from the C/C++ toolchain")
    parser.add_argument("--linux-output", type=Path, help="write the Linux list to this build output")
    parser.add_argument("--macos-output", type=Path, help="write the macOS list to this build output")
    parser.add_argument("archive", type=Path, help="Bazel-built libcnats.a")
    parser.add_argument("shared", type=Path, nargs="?", help="libcnats.so or libcnats.dylib to verify")
    args = parser.parse_args()
    generate = args.linux_output is not None or args.macos_output is not None
    if generate and (args.linux_output is None or args.macos_output is None):
        parser.error("provide both --linux-output and --macos-output")
    if generate and args.update:
        parser.error("--update cannot be combined with build output paths")
    if not generate and not args.update and args.shared is None:
        parser.error("provide a shared library to verify, use --update, or provide build output paths")

    header = args.header.read_text(encoding="utf-8")
    header = re.sub(r"/\*.*?\*/|//[^\n]*", "", header, flags=re.S)
    declared = set(re.findall(r"^NATS_EXTERN\s+[^;]+?\b(\w+)\s*\(", header, re.M))
    expected = declared & symbols(args.archive, nm=args.nm)
    if not expected:
        raise SystemExit("No public API symbols found in the archive")
    names = sorted(expected)
    linux = "{\n    global:\n" + "".join(f"        {name};\n" for name in names) + "    local: *;\n};\n"
    macos = "".join(f"_{name}\n" for name in names)
    paths = (args.linux_output, args.macos_output) if generate else (ROOT / "cnats.lds", ROOT / "cnats.macos.exports")
    for path, contents in zip(paths, (linux, macos)):
        if args.update or generate:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8", newline="\n")
        elif path.read_text(encoding="utf-8") != contents:
            raise SystemExit(f"{path.name} is stale; run bazel run //tools:update_exports")
    if args.shared is not None:
        actual = symbols(args.shared, shared=True, nm=args.nm)
        if actual != expected:
            raise SystemExit(f"Shared API mismatch: missing={sorted(expected - actual)}, unexpected={sorted(actual - expected)}")
    print(f"Verified {len(expected)} public API symbols")


if __name__ == "__main__":
    main()
