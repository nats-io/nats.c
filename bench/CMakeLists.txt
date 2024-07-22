if(NOT NATS_BUILD_LIB_STATIC)
  MESSAGE(FATAL_ERROR
    "Building tests require static library, or run CMake with -DBUILD_TESTING=OFF")
  return()
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/bench)

if(NATS_BUILD_WITH_TLS)
  include_directories(${OPENSSL_INCLUDE_DIR})
endif(NATS_BUILD_WITH_TLS)
if(NATS_BUILD_LIBUV_EXAMPLE AND LIBUV_DIR)
  include_directories(${LIBUV_DIR}/include)
endif()
if(NATS_BUILD_LIBEVENT_EXAMPLE AND LIBEVENT_DIR)
  include_directories(${LIBEVENT_DIR}/include)
endif()
if(NATS_BUILD_STREAMING)
  include_directories(${NATS_PROTOBUF_INCLUDE_DIRS})
  include_directories(${PROJECT_SOURCE_DIR}/src/stan)
endif(NATS_BUILD_STREAMING)


set(LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})

# Get all the .c files in the examples directory
file(GLOB BENCH_SOURCES RELATIVE ${PROJECT_SOURCE_DIR}/bench *.c)

# For each file...
foreach(bsrc ${BENCH_SOURCES})
    # Remove the suffix so that it becomes the executable name
    string(REPLACE ".c" "" bname ${bsrc})
    set(bexe "bench-${bname}")

    add_executable(${bexe} ${PROJECT_SOURCE_DIR}/bench/${bsrc})

    target_link_libraries(${bexe} nats_static ${NATS_EXTRA_LIB} ${NATS_ASYNC_IO_LIB})
endforeach()
