#!/usr/bin/env bash
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

# Called by bazel-contrib's BCR-trusted release workflow at the release tag.
set -euo pipefail

release_tag=${1:?Usage: release_prep.sh vVERSION}
if [[ ! "$release_tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9][A-Za-z0-9.-]*)?$ ]]; then
    echo "Invalid release tag: $release_tag" >&2
    exit 1
fi
release_version=${release_tag#v}

# Never package another commit under the requested tag's name.
if [[ $(git rev-parse HEAD) != $(git rev-parse --verify "refs/tags/$release_tag^{commit}") ]]; then
    echo "The checkout must be at $release_tag" >&2
    exit 1
fi

# The caller captures stdout as release notes. Preserve the maintainer's notes,
# which the prepare job saved before the reusable workflow downloaded artifacts.
release_notes=cnats-release-metadata/release-notes.md
if [[ ! -f "$release_notes" ]]; then
    echo "Missing existing release notes: $release_notes" >&2
    exit 1
fi

python3 - "$release_version" <<'PY'
import gzip
import io
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

version = sys.argv[1]
header = re.search(r'^#define NATS_VERSION_STRING "([^"]+)"', Path('src/version.h').read_text(), re.M)
if header is None or header[1] != version:
    sys.exit('Release tag and src/version.h versions must match')

prefix = f'cnats-{version}/'
# Archive the tagged tree, stamping only the module() version in the published
# copy. Preserve dependency versions, the source checkout, and Git tar metadata.
with tempfile.TemporaryFile() as raw:
    subprocess.run(['git', 'archive', '--format=tar', f'--prefix={prefix}', 'HEAD'], stdout=raw, check=True)
    raw.seek(0)
    with tarfile.open(fileobj=raw, mode='r:') as source:
        module = source.getmember(f'{prefix}MODULE.bazel')
        text = source.extractfile(module).read().decode()
        text, count = re.subn(
            r'(\bmodule\s*\([^)]*?\bversion\s*=\s*)"[^"]*"',
            lambda match: f'{match[1]}"{version}"',
            text, count=1, flags=re.S,
        )
        if count == 0:
            text, count = re.subn(
                r'(\bmodule\s*\(\s*)',
                lambda match: f'{match[1]}version = "{version}",\n    ',
                text, count=1,
            )
        if count != 1:
            sys.exit('MODULE.bazel must declare module() to stamp')
        stamped = text.encode()
        destination = Path('build-bazel-release')
        destination.mkdir(exist_ok=True)
        with (destination / f'cnats-{version}.tar.gz').open('wb') as output:
            with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.PAX_FORMAT) as archive:
                    for member in source:
                        if member.name == module.name:
                            member.size = len(stamped)
                            archive.addfile(member, io.BytesIO(stamped))
                        else:
                            archive.addfile(member, source.extractfile(member) if member.isfile() else None)
PY
cat "$release_notes"
