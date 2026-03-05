# CLAUDE.md - nats.c

NATS client library for C. Supports core NATS, JetStream, KeyValue, Object Store, and microservices. C99 standard, cross-platform (Linux, macOS, Windows).

## Build Commands

```bash
# Standard build (out-of-source)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make

# Common cmake options
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \           # Debug, Release, RelWithDebInfo
  -DNATS_BUILD_WITH_TLS=ON \           # TLS via OpenSSL (default ON)
  -DNATS_BUILD_STREAMING=OFF \         # NATS Streaming/STAN (deprecated, default OFF)
  -DNATS_BUILD_USE_SODIUM=OFF \        # libsodium for NKeys (default OFF)
  -DNATS_WITH_EXPERIMENTAL=ON \        # Experimental APIs
  -DNATS_BUILD_EXAMPLES=ON \           # Build examples (default ON)
  -DNATS_COVERAGE=ON                   # Code coverage

# Dev mode build (adds type safety checks for lock/unlock macros)
cmake .. -DNATS_BUILD_DEV_MODE=ON -DCMAKE_BUILD_TYPE=Debug

# Run tests (requires nats-server on PATH)
cd build
ctest -L test --timeout 60 --output-on-failure

# Run a single test
cd build
ctest -L test --timeout 60 -V --repeat-until-fail 1 -R <TestName>    # e.g., ctest -L test --timeout 60 -V --repeat-until-fail 1 -R DefaultConnection

# With sanitizers (set env before cmake)
NATS_SANITIZE=address cmake .. -DNATS_SANITIZE=ON -DCMAKE_C_FLAGS=-fsanitize=address
```

## Project Structure

```
src/                        # Library source code
  nats.h                    # Internal header with public API declarations (~10K lines, Doxygen-documented)
  natsp.h                   # Private header: all internal struct definitions, helper macros
  status.h                  # natsStatus enum, jsErrCode enum (public)
  version.h                 # Version defines (generated from version.h.in)
  mem.h                     # Memory allocation macros (NATS_MALLOC/FREE/STRDUP)
  err.h                     # Error reporting macros and functions
  conn.c/h                  # Connection implementation
  sub.c/h                   # Subscription implementation
  pub.c                     # Publish implementation
  msg.c/h                   # Message type
  js.c/h, jsm.c             # JetStream client and management
  kv.c/h                    # KeyValue store
  object.c/h                # Object store
  micro.c, microp.h         # Microservices framework (micro_*)
  micro_client.c, micro_endpoint.c, micro_error.c, micro_monitoring.c, micro_request.c
  opts.c/h                  # Connection options
  parser.c/h                # Protocol parser
  buf.c/h                   # Buffer utilities
  hash.c/h                  # Hash table
  nuid.c/h                  # NUID generation
  nkeys.c/h                 # NKey authentication
  crypto.c/h                # Crypto utilities
  dispatch.c/h              # Message dispatch (thread pool, per-sub threads)
  asynccb.c/h               # Async callback management
  timer.c/h                 # Timer implementation
  srvpool.c/h               # Server pool management
  comsock.c/h               # Socket communication
  url.c/h                   # URL parsing
  util.c/h                  # General utilities
  stats.c/h                 # Statistics
  natstime.c/h              # Time utilities
  gc.h                      # Garbage collection
  deprnats.h                # Deprecated top-level nats.h redirect
  include/                  # Platform abstraction headers
    n-unix.h, n-win.h       # Platform-specific type definitions
  unix/                     # Unix platform impl (thread, mutex, cond, sock)
  win/                      # Windows platform impl
  glib/                     # Internal GLib-like utilities (timers, SSL, GC, dispatch pool, etc.)
  stan/                     # NATS Streaming (deprecated, requires protobuf-c)
  adapters/                 # Event loop adapter headers (libuv, libevent)
test/
  test.c                    # Single monolithic test file
  test.h                    # Test helpers, server management macros
  list.h                    # X-macro that includes list_test.txt, list_bench.txt, list_stan.txt
  list_test.txt             # Test function registry (~304 tests), format: _test(TestName)
  list_bench.txt            # Benchmark function registry
  CMakeLists.txt            # Test build config, registers each test with CTest
  certs/                    # TLS test certificates
  dylib/                    # Test for dynamic library loading without NATS calls
  check_cpp/                # C++ compatibility check
examples/                   # Example programs
  getstarted/               # Simple getting-started examples
  stan/                     # NATS Streaming examples (deprecated)
doc/                        # Doxygen documentation
```

## Code Style and Conventions

