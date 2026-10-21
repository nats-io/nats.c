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


def symbols(path, shared=False):
    if sys.platform == "darwin":
        command = ["nm", "-gU", str(path)]
    else:
        command = ["nm", "--defined-only", "-g"]
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
    parser.add_argument("archive", type=Path, help="Bazel-built libcnats.a")
    parser.add_argument("shared", type=Path, nargs="?", help="libcnats.so or libcnats.dylib to verify")
    args = parser.parse_args()
    if not args.update and args.shared is None:
        parser.error("provide a shared library to verify, or use --update")

    header = (ROOT / "src/nats.h").read_text()
    header = re.sub(r"/\*.*?\*/|//[^\n]*", "", header, flags=re.S)
    declared = set(re.findall(r"^NATS_EXTERN\s+[^;]+?\b(\w+)\s*\(", header, re.M))
    expected = declared & symbols(args.archive)
    if not expected:
        raise SystemExit("No public API symbols found in the archive")
    names = sorted(expected)
    linux = "{\n    global:\n" + "".join(f"        {name};\n" for name in names) + "    local: *;\n};\n"
    macos = "".join(f"_{name}\n" for name in names)
    for name, contents in [("cnats.lds", linux), ("cnats.macos.exports", macos)]:
        path = ROOT / name
        if args.update:
            path.write_text(contents)
        elif path.read_text() != contents:
            raise SystemExit(f"{name} is stale; run tools/bazel_exports.py --update {args.archive}")
    if args.shared is not None:
        actual = symbols(args.shared, shared=True)
        if actual != expected:
            raise SystemExit(f"Shared API mismatch: missing={sorted(expected - actual)}, unexpected={sorted(actual - expected)}")
    print(f"Verified {len(expected)} public API symbols")


if __name__ == "__main__":
    main()
