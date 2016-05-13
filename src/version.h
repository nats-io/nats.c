/*
 * DO NOT EDIT! Generated during make process.
 * Copyright 2015 Apcera Inc. All rights reserved.
 */

#ifndef VERSION_H_
#define VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#define NATS_VERSION_MAJOR  1
#define NATS_VERSION_MINOR  3
#define NATS_VERSION_PATCH  6

#define NATS_VERSION_STRING "1.3.6"
			 				  
#define NATS_VERSION_NUMBER ((NATS_VERSION_MAJOR << 16) | \
                             (NATS_VERSION_MINOR <<  8) | \
                             NATS_VERSION_PATCH)
                             
#define NATS_VERSION_REQUIRED_NUMBER 0x010100                             

#ifdef __cplusplus
}
#endif

#endif /* VERSION_H_ */
