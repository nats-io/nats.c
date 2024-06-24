cmake_minimum_required(VERSION 3.10)

# Uncomment to have the build process verbose
set(CMAKE_VERBOSE_MAKEFILE TRUE)

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
file(GLOB TEST_SOURCES RELATIVE ${PROJECT_SOURCE_DIR}/test *.c)

# Build the test program, link statically with the library.
add_executable(natstest ${TEST_SOURCES})
target_link_libraries(natstest nats_static ${NATS_EXTRA_LIB})

set (LIST_FILE ${PROJECT_SOURCE_DIR}/test/all_tests.txt)

# Read the file 'list.txt' to get all the test names
if(EXISTS ${LIST_FILE})
  file(STRINGS ${LIST_FILE} TEST_NAMES)
else()
    set(TEST_NAMES)
endif()

message("Test Names: ${TEST_NAMES}")

foreach(name ${TEST_NAMES})
  # Remove the _tc() prefix
  string(REGEX REPLACE "_tc\\(([^)]+)\\)" "\\1" TEST_NAME ${name})
  add_test(NAME Test_${TEST_NAME}
          WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
          COMMAND natstest ${TEST_NAME}) 

  # Make sure the test passes
  set_tests_properties(Test_${TEST_NAME} PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASSED")
endforeach()