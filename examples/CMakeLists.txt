# We need this directory to build the examples
include_directories(${PROJECT_SOURCE_DIR}/src)

# Get all the .c files in the examples directory
file(GLOB EXAMPLES_SOURCES RELATIVE ${PROJECT_SOURCE_DIR}/examples *.c)

# For each file...
foreach(examples_src ${EXAMPLES_SOURCES})

    # Remove the suffix so that it becomes the executable name
    string(REPLACE ".c" "" examplename ${examples_src})
    set(exampleexe "nats-${examplename}")

    # Build the executable
    add_executable(${exampleexe} ${PROJECT_SOURCE_DIR}/examples/${examples_src} )

    # Link dynamically
    target_link_libraries(${exampleexe} nats)

endforeach()

