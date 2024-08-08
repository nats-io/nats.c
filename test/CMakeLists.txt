cmake_minimum_required(VERSION 3.10)

enable_testing()

if(NOT BUILD_TESTING)
  return()
endif()

if(NOT NATS_BUILD_LIB_STATIC)
  MESSAGE(FATAL_ERROR
    "Building tests require static library, or run CMake with -DBUILD_TESTING=OFF")
  return()
endif()

if(MSVC)
  set_source_files_properties(test.c PROPERTIES COMPILE_FLAGS "/w")
endif()

# We need this to build the test program
include_directories(${PROJECT_SOURCE_DIR}/src)

if(NATS_BUILD_WITH_TLS)
  include_directories(${OPENSSL_INCLUDE_DIR})
endif(NATS_BUILD_WITH_TLS)

if(NATS_BUILD_STREAMING)
  include_directories(${NATS_PROTOBUF_INCLUDE_DIRS})
  include_directories(${PROJECT_SOURCE_DIR}/src/stan)
endif(NATS_BUILD_STREAMING)

# Build the test program
add_executable(testsuite test.c bench_sub_async.c)

# Link statically with the library
target_link_libraries(testsuite nats_static ${NATS_EXTRA_LIB})

set(BENCH_LIST ${PROJECT_SOURCE_DIR}/test/list_bench.txt)
set(STAN_LIST ${PROJECT_SOURCE_DIR}/test/list_stan.txt)

list(APPEND ALL_LISTS ${PROJECT_SOURCE_DIR}/test/list_test.txt)
list(APPEND ALL_LISTS ${BENCH_LIST})
if(NATS_BUILD_STREAMING)
list(APPEND ALL_LISTS ${STAN_LIST})
message("Building Streaming tests" ${ALL_LISTS})
endif()

set(TEST_NAMES)

foreach(LIST_FILE ${ALL_LISTS})
  # Get all the test names
  if(EXISTS ${LIST_FILE})
    file(STRINGS ${LIST_FILE} TEST_NAMES)
  else()
    set(TEST_NAMES)
  endif()

  foreach(name ${TEST_NAMES})
    # Remove the _test() prefix
    string(REGEX REPLACE "_test\\(([^)]+)\\)" "\\1" TEST_NAME ${name})
    add_test(NAME ${TEST_NAME}
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
      COMMAND testsuite ${TEST_NAME})

    if(${LIST_FILE} STREQUAL ${BENCH_LIST})
      set_tests_properties(${TEST_NAME} PROPERTIES LABELS "bench")
    else()
      # Make sure the test passes
      set_tests_properties(${TEST_NAME} PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASSED")

      # Set TSAN_OPTIONS for the test
      if(NATS_SANITIZE)
        set_tests_properties(${TEST_NAME} PROPERTIES
          ENVIRONMENT "TSAN_OPTIONS=detect_deadlocks=1:second_deadlock_stack=1:halt_on_error=1:report_signal_unsafe=1")
      endif(NATS_SANITIZE)

      set_tests_properties(${TEST_NAME} PROPERTIES LABELS "test")
    endif()
  endforeach()
endforeach()
