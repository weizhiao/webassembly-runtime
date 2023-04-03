include_directories(${UTILS_DIR}/include)

file (GLOB_RECURSE source_all ${UTILS_DIR}/src/*.c)

set (UTILS_SOURCE ${source_all})