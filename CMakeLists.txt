cmake_minimum_required (VERSION 3.14)

#include(CheckPIESupported)

project (runtime)

#set (CMAKE_VERBOSE_MAKEFILE OFF)

if(NOT DEFINED RUNTIME_BUILD_PLATFORM)
  set (RUNTIME_BUILD_PLATFORM "linux")
endif()

# 重置链接参数
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

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

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif ()

set (RUNTIME_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

include (${RUNTIME_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_executable (runtime ${MAIN_SOURCE} ${WAMR_RUNTIME_LIB_SOURCE})

#check_pie_supported()
#set_target_properties (runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

#install (TARGETS runtime DESTINATION bin)

#target_link_libraries (runtime vmlib ${LLVM_AVAILABLE_LIBS} ${UV_A_LIBS} -lm -ldl -lpthread)

#add_library (libiwasm SHARED ${WAMR_RUNTIME_LIB_SOURCE})

#install (TARGETS libiwasm DESTINATION lib)

#set_target_properties (libiwasm PROPERTIES OUTPUT_NAME iwasm)

#target_link_libraries (libiwasm ${LLVM_AVAILABLE_LIBS} ${UV_A_LIBS} -lm -ldl -lpthread)
