add_definitions(-DWASM_ENABLE_LIBC_WASI=1)

file (GLOB_RECURSE source_all ${WASI_DIR}/*.c )

include_directories(${WASI_DIR}/libc-wasi/sandboxed-system-primitives/include
                    ${WASI_DIR}/libc-wasi/sandboxed-system-primitives/src
                    ${WASI_DIR}/include)

set (WASI_SOURCE ${source_all})
