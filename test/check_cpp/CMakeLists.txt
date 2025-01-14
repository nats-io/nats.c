if(NOT BUILD_TESTING)
  return()
endif()

# We need this to build the test program
include_directories(${PROJECT_SOURCE_DIR}/src)

if(NATS_BUILD_WITH_TLS)
  include_directories(${OPENSSL_INCLUDE_DIR})
endif(NATS_BUILD_WITH_TLS)

# Build the test program
add_executable(check_cpp check_cpp.cpp)

target_link_libraries(check_cpp nats ${NATS_EXTRA_LIB})
