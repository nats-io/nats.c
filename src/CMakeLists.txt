#------------------------
# Add the sub-directories
#------------------------
include_directories(include)
include_directories(${NATS_PLATFORM_INCLUDE})

if(NATS_BUILD_WITH_TLS)
  include_directories(${OPENSSL_INCLUDE_DIR})
  set(NATS_OPENSSL_LIBS "${OPENSSL_LIBRARIES}")
endif(NATS_BUILD_WITH_TLS)

if(NATS_BUILD_STREAMING)
  include_directories(${PROTOBUF_DIR}/include)
  include_directories(stan)
endif(NATS_BUILD_STREAMING)

#---------------------------------------
# Grab all files in 'src' and 'src/unix'
# or 'src/win' depending on the platform
#---------------------------------------
file(GLOB SOURCES "*.c")
file(GLOB PS_SOURCES "${NATS_PLATFORM_INCLUDE}/*.c")

# Add stan directory if building with Streaming support
if(NATS_BUILD_STREAMING)
  file(GLOB S_SOURCES "stan/*.c")
endif(NATS_BUILD_STREAMING)

# --------------------------------------
# Create the shared and static libraries
# --------------------------------------
add_library(nats SHARED ${SOURCES} ${PS_SOURCES} ${S_SOURCES})
target_link_libraries(nats ${NATS_OPENSSL_LIBS} ${NATS_EXTRA_LIB} ${NATS_PROTOBUF_LIB})
add_library(nats_static STATIC ${SOURCES} ${PS_SOURCES} ${S_SOURCES})
target_link_libraries(nats_static ${NATS_OPENSSL_LIBS} ${NATS_PROTOBUF_LIB})

# --------------------------------------
# Install the libraries and header files
# --------------------------------------
install(TARGETS nats DESTINATION lib)
install(TARGETS nats_static DESTINATION lib)
install(FILES deprnats.h DESTINATION include RENAME nats.h)
install(FILES nats.h status.h version.h DESTINATION include/nats)
install(FILES adapters/libevent.h adapters/libuv.h DESTINATION include/nats/adapters)
