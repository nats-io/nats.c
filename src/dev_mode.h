// Copyright 2015-2022 The NATS Authors
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

#ifndef DEV_MODE_H_
#define DEV_MODE_H_

#ifndef _WIN32
#define __SHORT_FILE__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#else
#define __SHORT_FILE__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#endif

#define DEVNOLOG(s)
#define DEVNOLOGf(fmt, ...)
#define DEVNOLOGx(file, line, func, fmt, ...)

// Comment/uncomment to enable debug logging and tracking.
#define DEV_MODE (1)

#ifdef DEV_MODE

// Comment/uncomment to enable debug logging and tracking in specific modules.
// #define DEV_MODE_CONN
// #define DEV_MODE_CONN_TRACE
#define DEV_MODE_MEM_HEAP
// #define DEV_MODE_MEM_POOL
// #define DEV_MODE_MEM_POOL_TRACE
// #define DEV_MODE_JSON

#define DEV_MODE_DEFAULT_LOG_LEVEL DEV_MODE_TRACE

//-----------------------------------------------------------------------------
// Dev mode stuff...

#define DEV_MODE_CTX , __SHORT_FILE__, __LINE__, __NATS_FUNCTION__
#define DEV_MODE_PASSARGS , file, line, func
#define DEV_MODE_ARGS , const char *file, int line, const char *func

#define DEV_MODE_ERROR 1
#define DEV_MODE_WARN 2
#define DEV_MODE_INFO 3
#define DEV_MODE_DEBUG 4
#define DEV_MODE_TRACE 5

#define DEV_MODE_LEVEL_STR(level) (level == DEV_MODE_ERROR ? "ERROR" : (level == DEV_MODE_WARN ? "WARN" : (level == DEV_MODE_INFO ? "INFO" : (level == DEV_MODE_DEBUG ? "DEBUG" : "TRACE"))))

extern int nats_devmode_log_level;

#define DEVLOGx(level, module, file, line, func, fmt, ...) \
    ((level <= nats_devmode_log_level) ? fprintf(stderr, "%5s: %6s: " fmt " (%s:%s:%d)\n", DEV_MODE_LEVEL_STR(level), (module), __VA_ARGS__, (func), (file), (line)) : 0)
#define DEVLOG(level, module, str) DEVLOGx((level), (module), __SHORT_FILE__, __LINE__, __func__, "%s", str)
#define DEVLOGf(level, module, fmt, ...) DEVLOGx((level), (module), __SHORT_FILE__, __LINE__, __func__, fmt, __VA_ARGS__)

const char *natsString_debugPrintable(natsString *buf, size_t limit);
const char *natsString_debugPrintableN(const uint8_t *data, size_t len, size_t limit);
const char *natsString_debugPrintableC(const char *buf, size_t limit);
const char *natsPool_debugPrintable(natsString *buf, natsPool *pool, size_t limit);

#else // DEV_MODE

#define DEV_MODE_ARGS
#define DEV_MODE_CTX
#define DEVLOGx(level, module, file, line, func, fmt, ...)
#define DEVLOG(level, module, str)
#define DEVLOGf(level, module, fmt, ...)

#endif // DEV_MODE

#define DEVERROR(module, str) DEVLOG(DEV_MODE_ERROR, (module), (str))
#define DEVERRORf(module, fmt, ...) DEVLOGf(DEV_MODE_ERROR, (module), fmt, __VA_ARGS__)
#define DEVERRORx(module, file, line, func, fmt, ...) DEVLOGx(DEV_MODE_ERROR, (module), (file), (line), (func), fmt, __VA_ARGS__)
#define DEVWARN(module, str) DEVLOG(DEV_MODE_WARN, (module), (str))
#define DEVWARNf(module, fmt, ...) DEVLOGf(DEV_MODE_WARN, (module), fmt, __VA_ARGS__)
#define DEVWARNx(module, file, line, func, fmt, ...) DEVLOGx(DEV_MODE_WARN, (module), (file), (line), (func), fmt, __VA_ARGS__)
#define DEVINFO(module, str) DEVLOG(DEV_MODE_INFO, (module), (str))
#define DEVINFOf(module, fmt, ...) DEVLOGf(DEV_MODE_INFO, (module), fmt, __VA_ARGS__)
#define DEVINFOx(module, file, line, func, fmt, ...) DEVLOGx(DEV_MODE_INFO, (module), (file), (line), (func), fmt, __VA_ARGS__)
#define DEVDEBUG(module, str) DEVLOG(DEV_MODE_DEBUG, (module
#define DEVDEBUGf(module, fmt, ...) DEVLOGf(DEV_MODE_DEBUG, (module), fmt, __VA_ARGS__)
#define DEVDEBUGx(module, file, line, func, fmt, ...) DEVLOGx(DEV_MODE_DEBUG, (module
#define DEVTRACE(module, str) DEVLOG(DEV_MODE_TRACE, (module), (str))
#define DEVTRACEf(module, fmt, ...) DEVLOGf(DEV_MODE_TRACE, (module), fmt, __VA_ARGS__)
#define DEVTRACEx(module, file, line, func, fmt, ...) DEVLOGx(DEV_MODE_TRACE, (module), (file), (line), (func), fmt, __VA_ARGS__)

#endif /* DEV_MODE_H_ */
