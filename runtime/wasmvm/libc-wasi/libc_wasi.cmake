set (LIBC_WASI_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${LIBC_WASI_DIR}/sandboxed-system-primitives/include
                    ${LIBC_WASI_DIR}/sandboxed-system-primitives/src)

file (GLOB_RECURSE source_all ${LIBC_WASI_DIR}/*.c )

set (LIBC_WASI_SOURCE ${source_all})
