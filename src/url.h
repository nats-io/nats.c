// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef URL_H_
#define URL_H_

#include "status.h"

typedef struct __natsUrl
{
    char    *fullUrl;
    char    *host;
    int     port;
    char    *username;
    char    *password;

} natsUrl;

natsStatus
natsUrl_Create(natsUrl **newUrl, const char *urlStr);

void
natsUrl_Destroy(natsUrl *url);

#endif /* SRC_URL_H_ */
