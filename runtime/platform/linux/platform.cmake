add_definitions(-DPLATFORM_LINUX)

include_directories(${PLATFORM_DIR})
include_directories(${PLATFORM_DIR}/../include)

include(${PLATFORM_DIR}/../common/posix/platform_posix.cmake)

file (GLOB_RECURSE source_all ${PLATFORM_DIR}/*.c)

set (PLATFORM_SOURCE 
    ${source_all}
    ${PLATFORM_POSIX_SOURCE}
)