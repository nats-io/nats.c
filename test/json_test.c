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
#include "json.h"
#include "test.h"

void Test_JSONStructure(void)
{
    natsStatus s = NATS_OK;
    typedef struct TC
    {
        const char *name;
        const char *json;
    } TC;
    const TC tests[] = {
        {"empty object: ", "{}"},
        {"single: number: ", "{ \"test\":1}"},
        {"single: boolean true: ","{ \"test\":true}"},
        {"single: boolean false: ","{ \"test\":false}"},
        {"single: string: ","{ \"test\":\"abc\"}"},
        {"single: null: ","{ \"test\": null}"},
        {"multiple: numbers: ","{ \"test\":1, \"test2\":2}"},
        {"multiple: booleans: ","{ \"test\":true, \"test2\":false}"},
        {"multiple: strings: ","{ \"test\":\"a\", \"test2\":\"b\"}"},
        {"multiple: nulls: ","{ \"test\":null, \"test2\":null}"},
        {"multiple: mixed: ","{ \"test\":1, \"test2\":true, \"test3\":\"abc\", \"test4\":null}"},
        {"multiple: mixed different order: ","{ \"test2\":true, \"test3\":\"abc\", \"test4\":null, \"test\":1}"},

        {"empty array: ","{ \"test\": []}"},
        {"array of empty arrays: ","{ \"test\": [[], [], []]}"},
        {"array of empty objects: ","{ \"test\": [{}, {}, {}]}"},
        {"array of strings: ","{ \"test\": [\"a\", \"b\", \"c\"]}"},
        {"array of objects: ","{ \"test\": [{\"a\": 1}, {\"b\": \"c\"}]}"},
        {"array of arrays: ","{ \"test\": [[{\"a\": 1}], [{\"b\": \"c\"}]]}"},
        {"array of numbers: ","{ \"test\": [1, 2, 3]}"},
        {"array of doubles: ","{ \"test\": [1.1, 2.2, 3.3]}"},
        {"array of booleans: ","{ \"test\": [true, false, true]}"},
        {"empty nested object","{ \n\"test\":\n{}}"},
        {"nested objects","{ \"test\":1, \"inner1\": {\"inner\":\"a\",\"inner2\":2,\"inner3\":false,\"inner4\":{\"inner2\" : 1.234}}}"},

        {"ignored commas", "{ ,, \"test\":1,,,,  }"},
    };
    const TC errorTests[] = {
        {"error: starts with a letter", " A"},
        {"error: starts with a quote", " \""},
        {"error: value starts with a wrong char", " {\"test\" : XXX }"},
        {"error: array of nulls: ", "{ \"test\": [null, null, null]}"},
        {"error: mixed type array: ", "{ \"test\": [1, \"abc\", true]}"},
    };

    natsPool *pool = NULL;
    test("Create memory pool: ");
    s = nats_createPool(&pool, &nats_defaultMemOptions, "json-test");
    testCond(STILL_OK(s));

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(*tests)); i++)
    {
        natsJSONParser *parser = NULL;
        nats_JSON *json = NULL;
        size_t consumed = 0;
        TC tc = tests[i];
        
        test(tc.name);
        s = natsJSONParser_Create(&parser, pool);
        const uint8_t *data = (const uint8_t *)tc.json;
        const uint8_t *end = (const uint8_t *)tc.json + strlen(tc.json);
        IFOK(s, natsJSONParser_Parse(&json, parser, data, end, &consumed));
        testCond((STILL_OK(s)) && (json != NULL) && (consumed == strlen(tc.json)));
    }

    for (int i = 0; i < (int)(sizeof(errorTests) / sizeof(*errorTests)); i++)
    {
        natsJSONParser *parser = NULL;
        nats_JSON *json = NULL;
        TC tc = errorTests[i];

        test(tc.name);
        s = natsJSONParser_Create(&parser, pool);
        const uint8_t *data = (const uint8_t *)tc.json;
        const uint8_t *end = (const uint8_t *)tc.json + strlen(tc.json);
        IFOK(s, natsJSONParser_Parse(&json, parser, data, end, NULL));
        testCond((s != NATS_OK) && (json == NULL));
    }

    nats_releasePool(pool);
}


