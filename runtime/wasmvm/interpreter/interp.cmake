add_definitions (-DWASM_ENABLE_INTERP=1)

include_directories(${INTERP_DIR}/include)

file (GLOB_RECURSE source_all
    ${INTERP_DIR}/src/wasm_interp_classic.c
)

set (INTERP_SOURCE ${source_all})

