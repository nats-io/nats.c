# Bazel support

The `cnats` Bzlmod module supports Linux, macOS (Intel and ARM), and Windows
with MSVC. Bazel 7.2.1 or newer is required; CI tests the current Bazel 7, 8,
and 9 releases. [Bazelisk](https://github.com/bazelbuild/bazelisk) uses the
checked-in `.bazelversion` for local development.

## Build and test

From the repository root:

```sh
bazel build //:cnats //:cnats_shared
bazel test --test_output=errors //...
```

The separate `test/bcr` module also verifies the external dependency interface:

```sh
cd test/bcr
bazel test --test_output=errors //...
```

Its static and shared C smoke tests cover the public headers, version, lifecycle,
messages and headers, JetStream options, and TLS configuration. They do not need
a NATS server. Set `USE_BAZEL_VERSION=7.x`, `8.x`, or `9.x` to select a different
Bazel version when using Bazelisk.

Run `python3 tools/test_bazel_release.py` to check archive reproducibility,
release-note preservation, and rejection of invalid release inputs locally.

## Consume the library

Add this to your application's `MODULE.bazel`, replacing `VERSION` with a
[published cnats version](https://registry.bazel.build/modules/cnats):

```starlark
bazel_dep(name = "cnats", version = "VERSION")
```

For a local checkout, also add:

```starlark
local_path_override(module_name = "cnats", path = "/path/to/nats.c")
```

Include `<nats/nats.h>`, `<nats/status.h>`, and `<nats/version.h>`.
The deprecated flat `<nats.h>` and internal headers are not public Bazel headers.
Load the C++ rules from `rules_cc` in the consumer's BUILD file:

```starlark
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

cc_binary(
    name = "client",
    srcs = ["client.c"],
    deps = ["@cnats"],
)

cc_binary(
    name = "shared_client",
    srcs = ["client.c"],
    deps = ["@cnats//:cnats_shared_headers"],
    dynamic_deps = ["@cnats//:cnats_shared"],
)
```

The shared target produces `libcnats.so`, `libcnats.dylib`, or `cnats.dll`.
Bazel supplies the runtime library to shared consumers. Windows export/import
definitions are scoped to the appropriate DLL build and consumers; the static
build uses `NATS_STATIC` privately.

## Features and dependencies

The build follows BCR's existing feature defaults: core NATS, JetStream,
key/value, ObjectStore, and microservices, with TLS and forced hostname
verification enabled. Streaming, experimental APIs, libsodium, and adapter
integrations are not enabled. CMake options do not configure the Bazel build.

The declared dependencies match the BCR build: OpenSSL `3.5.4.bcr.1`, platforms
`1.0.0`, and rules_cc `0.2.17`. Normal module resolution may select newer
versions. Shared cnats embeds OpenSSL; only the public cnats API is exported.

## Maintain shared-library exports

Linux and macOS use checked-in export lists, so ordinary builds do not need
tools that scan symbols. After adding or removing public APIs, build the static
library and refresh both lists on Linux or macOS:

```sh
bazel build //:cnats
python3 tools/bazel_exports.py --update bazel-bin/libcnats.a
bazel build //:cnats_shared
python3 tools/bazel_exports.py bazel-bin/libcnats.a bazel-bin/libcnats.so
```

Use `libcnats.dylib` for the last command on macOS. The tool intersects
`NATS_EXTERN` declarations with implemented symbols from the default static
build, then verifies the shared library exports exactly that API. CI also
checks that the shared library has no OpenSSL shared-library dependency.

## Releases and BCR attestations

Maintainers continue to publish GitHub releases with their existing version tags
and release notes. The `Bazel release and BCR attestations` workflow runs on
`release: published`, then:

1. Preserves the existing notes and prerelease status, and tests the tagged build.
2. Creates `cnats-VERSION.tar.gz` from the tagged Git tree, rooted at
   `cnats-VERSION/`, with deterministic gzip metadata. The archive's root
   `MODULE.bazel` is stamped with the tag version, removing the leading `v`.
3. Uses BCR's trusted `bazel-contrib` release workflow to attach that archive and
   `cnats-VERSION.tar.gz.intoto.jsonl` to the existing release.
4. Uses the trusted `publish-to-bcr` workflow to generate `source.json` and
   `MODULE.bazel` attestations, attach them to the release, and submit a draft
   BCR pull request containing the module, source manifest, and `attestations.json`.

The source template points at the uploaded archive rather than GitHub's
automatically generated tag archive. Future registry entries use the upstream
build files directly, without a BUILD overlay. Existing BCR versions remain
unchanged.

The source template also sets `docs_url` to this guide at the release tag, so
each BCR entry links to documentation matching its source release. No generated
documentation archive is required.

Before enabling this workflow, configure these GitHub Actions settings in the
upstream repository:

- Repository variable `BCR_REGISTRY_FORK`: a writable fork of
  `bazelbuild/bazel-central-registry`, for example `dcarp/bazel-central-registry`.
- Secret `BCR_PUBLISH_TOKEN`: a publishing account's classic PAT with `repo` and
  `workflow` scopes, as required by
  [publish-to-bcr](https://github.com/bazel-contrib/publish-to-bcr#setup).

The workflow grants `contents: write`, `id-token: write`, and
`attestations: write` only to release/publishing jobs. The publishing job also
has `actions: read` to download release artifacts. A registry PR is created as
a draft for maintainer review; it is not merged automatically.

For each release, keep `src/version.h` and the CMake version in sync with the
tag. `MODULE.bazel` intentionally omits the version in Git: packaging inserts it
in the published copy automatically, and BCR publishing uses the same tag version
for its module entry. For example, `v3.15.0` produces `version = "3.15.0"`;
prerelease tags retain their suffix. Dependency versions and the checkout are
preserved. The consumer module uses a local override without a cnats version.
Update both `.bazelversion` files when appropriate. The preparation script rejects
a tag that disagrees with the public version header. The release tag must include
these workflow and template files.

Retry a failed run using GitHub Actions, or dispatch the workflow at the same tag:

```sh
gh workflow run bazel-release.yml --ref vVERSION -f tag_name=vVERSION
```

The tag must identify an existing published release. Running from a branch is
rejected so the attestation records the release source ref. The release currently
must allow adding assets after publication; immutable releases require a
different draft/finalize sequence.

After the first release, validate the generated registry entry with BCR's
`tools/bcr_validation.py --check=cnats@VERSION`, and verify the source archive:

```sh
gh attestation verify cnats-VERSION.tar.gz --repo nats-io/nats.c
```

See [BCR attestation requirements](https://github.com/bazelbuild/bazel-central-registry/blob/main/docs/attestations.md)
for the three artifacts and trusted builder identities checked during presubmit.
Signed attestations are generated in GitHub Actions; they cannot be generated
or fully verified by a local checkout before a release run.
