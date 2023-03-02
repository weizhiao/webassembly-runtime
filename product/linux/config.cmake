if (NOT DEFINED RUNTIME_BUILD_INTERP)
  # Enable Interpreter by default
  set (RUNTIME_BUILD_INTERP 1)
endif ()

file (GLOB_RECURSE source_all ${PRODUCT_DIR}/main.c)
set (MAIN_SOURCE ${source_all})