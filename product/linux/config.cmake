if (NOT DEFINED RUNTIME_BUILD_BULK_MEMORY)
  # Enable bulk memory by default
  set (RUNTIME_BUILD_BULK_MEMORY 1)
endif ()

if (NOT DEFINED RUNTIME_BUILD_JIT)
  set (RUNTIME_BUILD_JIT 1)
endif ()

if(NOT DEFINED RUNTIME_BUILD_THREAD)
  set (RUNTIME_BUILD_THREAD 0)
endif()

file (GLOB_RECURSE source_all ${PRODUCT_DIR}/main.c)
set (MAIN_SOURCE ${source_all})