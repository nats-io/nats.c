#------------------------
# Add the sub-directories
#------------------------
include_directories(include)
include_directories(${NATS_PLATFORM_INCLUDE})
include_directories(glib)

if(NATS_BUILD_WITH_TLS)
  include_directories(${OPENSSL_INCLUDE_DIR})
  set(NATS_OPENSSL_LIBS "${OPENSSL_LIBRARIES}")
endif(NATS_BUILD_WITH_TLS)

if(NATS_BUILD_STREAMING)
  include_directories(${NATS_PROTOBUF_INCLUDE_DIRS})
  include_directories(stan)
endif(NATS_BUILD_STREAMING)

if(NATS_BUILD_USE_SODIUM)
  include_directories(${NATS_SODIUM_INCLUDE_DIRS})
endif(NATS_BUILD_USE_SODIUM)

#---------------------------------------
# Grab all files in 'src' and 'src/unix'
# or 'src/win' depending on the platform
#---------------------------------------
file(GLOB SOURCES "*.c")
file(GLOB PS_SOURCES "${NATS_PLATFORM_INCLUDE}/*.c")
file(GLOB GLIB_SOURCES "glib/*.c")

# Add stan directory if building with Streaming support
if(NATS_BUILD_STREAMING)
  file(GLOB S_SOURCES "stan/*.c")
endif(NATS_BUILD_STREAMING)

# --------------------------------------
# Create the shared and static libraries
# --------------------------------------
if(NATS_BUILD_LIB_SHARED)
  add_library(nats SHARED ${SOURCES} ${GLIB_SOURCES} ${PS_SOURCES} ${S_SOURCES})
  target_link_libraries(nats ${NATS_OPENSSL_LIBS} ${NATS_EXTRA_LIB} ${NATS_PROTOBUF_LIBRARIES} ${NATS_SODIUM_LIBRARIES})
  set_target_properties(nats PROPERTIES
    VERSION ${NATS_VERSION_MAJOR}.${NATS_VERSION_MINOR}.${NATS_VERSION_PATCH}
    SOVERSION ${NATS_VERSION_MAJOR}.${NATS_VERSION_MINOR})
endif(NATS_BUILD_LIB_SHARED)

if(NATS_BUILD_LIB_STATIC)
  add_library(nats_static STATIC ${SOURCES} ${GLIB_SOURCES} ${PS_SOURCES} ${S_SOURCES})
  target_link_libraries(nats_static ${NATS_OPENSSL_LIBS} ${NATS_PROTOBUF_LIBRARIES} ${NATS_SODIUM_LIBRARIES})
  set_target_properties(nats_static PROPERTIES
    VERSION ${NATS_VERSION_MAJOR}.${NATS_VERSION_MINOR}.${NATS_VERSION_PATCH}
    SOVERSION ${NATS_VERSION_MAJOR}.${NATS_VERSION_MINOR})
  target_compile_definitions(nats_static PRIVATE -DNATS_STATIC)
endif(NATS_BUILD_LIB_STATIC)

# --------------------------------------
# Install the libraries and header files
# --------------------------------------


if(NATS_BUILD_LIB_SHARED)
  set_property(TARGET nats PROPERTY DEBUG_POSTFIX d)
  target_include_directories(nats PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)
  install(TARGETS nats EXPORT cnats-targets DESTINATION ${NATS_LIBDIR})
  install(EXPORT cnats-targets
        NAMESPACE cnats::
        FILE cnats-config.cmake
        DESTINATION ${NATS_LIBDIR}/cmake/cnats)
  install(FILES "${PROJECT_BINARY_DIR}/cnats-config-version.cmake"
        DESTINATION ${NATS_LIBDIR}/cmake/cnats)
endif(NATS_BUILD_LIB_SHARED)

if(NATS_BUILD_LIB_STATIC)
  set_property(TARGET nats_static PROPERTY DEBUG_POSTFIX d)
  target_include_directories(nats_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)
  install(TARGETS nats_static EXPORT cnats-targets ARCHIVE DESTINATION ${NATS_LIBDIR})
  install(EXPORT cnats-targets
        NAMESPACE cnats::
        FILE cnats-config.cmake
        DESTINATION ${NATS_LIBDIR}/cmake/cnats)
  install(FILES "${PROJECT_BINARY_DIR}/cnats-config-version.cmake"
        DESTINATION ${NATS_LIBDIR}/cmake/cnats)
endif(NATS_BUILD_LIB_STATIC)

install(FILES deprnats.h DESTINATION ${NATS_INCLUDE_DIR} RENAME nats.h)
install(FILES nats.h status.h version.h DESTINATION ${NATS_INCLUDE_DIR}/nats)
install(FILES adapters/libevent.h adapters/libuv.h DESTINATION ${NATS_INCLUDE_DIR}/nats/adapters)
