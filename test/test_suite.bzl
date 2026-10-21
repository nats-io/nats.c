"""Individual Bazel targets for the existing C tests."""

load("@rules_cc//cc:cc_test.bzl", "cc_test")

def cnats_test_suite(name, test_names, fixtures, server = None):
    """Declare one cc_test per C test and a test_suite grouping those targets.

    Args:
        name: Suite name, also used to prefix each test target.
        test_names: Names accepted by the C test program.
        fixtures: Files copied into each test's writable directory.
        server: Optional multitool server executable for integration tests.
    """
    args = ["--fixture=" + f + "=$(rlocationpath " + f + ")" for f in fixtures]
    data = fixtures
    if server:
        args.append("--server=$(rlocationpath " + server + ")")
        data = data + [server]
    tests = []
    for test_name in test_names:
        target = name + "_" + test_name
        cc_test(
            name = target,
            size = "small",
            timeout = "short",
            args = [test_name] + args,
            data = data,
            linkstatic = True,
            tags = ["exclusive", "manual"] if server else [],
            deps = [":test_setup"],
        )
        tests.append(":" + target)
    native.test_suite(
        name = name,
        tags = ["manual"] if server else [],
        tests = tests,
    )
