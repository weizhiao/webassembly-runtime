add_definitions (-DWASM_ENABLE_INTERP=1)

include_directories(${INTERP_DIR}/loader/include)
include_directories($)

file (GLOB_RECURSE source_all
    ${INTERP_DIR}/*.c
)

set (INTERP_SOURCE ${source_all})
