"""Generate the checked-in Starlark list from the C test registry."""

import json
from pathlib import Path
import re
import sys


def main():
    source, output = map(Path, sys.argv[1:])
    test_names = []
    seen = set()
    for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        match = re.fullmatch(r"_test\(([A-Za-z_][A-Za-z0-9_]*)\)", line.strip())
        if not match:
            raise SystemExit(f"{source}:{number}: expected _test(test_name)")
        test_name = match[1]
        if test_name in seen:
            raise SystemExit(f"{source}:{number}: duplicate test {test_name}")
        seen.add(test_name)
        test_names.append(test_name)
    if not test_names:
        raise SystemExit(f"{source}: no tests found")
    content = '"""Generated from list_test.txt; update with bazel run //test:update_test_list."""\n\n'
    content += "ALL_TESTS = [\n"
    content += "".join(f"    {json.dumps(test_name)},\n" for test_name in test_names)
    content += "]\n"
    output.write_text(content, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
