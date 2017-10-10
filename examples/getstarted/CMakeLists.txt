if(NOT NATS_BUILD_EXAMPLES)
  return()
endif()

# We need this directory to build the examples
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/examples/getstarted)

# Get all the .c files in the examples directory
file(GLOB EXAMPLES_SOURCES RELATIVE ${PROJECT_SOURCE_DIR}/examples/getstarted *.c)

if(NOT NATS_BUILD_STATIC_EXAMPLES)
  add_definitions(-Dnats_IMPORTS)
endif()

# For each file...
foreach(examples_src ${EXAMPLES_SOURCES})

    # Remove the suffix so that it becomes the executable name
    string(REPLACE ".c" "" examplename ${examples_src})
    set(exampleexe "${examplename}")

    # Build the executable
    add_executable(${exampleexe} ${PROJECT_SOURCE_DIR}/examples/getstarted/${examples_src})

    # Link
    if(NATS_BUILD_STATIC_EXAMPLES)
		target_link_libraries(${exampleexe} nats_static ${NATS_EXTRA_LIB})
	else()
		target_link_libraries(${exampleexe} nats ${NATS_EXTRA_LIB})
	endif()

endforeach()
