if(NOT NATS_BUILD_EXAMPLES)
  return()
endif()

# We need this directory to build the examples
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/examples)

if(NATS_BUILD_LIBUV_EXAMPLE)
  include_directories(${LIBUV_DIR}/include)
endif()

if(NATS_BUILD_LIBEVENT_EXAMPLE)
  include_directories(${LIBEVENT_DIR}/include)
endif()

if(WIN32)
  set(LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})
else()
  if(NATS_BUILD_STATIC_EXAMPLES)
    set(LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})
  else()
    set(LIB_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX})
  endif()
endif()

# Get all the .c files in the examples directory
file(GLOB EXAMPLES_SOURCES RELATIVE ${PROJECT_SOURCE_DIR}/examples *.c)

if(NOT NATS_BUILD_STATIC_EXAMPLES)
  add_definitions(-Dnats_IMPORTS)
endif()

# For each file...
foreach(examples_src ${EXAMPLES_SOURCES})

    # Remove the suffix so that it becomes the executable name
    string(REPLACE ".c" "" examplename ${examples_src})
    set(exampleexe "nats-${examplename}")

    set(buildExample OFF)
    set(NATS_ASYNC_IO_LIB "")

    if(examplename MATCHES "libuv")
      if(NATS_BUILD_LIBUV_EXAMPLE)
        set(buildExample ON)
        set(NATS_ASYNC_IO_LIB ${LIBUV_DIR}/lib/libuv${LIB_SUFFIX})
      endif()
    elseif(examplename MATCHES "libevent")
      if(NATS_BUILD_LIBEVENT_EXAMPLE)
        set(buildExample ON)
        if(WIN32)
          set(NATS_ASYNC_IO_LIB ${LIBEVENT_DIR}/lib/event${LIB_SUFFIX})
        else()
          set(NATS_ASYNC_IO_LIB ${LIBEVENT_DIR}/lib/libevent${LIB_SUFFIX} ${LIBEVENT_DIR}/lib/libevent_pthreads${LIB_SUFFIX})
        endif()
      endif()
    else()
      set(buildExample ON)
    endif()

    if(buildExample)
      # Build the executable
      add_executable(${exampleexe} ${PROJECT_SOURCE_DIR}/examples/${examples_src})

      # Link
      if(NATS_BUILD_STATIC_EXAMPLES)
        target_link_libraries(${exampleexe} nats_static ${NATS_EXTRA_LIB} ${NATS_ASYNC_IO_LIB})
      else()
        target_link_libraries(${exampleexe} nats ${NATS_EXTRA_LIB} ${NATS_ASYNC_IO_LIB})
      endif()
    endif()

endforeach()