### Naming
- **Public API functions**: `nats<Type>_<Action>()` with `NATS_EXTERN` prefix, e.g., `natsConnection_Connect()`, `natsSubscription_NextMsg()`, `jsStreamConfig_Init()`
- **Internal functions**: `nats<Type>_<action>()` (camelCase action), e.g., `natsConn_publish()`, `natsConn_bufferFlush()`
- **Static functions**: `_lowerCamelCase()` with leading underscore, e.g., `_spinUpSocketWatchers()`, `_processConnInit()`
- **Struct fields**: `camelCase`, e.g., `maxReconnect`, `writeDeadline`, `reconnectWait`
- **Constants/defines**: `NATS_UPPER_CASE` for public, `_UPPER_CASE_` for protocol ops
- **Types**: Opaque typedefs in public API (`natsConnection*`, `natsSubscription*`), `__name` double-underscore prefix for internal struct tags (e.g., `struct __natsConnection`)
- **Microservices**: use `micro_` prefix and `snake_case` for internal functions, `microService_Action()` for public

### Header Guards
- Format: `NAME_H_` (e.g., `#ifndef CONN_H_`, `#ifndef NATSP_H_`, `#ifndef STATUS_H_`)

### License Header
Every file starts with the Apache 2.0 license header (C-style `//` comments):
```c
// Copyright 2015-2025 The NATS Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
```

### Error Handling
- Functions return `natsStatus` (enum). Success is `NATS_OK`.
- Chain calls with `IFOK(s, call())` macro: `if (s == NATS_OK) { s = (call); }`
- Set errors via `nats_setError()` or `nats_setDefaultError()` macros
- Microservices use `microError*` return type with `MICRO_CALL(err, call)` chaining

### Memory Management
- Use `NATS_MALLOC`, `NATS_CALLOC`, `NATS_REALLOC`, `NATS_STRDUP`, `NATS_FREE` from `mem.h`
- Duplicate strings with `DUP_STRING(s, dest, src)` or `IF_OK_DUP_STRING(s, dest, src)` macros
- Reference counting: `_retain()`/`_release()` pattern (macros in release, functions in DEV_MODE)
- `*_Destroy()` functions for public API cleanup

### Threading / Locking
- Custom mutex/condition/thread abstraction in `natsp.h` (platform-specific in `unix/` and `win/`)
- Lock helpers: `natsConn_Lock()`/`natsConn_Unlock()` -- macros in release, real functions in DEV_MODE
- `NATS_BUILD_DEV_MODE` build flag enables type-safe function versions for debugging

### Brace Style
- Opening brace always on next line
- 4-space indentation

### Tests
- All tests are in a single `test/test.c` file (~41K lines)
- Test functions: `void test_<TestName>(void)` (e.g., `test_DefaultConnection`, `test_JetStreamPublish`)
- Registered in `test/list_test.txt` with `_test(TestName)` macro
- Test assertions: `test("description")` then `testCond(condition)` -- prints PASSED/FAILED
- Tests require a running `nats-server` binary on PATH
- NATS_TEST_SERVER_VERSION environment must be set to run tests. This should be set with `export NATS_TEST_SERVER_VERSION="$(nats-server -v)"`
- Tests use `serverVersionAtLeast(major, minor, update)` to skip version-dependent tests

## CI

- Runs on Ubuntu (gcc, clang) and Windows
- PR checks: Debug build, DEV_MODE, sanitizers (address + thread), coverage (TLS/no-TLS/no-verify-host)
- Tests against `nats-server` main branch (or tagged releases)
- Dependencies fetched from `nats-io/nats.c.deps` repo (protobuf-c, libsodium, nats-server binary)
- Coverage reports via Codecov

## Key CMake Options Reference

| Option | Default | Description |
|--------|---------|-------------|
| `NATS_BUILD_WITH_TLS` | ON | TLS support via OpenSSL |
| `NATS_BUILD_TLS_FORCE_HOST_VERIFY` | ON | Force hostname verification |
| `NATS_BUILD_STREAMING` | OFF | NATS Streaming (deprecated) |
| `NATS_BUILD_USE_SODIUM` | OFF | libsodium for NKey support |
| `NATS_WITH_EXPERIMENTAL` | OFF | Experimental API support |
| `NATS_BUILD_LIB_STATIC` | ON | Build static library |
| `NATS_BUILD_LIB_SHARED` | ON | Build shared library |
| `NATS_BUILD_EXAMPLES` | ON | Build example programs |
| `NATS_COVERAGE` | ON/OFF | Code coverage flags |
| `NATS_BUILD_DEV_MODE` | OFF | Type-safe lock/retain macros |
| `NATS_SANITIZE` | OFF | Enable sanitizers |
| `CMAKE_BUILD_TYPE` | Release | Build type |

## Important Patterns

- The public header is `src/nats.h` (installed as `<nats/nats.h>`). The old `<nats.h>` path goes through `deprnats.h` which emits a deprecation warning.
- `natsp.h` is the central private header that includes all internal type definitions and is included by almost every `.c` file.
- The library uses C99 (`-std=c99 -pedantic` on Unix).
- No `.clang-format` or `.editorconfig` exists -- follow existing code style.
- Streaming (STAN) support is deprecated; new features should use JetStream.
- The test binary is `testsuite` and each test is a CTest entry with label `test` or `bench`.