// Test_JSON(void)
// {
//     natsStatus s;
//     nats_JSON *json = NULL;
//     char buf[256];
//     int i;
//     int intVal = 0;
//     int64_t longVal = 0;
//     char *strVal = NULL;
//     bool boolVal = false;
//     long double doubleVal = 0;
//     char **arrVal = NULL;
//     bool *arrBoolVal = NULL;
//     long double *arrDoubleVal = NULL;
//     int *arrIntVal = NULL;
//     int64_t *arrLongVal = NULL;
//     uint64_t *arrULongVal = NULL;
//     nats_JSON **arrObjVal = NULL;
//     nats_JSONArray **arrArrVal = NULL;
//     int arrCount = 0;
//     uint64_t ulongVal = 0;
//     nats_JSON *obj1 = NULL;
//     nats_JSON *obj2 = NULL;
//     nats_JSON *obj3 = NULL;
//     int32_t int32Val = 0;
//     uint16_t uint16Val = 0;
//     const char *wrong[] = {
//         "{",
//         "}",
//         "{start quote missing\":0}",
//         "{\"end quote missing: 0}",
//         "{\"test\":start quote missing\"}",
//         "{\"test\":\"end quote missing}",
//         "{\"test\":1.2x}",
//         "{\"test\":tRUE}",
//         "{\"test\":true,}",
//         "{\"test\":true}, xxx}",
//         "{\"test\": \"abc\\error here\"}",
//         "{\"test\": \"abc\\u123\"}",
//         "{\"test\": \"abc\\u123g\"}",
//         "{\"test\": \"abc\\u 23f\"}",
//         ("{\"test\": \"abc\\"
//          ""),
//         "{\"test\": \"abc\\u1234",
//         "{\"test\": \"abc\\uabc",
//         "{\"test\" \"separator missing\"}",
//         "{\"test\":[1, \"abc\", true]}",
//     };
//     const char *good[] = {
//         "{}",
//         " {}",
//         " { }",
//         " { } ",
//         "{ \"test\":{}}",
//         "{ \"test\":1.2}",
//         "{ \"test\" :1.2}",
//         "{ \"test\" : 1.2}",
//         "{ \"test\" : 1.2 }",
//         "{ \"test\" : 1.2,\"test2\":1}",
//         "{ \"test\" : 1.2, \"test2\":1}",
//         "{ \"test\":0}",
//         "{ \"test\" :0}",
//         "{ \"test\" : 0}",
//         "{ \"test\" : 0 }",
//         "{ \"test\" : 0,\"test2\":1}",
//         "{ \"test\" : 0, \"test2\":1}",
//         "{ \"test\":true}",
//         "{ \"test\": true}",
//         "{ \"test\": true }",
//         "{ \"test\":true,\"test2\":1}",
//         "{ \"test\": true,\"test2\":1}",
//         "{ \"test\": true ,\"test2\":1}",
//         "{ \"test\":false}",
//         "{ \"test\": false}",
//         "{ \"test\": false }",
//         "{ \"test\":false,\"test2\":1}",
//         "{ \"test\": false,\"test2\":1}",
//         "{ \"test\": false ,\"test2\":1}",
//         "{ \"test\":\"abc\"}",
//         "{ \"test\": \"abc\"}",
//         "{ \"test\": \"abc\" }",
//         "{ \"test\":\"abc\",\"test2\":1}",
//         "{ \"test\": \"abc\",\"test2\":1}",
//         "{ \"test\": \"abc\" ,\"test2\":1}",
//         "{ \"test\": \"a\\\"b\\\"c\" }",
//         "{ \"test\": [\"a\", \"b\", \"c\"]}",
//         "{ \"test\": [\"a\\\"b\\\"c\"]}",
//         "{ \"test\": [\"abc,def\"]}",
//         "{ \"test\": [{\"a\": 1}, {\"b\": \"c\"}]}",
//         "{ \"test\": [[{\"a\": 1}], [{\"b\": \"c\"}]]}",
//         "{ \"test\": []}",
//         "{ \"test\": {\"inner\":\"a\",\"inner2\":2,\"inner3\":false,\"inner4\":{\"inner_inner1\" : 1.234}}}",
//         "{ \"test\": \"a\\\"b\\\"c\"}",
//         "{ \"test\": \"\\\"\\\\/\b\f\n\r\t\\uabcd\"}",
//         "{ \"test\": \"\\ua12f\"}",
//         "{ \"test\": \"\\uA01F\"}",
//         "{ \"test\": null}",
//     };
//     nats_JSONField *f = NULL;
//     unsigned char *bytes = NULL;
//     int bl = 0;

