#根据功能启用宏
if (RUNTIME_BUILD_BULK_MEMORY EQUAL 1)
    add_definitions (-DWASM_ENABLE_BULK_MEMORY=1)
    message ("     Bulk memory feature enabled")
else ()
    add_definitions (-DWASM_ENABLE_BULK_MEMORY=0)
    message ("     Bulk memory feature disabled")
endif ()

#设置目录
set (RUNTIME_DIR ${RUNTIME_ROOT_DIR}/runtime)
set (PLATFORM_DIR ${RUNTIME_DIR}/platform/${RUNTIME_BUILD_PLATFORM})
set (UTILS_DIR ${RUNTIME_DIR}/utils)
set (WASMVM_DIR ${RUNTIME_DIR}/wasmvm)

include(${PLATFORM_DIR}/platform.cmake)
include(${UTILS_DIR}/utils.cmake)
include (${WASMVM_DIR}/wasmvm.cmake)

set (source_all
    ${PLATFORM_SOURCE}
    ${UTILS_SOURCE}
    ${WASMVM_SOURCE}
)

set (WASM_RUNTIME_LIB_SOURCE ${source_all})
