cmake_minimum_required (VERSION 3.14)

set (CMAKE_C_COMPILER "/usr/bin/gcc")

project (runtime)

# if (NOT CMAKE_BUILD_TYPE)
#   set(CMAKE_BUILD_TYPE Release)
# endif ()

if(NOT DEFINED RUNTIME_BUILD_PLATFORM)
  set (RUNTIME_BUILD_PLATFORM "linux")
endif()

set (CMAKE_C_STANDARD 99)

if (NOT DEFINED WAMR_BUILD_TARGET)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set (WAMR_BUILD_TARGET "AARCH64")
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    set (WAMR_BUILD_TARGET "RISCV64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    # Build as X86_64 by default in 64-bit platform
    set (WAMR_BUILD_TARGET "X86_64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Build as X86_32 by default in 32-bit platform
    set (WAMR_BUILD_TARGET "X86_32")
  else ()
    message(SEND_ERROR "Unsupported build target platform!")
  endif ()
endif ()

add_definitions("-Wall -g")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -Wformat-security -Wshadow")
# set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wsign-conversion")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wformat -Wformat-security -Wno-unused")

set (RUNTIME_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (PRODUCT_DIR ${RUNTIME_ROOT_DIR}/product/${RUNTIME_BUILD_PLATFORM})

#对应平台的cmake
include (${PRODUCT_DIR}/config.cmake)

#运行时本身的cmake
include (${RUNTIME_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_executable (runtime ${MAIN_SOURCE} ${WASM_RUNTIME_LIB_SOURCE})

target_link_libraries (runtime -lm -lpthread ${LLVM_AVAILABLE_LIBS})