//     for (i = 0; i < (int)(sizeof(wrong) / sizeof(char *)); i++)
//     {
//         snprintf(buf, sizeof(buf), "Negative test %d: ", (i + 1));
//         test(buf);
//         s = nats_JSONParse(&json, wrong[i], -1);
//         testCond((s != NATS_OK) && (json == NULL));
//         json = NULL;
//     }
//     nats_clearLastError();

//     for (i = 0; i < (int)(sizeof(good) / sizeof(char *)); i++)
//     {
//         snprintf(buf, sizeof(buf), "Positive test %d: ", (i + 1));
//         test(buf);
//         s = nats_JSONParse(&json, good[i], -1);
//         testCond((STILL_OK(s)) && (json != NULL));
//         nats_JSONDestroy(json);
//         json = NULL;
//     }
//     nats_clearLastError();

//     // Check values
//     test("Empty string: ");
//     s = nats_JSONParse(&json, "{}", -1);
//     IFOK(s, nats_JSONGetInt(json, "test", &intVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 0) && (intVal == 0));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Single field, string: ");
//     s = nats_JSONParse(&json, "{\"test\":\"abc\"}", -1);
//     IFOK(s, nats_JSONGetStr(json, "test", &strVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (strcmp(strVal, "abc") == 0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(strVal);
//     strVal = NULL;

//     test("Single field, string with escape chars: ");
//     s = nats_JSONParse(&json, "{\"test\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}", -1);
//     IFOK(s, nats_JSONGetStr(json, "test", &strVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (strcmp(strVal, "\"\\/\b\f\n\r\t") == 0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(strVal);
//     strVal = NULL;

//     test("Single field, string with unicode: ");
//     s = nats_JSONParse(&json, "{\"test\":\"\\u0026\\u003c\\u003e\"}", -1);
//     IFOK(s, nats_JSONGetStr(json, "test", &strVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (strcmp(strVal, "&<>") == 0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(strVal);
//     strVal = NULL;

//     test("Single field, int: ");
//     s = nats_JSONParse(&json, "{\"test\":1234}", -1);
//     IFOK(s, nats_JSONGetInt(json, "test", &intVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (intVal == 1234));
//     nats_JSONDestroy(json);
//     json = NULL;
//     intVal = 0;

//     test("Single field, int32: ");
//     s = nats_JSONParse(&json, "{\"test\":1234}", -1);
//     IFOK(s, nats_JSONGetInt32(json, "test", &int32Val));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (int32Val == 1234));
//     nats_JSONDestroy(json);
//     json = NULL;
//     int32Val = 0;

//     test("Single field, uint16: ");
//     s = nats_JSONParse(&json, "{\"test\":1234}", -1);
//     IFOK(s, nats_JSONGetUInt16(json, "test", &uint16Val));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (uint16Val == 1234));
//     nats_JSONDestroy(json);
//     json = NULL;
//     uint16Val = 0;

//     test("Single field, long: ");
//     s = nats_JSONParse(&json, "{\"test\":9223372036854775807}", -1);
//     IFOK(s, nats_JSONGetLong(json, "test", &longVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (longVal == 9223372036854775807L));
//     nats_JSONDestroy(json);
//     json = NULL;
//     longVal = 0;

//     test("Single field, neg long: ");
//     s = nats_JSONParse(&json, "{\"test\":-9223372036854775808}", -1);
//     IFOK(s, nats_JSONGetLong(json, "test", &longVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (longVal == (int64_t)0x8000000000000000));
//     nats_JSONDestroy(json);
//     json = NULL;
//     longVal = 0;

//     test("Single field, neg long as ulong: ");
//     s = nats_JSONParse(&json, "{\"test\":-123456789}", -1);
//     IFOK(s, nats_JSONGetULong(json, "test", &ulongVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (ulongVal == 0xFFFFFFFFF8A432EB));
//     nats_JSONDestroy(json);
//     json = NULL;
//     ulongVal = 0;

//     test("Single field, ulong: ");
//     s = nats_JSONParse(&json, "{\"test\":18446744073709551615}", -1);
//     IFOK(s, nats_JSONGetULong(json, "test", &ulongVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (ulongVal == 0xFFFFFFFFFFFFFFFF));
//     nats_JSONDestroy(json);
//     json = NULL;
//     ulongVal = 0;

//     test("Single field, ulong: ");
//     s = nats_JSONParse(&json, "{\"test\":9007199254740993}", -1);
//     IFOK(s, nats_JSONGetULong(json, "test", &ulongVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (ulongVal == 9007199254740993));
//     nats_JSONDestroy(json);
//     json = NULL;
//     ulongVal = 0;

