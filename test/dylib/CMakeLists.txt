if(NOT BUILD_TESTING)
  return()
endif()  

# We need this to build the test program
include_directories(${PROJECT_SOURCE_DIR}/src)

# Build the test program
add_executable(nonats nonats.c)

# Link dynamically with the library
add_definitions(-Dnats_IMPORTS)

target_link_libraries(nonats nats ${NATS_EXTRA_LIB})
