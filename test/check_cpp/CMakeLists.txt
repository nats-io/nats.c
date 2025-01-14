if(NOT BUILD_TESTING)
  return()
endif()

# We need this to build the test program
include_directories(${PROJECT_SOURCE_DIR}/src)

# Build the test program
add_executable(check_cpp check_cpp.cpp)

target_link_libraries(check_cpp nats ${NATS_EXTRA_LIB})
