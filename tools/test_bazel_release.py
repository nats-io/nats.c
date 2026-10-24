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

"""Exercise release packaging in isolated Git repositories without publishing."""

import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parent.parent / ".github/workflows/release_prep.sh"
VERSION = "1.2.3"
TAG = f"v{VERSION}"


class ReleasePackagingTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cnats-release-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.env = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        self.git("init", "--quiet")
        (self.root / "src").mkdir()
        (self.root / "src/version.h").write_text(f'#define NATS_VERSION_STRING "{VERSION}"\n')
        self.module = ('module(\n    name = "cnats",\n)\n'
                       'bazel_dep(name = "rules_cc", version = "0.2.17")\n')
        (self.root / "MODULE.bazel").write_text(self.module)
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Release sources")
        self.git("tag", TAG)
        notes = self.root / "cnats-release-metadata/release-notes.md"
        notes.parent.mkdir()
        self.notes = "## Release notes\n\nMaintainer text, including `code` and $values.\n"
        notes.write_text(self.notes)

    def git(self, *args):
        return subprocess.run(
            ["git", "-c", "user.name=Release Test", "-c", "user.email=test@example.invalid",
             "-c", "commit.gpgsign=false", *args],
            cwd=self.root, env=self.env, check=True, capture_output=True, text=True,
        )

    def prepare(self, tag=TAG):
        return subprocess.run(
            ["bash", str(SCRIPT), tag], cwd=self.root, env=self.env,
            capture_output=True, text=True,
        )

    def test_archive_preserves_notes_and_is_reproducible(self):
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, self.notes)
        archive = self.root / f"build-bazel-release/cnats-{VERSION}.tar.gz"
        first = archive.read_bytes()
        with tarfile.open(archive) as packed:
            self.assertEqual(
                set(packed.getnames()),
                {f"cnats-{VERSION}", f"cnats-{VERSION}/MODULE.bazel",
                 f"cnats-{VERSION}/src", f"cnats-{VERSION}/src/version.h"},
            )
            module = packed.extractfile(f"cnats-{VERSION}/MODULE.bazel").read().decode()
            self.assertIn(f'version = "{VERSION}"', module.partition(")")[0])
            self.assertIn('bazel_dep(name = "rules_cc", version = "0.2.17")', module)
            header = packed.extractfile(f"cnats-{VERSION}/src/version.h").read().decode()
            self.assertEqual(header, (self.root / "src/version.h").read_text())
        self.assertEqual((self.root / "MODULE.bazel").read_text(), self.module)
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(archive.read_bytes(), first)

    def test_rejects_header_version_mismatch(self):
        (self.root / "src/version.h").write_text('#define NATS_VERSION_STRING "9.9.9"\n')
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("versions must match", result.stderr)
        self.assertFalse((self.root / "build-bazel-release").exists())

    def test_prerelease_tag_stamps_prerelease_version(self):
        version = f"{VERSION}-beta"
        (self.root / "src/version.h").write_text(f'#define NATS_VERSION_STRING "{version}"\n')
        self.git("add", "src/version.h")
        self.git("commit", "--quiet", "-m", "Prerelease sources")
        self.git("tag", f"v{version}")
        result = self.prepare(f"v{version}")
        self.assertEqual(result.returncode, 0, result.stderr)
        with tarfile.open(self.root / f"build-bazel-release/cnats-{version}.tar.gz") as packed:
            module = packed.extractfile(f"cnats-{version}/MODULE.bazel").read().decode()
            self.assertIn(f'version = "{version}"', module.partition(")")[0])

    def test_existing_module_version_is_replaced(self):
        version = "1.2.4"
        module = ('module(name = "cnats", version = "0.0.0-dev")\n'
                  'bazel_dep(name = "rules_cc", version = "0.2.17")\n')
        (self.root / "MODULE.bazel").write_text(module)
        (self.root / "src/version.h").write_text(f'#define NATS_VERSION_STRING "{version}"\n')
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Sources with a module version")
        self.git("tag", f"v{version}")
        result = self.prepare(f"v{version}")
        self.assertEqual(result.returncode, 0, result.stderr)
        with tarfile.open(self.root / f"build-bazel-release/cnats-{version}.tar.gz") as packed:
            published = packed.extractfile(f"cnats-{version}/MODULE.bazel").read().decode()
            self.assertEqual(published, module.replace("0.0.0-dev", version))

    def test_rejects_checkout_at_another_commit(self):
        self.git("commit", "--quiet", "--allow-empty", "-m", "Later sources")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checkout must be at", result.stderr)

    def test_rejects_invalid_tag_and_missing_notes(self):
        result = self.prepare("v1.2.3/another-ref")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid release tag", result.stderr)
        (self.root / "cnats-release-metadata/release-notes.md").unlink()
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing existing release notes", result.stderr)


if __name__ == "__main__":
    unittest.main()
