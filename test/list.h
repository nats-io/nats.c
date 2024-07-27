// Copyright 2024 The NATS Authors
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

#ifdef _TEST_PROTO
#undef _test
#define _test(name) void test_##name(void);
#endif

#ifdef _TEST_LIST
#undef _test
#define _test(name) {#name, test_##name},
#endif

#include "list_test.txt"
#include "list_bench.txt"
#if defined(NATS_HAS_STREAMING)
#include "list_stan.txt"
#endif

#undef _test
