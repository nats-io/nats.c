if(NOT BUILD_TESTING)
  return()
endif()

if(NOT NATS_BUILD_LIB_STATIC)
  MESSAGE(FATAL_ERROR
          "Building tests require static library, or run CMake with -DBUILD_TESTING=OFF")
  return()
endif()

# We need this to build the test program
include_directories(${PROJECT_SOURCE_DIR}/src)

if(NATS_BUILD_WITH_TLS)
  include_directories(${OPENSSL_INCLUDE_DIR})
endif(NATS_BUILD_WITH_TLS)

# Build the test program
add_executable(nocrashonexit nocrashonexit.c)

target_link_libraries(nocrashonexit nats_static ${NATS_EXTRA_LIB})
