#------------------------
# Add the sub-directories
#------------------------
include_directories(include)
include_directories(${NATS_PLATFORM_INCLUDE})

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
add_library(nats_static STATIC ${SOURCES} ${PS_SOURCES})

# --------------------------------------
# Install the libraries and header files
# --------------------------------------
install(TARGETS nats DESTINATION lib)
install(TARGETS nats_static DESTINATION lib)
install(FILES nats.h status.h version.h DESTINATION include)