//     test("Single field, double: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5e3}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)1234.5e+3));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double negative: ");
//     s = nats_JSONParse(&json, "{\"test\":-1234}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)-1234));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp negative 1: ");
//     s = nats_JSONParse(&json, "{\"test\":1234e-3}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)1234.0 / 1000.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp negative 2: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5e-3}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345.0 / 10000.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp negative 3: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5e-1}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345.0 / 100.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp negative 4: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5e-0}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345.0 / 10.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 1: ");
//     s = nats_JSONParse(&json, "{\"test\":1234e+3}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)1234.0 * 1000));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 2: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5e+3}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345.0 * 100.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 3: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5678e+2}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345678.0 / 100.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 4: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5678e+4}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345678.0 / 10000.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 5: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5678e+5}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345678.0 * 10.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 6: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5678e+0}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345678.0 / 10000.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, double exp positive 6: ");
//     s = nats_JSONParse(&json, "{\"test\":1234.5678e1}", -1);
//     IFOK(s, nats_JSONGetDouble(json, "test", &doubleVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (doubleVal == (long double)12345678.0 / 1000.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     doubleVal = 0;

//     test("Single field, bool: ");
//     s = nats_JSONParse(&json, "{\"test\":true}", -1);
//     IFOK(s, nats_JSONGetBool(json, "test", &boolVal));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && boolVal);
//     nats_JSONDestroy(json);
//     json = NULL;
//     boolVal = false;

//     test("Single field, string array: ");
//     s = nats_JSONParse(&json, "{\"test\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}", -1);
//     IFOK(s, nats_JSONDupStringArray(json, "test", &arrVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 5) && (strcmp(arrVal[0], "a") == 0) && (strcmp(arrVal[1], "b") == 0) && (strcmp(arrVal[2], "c") == 0) && (strcmp(arrVal[3], "d") == 0) && (strcmp(arrVal[4], "e") == 0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     for (i = 0; i < arrCount; i++)
//         free(arrVal[i]);
//     free(arrVal);
//     arrVal = NULL;
//     arrCount = 0;

//     test("Single field, null string array: ");
//     s = nats_JSONParse(&json, "{\"test\": null}", -1);
//     IFOK(s, nats_JSONDupStringArray(json, "test", &arrVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrVal == NULL) && (arrCount == 0));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Single field, bool array: ");
//     s = nats_JSONParse(&json, "{\"test\":[true, false, true]}", -1);
//     IFOK(s, nats_JSONGetArrayBool(json, "test", &arrBoolVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 3) && arrBoolVal[0] && !arrBoolVal[1] && arrBoolVal[2]);
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(arrBoolVal);
//     arrBoolVal = NULL;
//     arrCount = 0;

//     test("Single field, double array: ");
//     s = nats_JSONParse(&json, "{\"test\":[1.0, 2.0, 3.0]}", -1);
//     IFOK(s, nats_JSONGetArrayDouble(json, "test", &arrDoubleVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 3) && (arrDoubleVal[0] == 1.0) && (arrDoubleVal[1] == 2.0) && (arrDoubleVal[2] == 3.0));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(arrDoubleVal);
//     arrDoubleVal = NULL;
//     arrCount = 0;

//     test("Single field, int array: ");
//     s = nats_JSONParse(&json, "{\"test\":[1, 2, 3]}", -1);
//     IFOK(s, nats_JSONGetArrayInt(json, "test", &arrIntVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 3) && (arrIntVal[0] == 1) && (arrIntVal[1] == 2) && (arrIntVal[2] == 3));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(arrIntVal);
//     arrIntVal = NULL;
//     arrCount = 0;

//     test("Single field, long array: ");
//     s = nats_JSONParse(&json, "{\"test\":[1, 2, 3]}", -1);
//     IFOK(s, nats_JSONGetArrayLong(json, "test", &arrLongVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 3) && (arrLongVal[0] == 1) && (arrLongVal[1] == 2) && (arrLongVal[2] == 3));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(arrLongVal);
//     arrLongVal = NULL;
//     arrCount = 0;

//     test("Single field, ulong array: ");
//     s = nats_JSONParse(&json, "{\"test\":[1, 2, 3]}", -1);
//     IFOK(s, nats_JSONGetArrayULong(json, "test", &arrULongVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 3) && (arrULongVal[0] == 1) && (arrULongVal[1] == 2) && (arrULongVal[2] == 3));
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(arrULongVal);
//     arrULongVal = NULL;
//     arrCount = 0;

