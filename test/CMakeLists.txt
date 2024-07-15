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
add_executable(testsuite test.c)

# Link statically with the library
target_link_libraries(testsuite nats_static ${NATS_EXTRA_LIB})

# Set the test index to 0
set(testIndex 0)

# Read the file 'list.txt' to get all the test names
file(STRINGS list.txt listOfTestNames)

# For each test name
foreach(name ${listOfTestNames})

  # Create a test and pass the index (start and end are the same)
  # to the testsuite executable
  add_test(NAME Test_${name}
           WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
           COMMAND testsuite ${testIndex} ${testIndex})

  # Make sure the test passes
  set_tests_properties(Test_${name} PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASSED")

  # Bump the test index number
  math(EXPR testIndex "${testIndex}+1")
endforeach()


