// Copyright 2017-2025 The NATS Authors
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

#include <iostream>
#include <nats.h>

// Check C++ compiler compatibility of structs in
// natsp.h to avoid incomplete type errors such as C2079.
#include <natsp.h>

int main(int argc, char **argv)
{
    // jsOptionsPullSubscribeAsync opts should be accessible
    jsFetch fetch;
    fetch.opts.Timeout = 1;

    std::cout << "OK" << std::endl;
    return 0;
}