//     test("Single field, object array: ");
//     s = nats_JSONParse(&json, "{\"test\":[{\"a\": 1},{\"b\": true}]}", -1);
//     IFOK(s, nats_JSONGetArrayObject(json, "test", &arrObjVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 2) && (nats_JSONGetInt(arrObjVal[0], "a", &intVal) == NATS_OK) && (intVal == 1) && (nats_JSONGetBool(arrObjVal[1], "b", &boolVal) == NATS_OK) && boolVal);
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(arrObjVal);
//     arrObjVal = NULL;
//     arrCount = 0;
//     intVal = 0;
//     boolVal = false;

//     test("Single field, array null: ");
//     s = nats_JSONParse(&json, "{\"test\":null}", -1);
//     IFOK(s, nats_JSONGetArrayObject(json, "test", &arrObjVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrObjVal == NULL) && (arrCount == 0));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Single field, array array: ");
//     s = nats_JSONParse(&json, "{\"test\":[[\"a\", \"b\"],[1, 2, 3],[{\"c\": true}]]}", -1);
//     IFOK(s, nats_JSONGetArrayArray(json, "test", &arrArrVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 3) && (nats_JSONArrayAsStrings(arrArrVal[0], &arrVal, &arrCount) == NATS_OK) && (arrCount == 2) && (strcmp(arrVal[0], "a") == 0) && (strcmp(arrVal[1], "b") == 0) && (nats_JSONArrayAsInts(arrArrVal[1], &arrIntVal, &arrCount) == NATS_OK) && (arrCount == 3) && (arrIntVal[0] == 1) && (arrIntVal[1] == 2) && (arrIntVal[2] == 3) && (nats_JSONArrayAsObjects(arrArrVal[2], &arrObjVal, &arrCount) == NATS_OK) && (arrCount == 1) && (nats_JSONGetBool(arrObjVal[0], "c", &boolVal) == NATS_OK) && boolVal);
//     nats_JSONDestroy(json);
//     json = NULL;
//     for (i = 0; i < 2; i++)
//         free(arrVal[i]);
//     free(arrVal);
//     arrVal = NULL;
//     free(arrIntVal);
//     arrIntVal = NULL;
//     free(arrArrVal);
//     arrArrVal = NULL;
//     free(arrObjVal);
//     arrObjVal = NULL;
//     boolVal = false;
//     arrCount = 0;

//     test("Object: ");
//     s = nats_JSONParse(&json, "{\"obj1\":{\"obj2\":{\"obj3\":{\"a\": 1},\"b\":true},\"c\":1.2},\"d\":3}", -1);
//     IFOK(s, nats_JSONGetObject(json, "obj1", &obj1));
//     IFOK(s, nats_JSONGetObject(obj1, "obj2", &obj2));
//     IFOK(s, nats_JSONGetObject(obj2, "obj3", &obj3));
//     IFOK(s, nats_JSONGetInt(obj3, "a", &intVal));
//     IFOK(s, nats_JSONGetBool(obj2, "b", &boolVal));
//     IFOK(s, nats_JSONGetDouble(obj1, "c", &doubleVal));
//     IFOK(s, nats_JSONGetLong(json, "d", &longVal));
//     testCond((STILL_OK(s)) && (intVal == 1) && boolVal && (doubleVal == (long double)12.0 / 10.0) && (longVal == 3));
//     nats_JSONDestroy(json);
//     json = NULL;
//     intVal = 0;
//     boolVal = false;
//     doubleVal = 0.0;
//     longVal = 0;

//     test("Object, null: ");
//     s = nats_JSONParse(&json, "{\"obj\":null}", -1);
//     IFOK(s, nats_JSONGetObject(json, "obj", &obj1));
//     testCond((STILL_OK(s)) && (obj1 == NULL));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("All field types: ");
//     s = nats_JSONParse(&json, "{\"bool\":true,\"str\":\"abc\",\"int\":123,\"long\":456,\"double\":123.5,\"array\":[\"a\"]}", -1);
//     IFOK(s, nats_JSONGetBool(json, "bool", &boolVal));
//     IFOK(s, nats_JSONGetStr(json, "str", &strVal));
//     IFOK(s, nats_JSONGetInt(json, "int", &intVal));
//     IFOK(s, nats_JSONGetLong(json, "long", &longVal));
//     IFOK(s, nats_JSONGetDouble(json, "double", &doubleVal));
//     IFOK(s, nats_JSONDupStringArray(json, "array", &arrVal, &arrCount));
//     testCond((STILL_OK(s)) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 6) && boolVal && (strcmp(strVal, "abc") == 0) && (intVal == 123) && (longVal == 456) && (doubleVal == (long double)1235.0 / 10.0) && (arrCount == 1) && (strcmp(arrVal[0], "a") == 0));
//     test("Unknown field type: ");
//     if (STILL_OK(s))
//         s = nats_JSONRefField(json, "int", 255, &f);
//     testCond(s != NATS_OK);
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(strVal);
//     strVal = NULL;
//     boolVal = false;
//     intVal = 0;
//     longVal = 0;
//     doubleVal = 0;
//     for (i = 0; i < arrCount; i++)
//         free(arrVal[i]);
//     free(arrVal);
//     arrVal = NULL;
//     arrCount = 0;

