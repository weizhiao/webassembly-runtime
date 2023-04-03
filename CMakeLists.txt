cmake_minimum_required (VERSION 3.14)

set (CMAKE_C_COMPILER "/usr/bin/gcc")
#include(CheckPIESupported)

project (runtime)
#set (CMAKE_VERBOSE_MAKEFILE OFF)

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

set (RUNTIME_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (PRODUCT_DIR ${RUNTIME_ROOT_DIR}/product/${RUNTIME_BUILD_PLATFORM})

#对应平台的cmake
include (${PRODUCT_DIR}/config.cmake)

#运行时本身的cmake
include (${RUNTIME_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_executable (runtime ${MAIN_SOURCE} ${WASM_RUNTIME_LIB_SOURCE})

#check_pie_supported()
#set_target_properties (runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

#install (TARGETS runtime DESTINATION bin)

target_link_libraries (runtime -lm -lpthread)

#add_library (libiwasm SHARED ${WAMR_RUNTIME_LIB_SOURCE})

#install (TARGETS libiwasm DESTINATION lib)

#set_target_properties (libiwasm PROPERTIES OUTPUT_NAME iwasm)

#target_link_libraries (libiwasm ${LLVM_AVAILABLE_LIBS} ${UV_A_LIBS} -lm -ldl -lpthread)
