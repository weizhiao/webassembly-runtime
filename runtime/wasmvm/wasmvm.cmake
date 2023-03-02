set (INTERP_DIR ${WASMVM_DIR}/interpreter)
set (COMMON_DIR ${WASMVM_DIR}/common)

include_directories(${COMMON_DIR}/include)

file (GLOB_RECURSE COMMON_SOURCE
    ${COMMON_DIR}/src/*.c
)

if (RUNTIME_BUILD_INTERP EQUAL 1)
    include (${INTERP_DIR}/interp.cmake)
endif ()

set (WASMVM_SOURCE 
    ${INTERP_SOURCE}
    ${COMMON_SOURCE}
)