//     test("Ask for wrong type: ");
//     s = nats_JSONParse(&json, "{\"test\":true}", -1);
//     IFOK(s, nats_JSONGetInt(json, "test", &intVal));
//     testCond((s != NATS_OK) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (intVal == 0));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Ask for wrong type (array): ");
//     s = nats_JSONParse(&json, "{\"test\":[\"a\", \"b\"]}", -1);
//     IFOK(s, nats_JSONRefArray(json, "test", TYPE_INT, &f));
//     testCond((s != NATS_OK) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (arrCount == 0) && (arrVal == NULL));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Ask for unknown type: ");
//     s = nats_JSONParse(&json, "{\"test\":true}", -1);
//     IFOK(s, nats_JSONRefField(json, "test", 9999, &f));
//     testCond((s == NATS_INVALID_ARG) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Ask for unknown type (array): ");
//     s = nats_JSONParse(&json, "{\"test\":true}", -1);
//     IFOK(s, nats_JSONRefArray(json, "test", 9999, &f));
//     testCond((s == NATS_INVALID_ARG) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Check no error and set to default for vars for unknown fields: ");
//     {
//         const char *initStr = "test";
//         const char *initStrArr[] = {"a", "b"};

//         strVal = (char *)initStr;
//         boolVal = true;
//         intVal = 123;
//         longVal = 456;
//         doubleVal = 789;
//         arrVal = (char **)initStrArr;
//         arrCount = 2;
//         s = nats_JSONParse(&json, "{\"test\":true}", -1);
//         IFOK(s, nats_JSONGetStr(json, "str", &strVal));
//         IFOK(s, nats_JSONGetInt(json, "int", &intVal));
//         IFOK(s, nats_JSONGetLong(json, "long", &longVal));
//         IFOK(s, nats_JSONGetBool(json, "bool", &boolVal));
//         IFOK(s, nats_JSONGetDouble(json, "bool", &doubleVal));
//         IFOK(s, nats_JSONDupStringArray(json, "array", &arrVal, &arrCount));
//         testCond((STILL_OK(s)) && (strVal == NULL) && (boolVal == false) && (intVal == 0) && (longVal == 0) && (doubleVal == 0) && (arrCount == 0) && (arrVal == NULL));
//         nats_JSONDestroy(json);
//         json = NULL;
//     }

//     test("Wrong string type: ");
//     strVal = NULL;
//     s = nats_JSONParse(&json, "{\"test\":12345678901112}", -1);
//     IFOK(s, nats_JSONGetStr(json, "test", &strVal));
//     testCond((s == NATS_INVALID_ARG) && (json != NULL) && (json->fields != NULL) && (json->fields->used == 1) && (strVal == NULL));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("NULL string with -1 len: ");
//     s = nats_JSONParse(&json, NULL, -1);
//     testCond((s == NATS_INVALID_ARG) && (json == NULL));
//     nats_clearLastError();

//     test("Field reused: ");
//     s = nats_JSONParse(&json, "{\"field\":1,\"field\":2}", -1);
//     IFOK(s, nats_JSONGetInt(json, "field", &intVal));
//     testCond((STILL_OK(s)) && (intVal == 2));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Nested arrays ok: ");
//     jsonMaxNested = 10;
//     s = nats_JSONParse(&json, "{\"test\":[[[1, 2]]]}", -1);
//     testCond(STILL_OK(s));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Nested arrays not ok: ");
//     jsonMaxNested = 10;
//     s = nats_JSONParse(&json, "{\"test\":[[[[[[[[[[[[[1, 2]]]]]]]]]]]]]}", -1);
//     testCond((s == NATS_ERR) && (json == NULL) && (strstr(nats_GetLastError(NULL), " nested arrays of 10") != NULL));
//     nats_clearLastError();

