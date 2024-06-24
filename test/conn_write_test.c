// Copyright 2015-2023 The NATS Authors
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

#include "natsp.h"
#include "test.h"
#include "conn.h"

void Test_ConnWriteChain(void)
{
    natsStatus s;
    natsWriteQueue w;

    natsMemOptions opts = {
        .heapPageSize = 4 * sizeof(natsWriteBuffer),
        .writeQueueBuffers = 4,
        .writeQueueMaxBuffers = 7,
    };
    char testName[256];
    sprintf(testName, "Set memory parameters: page size %zu, initial write buffers %zu, max write buffers %zu", 
        opts.heapPageSize, opts.writeQueueBuffers, opts.writeQueueMaxBuffers);
    test(testName);
    testCond(true);

    test("Initialize write chain");
    s = natsWriteChain_init(&w, &opts);
    testCond((STILL_OK(s)) &&
             (w.capacity == opts.writeQueueBuffers) &&
             (w.start == 0) &&
             (w.end == 0) &&
             (w.chain != NULL));

    test("Add 3 buffers");
    natsString s0 = NATS_STR("test0");
    natsString s1 = NATS_STR("test1");
    natsString s2 = NATS_STR("test2");
    s = natsWriteChain_add(&w, &s0, NULL, NULL);
    s = natsWriteChain_add(&w, &s1, NULL, NULL);
    s = natsWriteChain_add(&w, &s2, NULL, NULL);
    testCond((STILL_OK(s)) &&
             (w.start == 0) &&
             (w.end == 3) &&
             (w.chain[0].buf.data == s0.data) &&
             (w.chain[0].buf.len == s0.len) &&
             (w.chain[1].buf.data == s1.data) &&
             (w.chain[1].buf.len == s1.len) &&
             (w.chain[2].buf.data == s2.data) &&
             (w.chain[2].buf.len == s2.len) &&
             (w.capacity == 4));

    test("Get the current buffer, the first we added");
    natsWriteBuffer *wb = natsWriteChain_get(&w);
    testCond((wb != NULL) &&
             (wb->buf.data == s0.data) &&
             (wb->buf.len == s0.len));

    test("If we get again, we get the same one");
    wb = natsWriteChain_get(&w);
    testCond((wb != NULL) &&
             (wb->buf.data == s0.data) &&
             (wb->buf.len == s0.len));

    test("Done with the current buffer");
    s = natsWriteChain_done(NULL, &w);
    testCond((STILL_OK(s)) &&
             (w.start == 1) &&
             (w.end == 3) &&
             natsWriteChain_len(&w) == 2);

    test("Get the current buffer, the second we added");
    wb = natsWriteChain_get(&w);
    testCond((wb != NULL) &&
             (wb->buf.data == s1.data) &&
             (wb->buf.len == s1.len));

    test("Add/remove 9 times, to accomplish a wraparound of 1 item");
    natsString s3s11[] = {NATS_STR("test3"), NATS_STR("test4"), NATS_STR("test5"), NATS_STR("test6"),
                          NATS_STR("test7"), NATS_STR("test8"), NATS_STR("test9"), NATS_STR("test10"),
                          NATS_STR("test11")};
    for (size_t i = 0; i < sizeof(s3s11) / sizeof(*s3s11); i++)
    {
        s = natsWriteChain_done(NULL, &w);
        if (s != NATS_OK)
            break;
        s = natsWriteChain_add(&w, &s3s11[i], NULL, NULL);
        if (s != NATS_OK)
            break;
    }
    testCond((STILL_OK(s)) &&
             (w.start == 10) &&
             (w.end == 12) &&
             natsWriteChain_len(&w) == 2);

    test("Add one more");
    natsString s12 = NATS_STR("test12");
    s = natsWriteChain_add(&w, &s12, NULL, NULL);
    testCond((STILL_OK(s)) &&
             (w.start == 10) &&
             (w.end == 13) &&
             natsWriteChain_len(&w) == 3);

    test("Add one more and make sure it grows and resets");
    natsString s13 = NATS_STR("test13");
    s = natsWriteChain_add(&w, &s13, NULL, NULL);
    testCond((STILL_OK(s)) &&
             (w.start == 2) &&
             (w.end == 6) &&
             (natsWriteChain_len(&w) == 4) &&
             (w.capacity == opts.writeQueueMaxBuffers));
}
