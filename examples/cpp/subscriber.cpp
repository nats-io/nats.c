// Copyright 2015-2020 The NATS Authors
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

#include "nats.hpp"
#include <iostream>

class ErrHandler {
public:
    ErrHandler(const char* id) : id_(id) {}

    void handle(nats::Connection & nc, nats::Subscription & sub, natsStatus err)
    {
        std::cout << id_ << " error:" << nats::GetText(err) << std::endl;
    }

private:
    const char* id_;
};

class Handler {
public:
    Handler(const char* id) : id_(id) {}

    void msg(nats::Connection & nc, nats::Subscription & sub, nats::Msg && msg)
    {
        std::string data(msg.GetData(), msg.GetDataLength());
        std::cout << id_ << " received: " << msg.GetSubject() << " - " << data << std::endl;
    }

private:
    const char* id_;
};

int main(int argc, char **argv)
{
    Handler hh("HandlerId");
    ErrHandler eh("MyErrorHandler");

    nats::Open(10);

    nats::Options options;
    options.SetErrorHandler<ErrHandler, &ErrHandler::handle>(&eh);

    nats::Connection connection(options);
    nats::Subscription sub = connection.Subscribe<Handler, &Handler::msg>("subject", &hh);

    nats::Statistics stat;

    for (;;)
        nats::Sleep(1000);

    nats::Close();

    return 0;
}