//     test("Nested objects ok: ");
//     s = nats_JSONParse(&json, "{\"test\":{\"a\":{\"b\":{\"c\":1}}}}", -1);
//     testCond(STILL_OK(s));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Nested arrays not ok: ");
//     jsonMaxNested = 10;
//     s = nats_JSONParse(&json, "{\"test\":{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":{\"j\":{\"k\":{\"l\":{\"m\":1}}}}}}}}}}}}}}", -1);
//     testCond((s == NATS_ERR) && (json == NULL) && (strstr(nats_GetLastError(NULL), " nested objects of 10") != NULL));
//     nats_clearLastError();
//     jsonMaxNested = JSON_MAX_NEXTED;

//     // Negative tests
//     {
//         const char *badTimes[] = {
//             "{\"time\":\"too small\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456789-08:00X\"}",
//             "{\"time\":\"2021-06-23T18:22:00X\"}",
//             "{\"time\":\"2021-06-23T18:22:00-0800\"}",
//             "{\"time\":\"2021-06-23T18:22:00-08.00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.abcZ\"}",
//             "{\"time\":\"2021-06-23T18:22:00.abc-08:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234567890-08:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234567890Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123-0800\"}",
//         };
//         const char *errorsTxt[] = {
//             "too small",
//             "too long",
//             "invalid UTC offset",
//             "invalid UTC offset",
//             "invalid UTC offset",
//             "is invalid",
//             "is invalid",
//             "is invalid",
//             "too long",
//             "second fraction",
//             "invalid UTC offset",
//         };
//         for (i = 0; i < (int)(sizeof(errorsTxt) / sizeof(char *)); i++)
//         {
//             longVal = 0;
//             snprintf(buf, sizeof(buf), "Bad time '%s': ", badTimes[i]);
//             test(buf);
//             s = nats_JSONParse(&json, badTimes[i], -1);
//             IFOK(s, nats_JSONGetTime(json, "time", &longVal));
//             testCond((s != NATS_OK) && (json != NULL) && (longVal == 0) && (strstr(nats_GetLastError(NULL), errorsTxt[i]) != NULL));
//             nats_clearLastError();
//             nats_JSONDestroy(json);
//             json = NULL;
//         }
//     }

//     // Positive tests
//     {
//         const char *goodTimes[] = {
//             "{\"time\":\"0001-01-01T00:00:00Z\"}",
//             "{\"time\":\"1970-01-01T01:00:00+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12345Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234567Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12345678Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456789Z\"}",
//             "{\"time\":\"2021-06-23T18:22:00-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12345-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234567-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12345678-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456789-07:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12345+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.1234567+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.12345678+01:00\"}",
//             "{\"time\":\"2021-06-23T18:22:00.123456789+01:00\"}",
//         };
//         int64_t results[] = {
//             0,
//             0,
//             1624472520000000000,
//             1624472520100000000,
//             1624472520120000000,
//             1624472520123000000,
//             1624472520123400000,
//             1624472520123450000,
//             1624472520123456000,
//             1624472520123456700,
//             1624472520123456780,
//             1624472520123456789,
//             1624497720000000000,
//             1624497720100000000,
//             1624497720120000000,
//             1624497720123000000,
//             1624497720123400000,
//             1624497720123450000,
//             1624497720123456000,
//             1624497720123456700,
//             1624497720123456780,
//             1624497720123456789,
//             1624468920000000000,
//             1624468920100000000,
//             1624468920120000000,
//             1624468920123000000,
//             1624468920123400000,
//             1624468920123450000,
//             1624468920123456000,
//             1624468920123456700,
//             1624468920123456780,
//             1624468920123456789,
//         };
//         for (i = 0; i < (int)(sizeof(results) / sizeof(int64_t)); i++)
//         {
//             longVal = 0;
//             snprintf(buf, sizeof(buf), "Time '%s' -> %" PRId64 ": ", goodTimes[i], results[i]);
//             test(buf);
//             s = nats_JSONParse(&json, goodTimes[i], -1);
//             IFOK(s, nats_JSONGetTime(json, "time", &longVal));
//             testCond((STILL_OK(s)) && (json != NULL) && (longVal == results[i]));
//             nats_JSONDestroy(json);
//             json = NULL;
//         }
//     }

