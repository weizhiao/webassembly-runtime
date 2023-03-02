set (RUNTIME_DIR ${RUNTIME_ROOT_DIR}/runtime)
set (PLATFORM_DIR ${RUNTIME_DIR}/platform/${RUNTIME_BUILD_PLATFORM})
set (UTILS_DIR ${RUNTIME_DIR}/utils)
set (WASMVM_DIR ${RUNTIME_DIR}/wasmvm)
set (PRODUCT_DIR ${RUNTIME_ROOT_DIR}/product/${RUNTIME_BUILD_PLATFORM})

include(${PLATFORM_DIR}/platform.cmake)
include(${UTILS_DIR}/utils.cmake)
#在config中设置启用的功能
include (${PRODUCT_DIR}/config.cmake)
include (${WASMVM_DIR}/wasmvm.cmake)

set (source_all
    ${PLATFORM_SOURCE}
    ${UTILS_SOURCE}
    ${WASMVM_SOURCE}
)

set (WAMR_RUNTIME_LIB_SOURCE ${source_all})
