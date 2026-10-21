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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "rules_cc/cc/runfiles/runfiles.h"

extern "C" int cnats_test_main(int argc, char **argv);

namespace fs = std::filesystem;
using rules_cc::cc::runfiles::Runfiles;

static void set_env(const char *name, const std::string &value)
{
#ifdef _WIN32
    int status = _putenv_s(name, value.c_str());
#else
    int status = setenv(name, value.c_str(), 1);
#endif
    if (status != 0)
        throw std::runtime_error(std::string("Unable to set ") + name);
}

int main(int argc, char **argv)
{
    try
    {
        const char *temporary = std::getenv("TEST_TMPDIR");
        if (argc < 2 || temporary == nullptr)
            throw std::runtime_error("Expected a test name and Bazel TEST_TMPDIR");
        std::string error;
        std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
        if (!runfiles)
            throw std::runtime_error(error);
        const fs::path workdir = fs::absolute(temporary);
        fs::create_directories(workdir);
        bool server = false;
#ifdef _WIN32
        const char *server_exe = ".\\nats-server.exe";
        const char *version_command = ".\\nats-server.exe -v";
#else
        const char *server_exe = "./nats-server";
        const char *version_command = "./nats-server -v";
#endif
        for (int i = 2; i < argc; ++i)
        {
            std::string arg = argv[i];
            std::string source;
            fs::path destination;
            if (arg.compare(0, 10, "--fixture=") == 0)
            {
                size_t separator = arg.find('=', 10);
                if (separator == std::string::npos)
                    throw std::runtime_error("Invalid fixture: " + arg);
                destination = workdir / arg.substr(10, separator - 10);
                source = arg.substr(separator + 1);
            }
            else if (arg.compare(0, 9, "--server=") == 0)
            {
                destination = workdir / server_exe;
                source = arg.substr(9);
                server = true;
            }
            else
                throw std::runtime_error("Unknown setup argument: " + arg);
            fs::create_directories(destination.parent_path());
            fs::copy_file(runfiles->Rlocation(source), destination, fs::copy_options::overwrite_existing);
            fs::permissions(destination, fs::perms::owner_write, fs::perm_options::add);
        }
        fs::current_path(workdir);
        // A C test inspects the native server log, so retain its log redirection.
        set_env("NATS_TEST_KEEP_SERVER_OUTPUT", "");
        if (server)
        {
#ifdef _WIN32
            FILE *version = _popen(version_command, "r");
#else
            FILE *version = popen(version_command, "r");
#endif
            if (version == nullptr)
                throw std::runtime_error("Unable to read the NATS server version");
            std::string output;
            char buffer[256];
            while (std::fgets(buffer, sizeof(buffer), version) != nullptr)
                output += buffer;
#ifdef _WIN32
            int status = _pclose(version);
#else
            int status = pclose(version);
#endif
            if (status != 0 || output.empty())
                throw std::runtime_error("NATS server version command failed");
            set_env("NATS_TEST_SERVER_EXE", server_exe);
            set_env("NATS_TEST_SERVER_VERSION", output);
        }
        char *test_argv[] = {argv[0], argv[1], nullptr};
        return cnats_test_main(2, test_argv);
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Test setup failed: %s\n", error.what());
        return 1;
    }
}