//     test("GetStr bad type: ");
//     s = nats_JSONParse(&json, "{\"test\":true}", -1);
//     IFOK(s, nats_JSONRefStr(json, "test", (const char **)&strVal));
//     testCond((s != NATS_OK) && (strVal == NULL));
//     nats_clearLastError();
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("GetStr: ");
//     s = nats_JSONParse(&json, "{\"test\":\"direct\"}", -1);
//     IFOK(s, nats_JSONRefStr(json, "test", (const char **)&strVal));
//     testCond((STILL_OK(s)) && (strVal != NULL) && (strcmp(strVal, "direct") == 0));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("GetBytes bad type: ");
//     s = nats_JSONParse(&json, "{\"test\":true}", -1);
//     IFOK(s, nats_JSONGetBytes(json, "test", &bytes, &bl));
//     testCond((s != NATS_OK) && (bytes == NULL) && (bl == 0));
//     nats_clearLastError();
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("GetBytes: ");
//     s = nats_JSONParse(&json, "{\"test\":\"dGhpcyBpcyB0ZXN0aW5nIGJhc2U2NCBlbmNvZGluZw==\"}", -1);
//     IFOK(s, nats_JSONGetBytes(json, "test", &bytes, &bl));
//     testCond((STILL_OK(s)) && (bytes != NULL) && (bl == 31) && (strncmp((const char *)bytes, "this is testing base64 encoding", bl) == 0));
//     nats_clearLastError();
//     nats_JSONDestroy(json);
//     json = NULL;
//     free(bytes);

//     test("Range with wrong type: ");
//     s = nats_JSONParse(&json, "{\"test\":123}", -1);
//     IFOK(s, nats_JSONRange(json, TYPE_STR, 0, _dummyJSONCb, NULL));
//     testCond((s == NATS_ERR) && (strstr(nats_GetLastError(NULL), "expected value type of")));
//     nats_clearLastError();

//     test("Range with wrong num type: ");
//     s = nats_JSONRange(json, TYPE_NUM, TYPE_INT, _dummyJSONCb, NULL);
//     testCond((s == NATS_ERR) && (strstr(nats_GetLastError(NULL), "expected numeric type of")));
//     nats_clearLastError();

//     test("Range ok: ");
//     ulongVal = 0;
//     s = nats_JSONRange(json, TYPE_NUM, TYPE_UINT, _dummyJSONCb, &ulongVal);
//     testCond((STILL_OK(s)) && (ulongVal == 123));
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Range cb returns error: ");
//     ulongVal = 0;
//     s = nats_JSONParse(&json, "{\"fail\":123}", -1);
//     IFOK(s, nats_JSONRange(json, TYPE_NUM, TYPE_UINT, _dummyJSONCb, &ulongVal));
//     testCond((s == NATS_INVALID_ARG) && (strstr(nats_GetLastError(NULL), "on purpose")));
//     nats_clearLastError();
//     nats_JSONDestroy(json);
//     json = NULL;

//     test("Parse empty array: ");
//     s = nats_JSONParse(&json, "{\"empty\":[]}", -1);
//     testCond(STILL_OK(s));

//     test("Get empty array array: ");
//     s = nats_JSONGetArrayArray(json, "empty", &arrArrVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrArrVal == NULL) && (arrCount == 0));

//     test("Get empty obj array: ");
//     s = nats_JSONGetArrayObject(json, "empty", &arrObjVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrObjVal == NULL) && (arrCount == 0));

//     test("Get empty ulong array: ");
//     s = nats_JSONGetArrayULong(json, "empty", &arrULongVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrULongVal == NULL) && (arrCount == 0));

//     test("Get empty long array: ");
//     s = nats_JSONGetArrayLong(json, "empty", &arrLongVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrLongVal == NULL) && (arrCount == 0));

//     test("Get empty int array: ");
//     s = nats_JSONGetArrayInt(json, "empty", &arrIntVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrIntVal == NULL) && (arrCount == 0));

//     test("Get empty double array: ");
//     s = nats_JSONGetArrayDouble(json, "empty", &arrDoubleVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrDoubleVal == NULL) && (arrCount == 0));

//     test("Get empty bool array: ");
//     s = nats_JSONGetArrayBool(json, "empty", &arrBoolVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrBoolVal == NULL) && (arrCount == 0));

//     test("Get empty string array: ");
//     s = nats_JSONDupStringArray(json, "empty", &arrVal, &arrCount);
//     testCond((STILL_OK(s)) && (arrVal == NULL) && (arrCount == 0));

//     nats_JSONDestroy(json);
//     json = NULL;
// }
