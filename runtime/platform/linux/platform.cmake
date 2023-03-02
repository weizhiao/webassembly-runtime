add_definitions(-DPLATFORM_LINUX)

include_directories(${PLATFORM_DIR})
include_directories(${PLATFORM_DIR}/../common)

file (GLOB_RECURSE source_all ${PLATFORM_DIR}/*.c)

set (PLATFORM_SOURCE ${source_all})