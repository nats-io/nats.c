/*
 * DO NOT EDIT! Generated during make process.
 * Copyright 2015-2018 The NATS Authors
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VERSION_H_
#define VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#define NATS_VERSION_MAJOR  1
#define NATS_VERSION_MINOR  8
#define NATS_VERSION_PATCH  0

#define NATS_VERSION_STRING "1.8.0-beta"
			 				  
#define NATS_VERSION_NUMBER ((NATS_VERSION_MAJOR << 16) | \
                             (NATS_VERSION_MINOR <<  8) | \
                             NATS_VERSION_PATCH)
                             
#define NATS_VERSION_REQUIRED_NUMBER 0x010100

#ifdef __cplusplus
}
#endif

#endif /* VERSION_H_ */
