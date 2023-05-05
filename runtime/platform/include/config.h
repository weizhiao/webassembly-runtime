#ifndef _CONFIG_H
#define _CONFIG_H

#define EXCEPTION_BUF_LEN 128
#define APP_THREAD_STACK_SIZE_DEFAULT (16 * 1024)

#ifndef WASM_ORC_JIT_COMPILE_THREAD_NUM
/* The number of compilation threads created by LLVM JIT */
#define WASM_ORC_JIT_COMPILE_THREAD_NUM 4
#endif

#ifndef WASM_ENABLE_THREAD
#define WASM_ENABLE_THREAD 1
#endif

#ifndef LLVM_VERSION_MAJOR
#define LLVM_VERSION_MAJOR 16
#endif

#ifndef WASM_ORC_JIT_BACKEND_THREAD_NUM
/* The number of backend threads created by runtime */
#define WASM_ORC_JIT_BACKEND_THREAD_NUM 4
#endif

#ifndef WASM_VALIDATE_THREAD_NUM
#define WASM_VALIDATE_THREAD_NUM 4
#endif

#define DEFAULT_VALUE_STACK_SIZE (16 * 1024)

#ifndef WASM_ENABLE_LIBC_WASI
#define WASM_ENABLE_LIBC_WASI 1
#endif

#ifndef WASM_ENABLE_JIT
#define WASM_ENABLE_JIT 1
#endif

#ifndef WASM_ENABLE_DISPATCH
#define WASM_ENABLE_DISPATCH 1
#endif

#endif