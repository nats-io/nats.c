#------------------------
# Add the sub-directories
#------------------------
include_directories(include)
include_directories(${NATS_PLATFORM_INCLUDE})
if(NATS_BUILD_WITH_TLS)
include_directories(${OPENSSL_INCLUDE_DIR})
set(NATS_OPENSSL_LIBS "${OPENSSL_LIBRARIES}")
endif(NATS_BUILD_WITH_TLS)

#---------------------------------------
# Grab all files in 'src' and 'src/unix'
# or 'src/win' depending on the platform
#---------------------------------------
file(GLOB SOURCES "*.c")
file(GLOB PS_SOURCES "${NATS_PLATFORM_INCLUDE}/*.c")

# --------------------------------------
# Create the shared and static libraries
# --------------------------------------
add_library(nats SHARED ${SOURCES} ${PS_SOURCES})
target_link_libraries(nats ${NATS_OPENSSL_LIBS} ${NATS_EXTRA_LIB})
add_library(nats_static STATIC ${SOURCES} ${PS_SOURCES})
target_link_libraries(nats_static ${NATS_OPENSSL_LIBS})

# --------------------------------------
# Install the libraries and header files
# --------------------------------------
install(TARGETS nats DESTINATION lib)
install(TARGETS nats_static DESTINATION lib)
install(FILES deprnats.h DESTINATION include RENAME nats.h)
install(FILES nats.h status.h version.h DESTINATION include/nats)
install(FILES adapters/libevent.h adapters/libuv.h DESTINATION include/nats/adapters)
