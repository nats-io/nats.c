// Copyright 2026 The NATS Authors
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

#include <stdio.h>
#include <string.h>

// This example reads the server's INFO line over a raw TCP connection using
// the POSIX sockets API, so it builds on Unix-like systems (Linux, macOS)
// only. On Windows the equivalent would use the Winsock API (winsock2.h).
#if defined(_WIN32)
int main(void)
{
    fprintf(stderr,
            "This example uses POSIX sockets and is not supported on Windows.\n");
    return 0;
}
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv)
{
    // NATS-DOC-START
    // Raw POSIX/BSD sockets -- Unix-like systems (Linux, macOS) only.
    // The server sends first. Open a raw TCP connection to port 4222 --
    // no NATS library involved -- and the server immediately sends its
    // INFO line: plain text, ending in CRLF, describing itself and the
    // limits it enforces (headers support, max_payload, ...).
    struct sockaddr_in addr;
    char               buf[4096];
    ssize_t            n;
    int                fd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(4222);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0)
    {
        perror("connect");
        return 2;
    }

    // Read what the server sent on connect and print it. This is the
    // first half of the handshake; a real client would now reply with
    // its own CONNECT line, which the library does for you.
    n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0)
    {
        buf[n] = '\0';
        printf("%s", buf);
    }
    // NATS-DOC-END

    close(fd);

    return 0;
}
#endif
