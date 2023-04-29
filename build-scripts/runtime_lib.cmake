#设置目录
set (RUNTIME_DIR ${RUNTIME_ROOT_DIR}/runtime)
set (PLATFORM_DIR ${RUNTIME_DIR}/platform/${RUNTIME_BUILD_PLATFORM})
set (UTILS_DIR ${RUNTIME_DIR}/utils)
set (WASMVM_DIR ${RUNTIME_DIR}/wasmvm)

#根据功能启用宏
if (RUNTIME_BUILD_BULK_MEMORY EQUAL 1)
    add_definitions (-DWASM_ENABLE_BULK_MEMORY=1)
    message ("     Bulk memory feature enabled")
else ()
    add_definitions (-DWASM_ENABLE_BULK_MEMORY=0)
    message ("     Bulk memory feature disabled")
endif ()

if (RUNTIME_BUILD_JIT EQUAL 1)
    add_definitions (-DWASM_ENABLE_JIT=1)
    if (NOT DEFINED LLVM_DIR)
      set (LLVM_SRC_ROOT "${RUNTIME_DIR}/deps/llvm")
      set (LLVM_BUILD_ROOT "${LLVM_SRC_ROOT}/build")
      if (NOT EXISTS "${LLVM_BUILD_ROOT}")
        message ("Cannot find LLVM dir: ${LLVM_BUILD_ROOT}")
      endif ()
      set (CMAKE_PREFIX_PATH "${LLVM_BUILD_ROOT};${CMAKE_PREFIX_PATH}")
    endif ()
    find_package(LLVM REQUIRED CONFIG)
    include_directories(${LLVM_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    message ("     Jit enabled")
else ()
    add_definitions (-DWASM_ENABLE_JIT=0)
    message ("     Jit disabled")
endif ()

include(${PLATFORM_DIR}/platform.cmake)
include(${UTILS_DIR}/utils.cmake)
include (${WASMVM_DIR}/wasmvm.cmake)

set (source_all
    ${PLATFORM_SOURCE}
    ${UTILS_SOURCE}
    ${WASMVM_SOURCE}
)

set (WASM_RUNTIME_LIB_SOURCE ${source_all})
