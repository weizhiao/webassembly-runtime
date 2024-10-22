if (NOT DEFINED RUNTIME_BUILD_JIT)
  set (RUNTIME_BUILD_JIT 0)
endif ()

if(NOT DEFINED RUNTIME_BUILD_THREAD)
  set (RUNTIME_BUILD_THREAD 0)
endif()

if(NOT DEFINED RUNTIME_BUILD_FPU)
  set (RUNTIME_BUILD_FPU 0)
endif()

if(NOT DEFINED RUNTIME_BUILD_DISPATCH)
  set (RUNTIME_BUILD_DISPATCH 1)
endif()

if(NOT DEFINED RUNTIME_BUILD_BUILTIN)
  set (RUNTIME_BUILD_BUILTIN 1)
endif()

set(RUNTIME_BUILD_WASI 1)

file (GLOB_RECURSE source_all ${PRODUCT_DIR}/main.c)
set (MAIN_SOURCE ${source_all})