#include <stdio.h>
#include "wasm_runtime_api.h"

int main(int argc, char *argv[]){
    wasm_runtime_init_env();
    const char *dir_list[8] = { NULL };
    uint32 dir_list_size = 0;
    const char *env_list[8] = { NULL };
    uint32 env_list_size = 0;
    const char *addr_pool[8] = { NULL };
    uint32 addr_pool_size = 0;
    const char *ns_lookup_pool[8] = { NULL };
    uint32 ns_lookup_pool_size = 0;
    unsigned ret_size;
    uint8 * file_buf = platform_read_file(argv[1],&ret_size);
    char error_buf[128] = { 0 };
    WASMModule *module = wasm_loader(file_buf, ret_size, error_buf, 128);
    wasm_validator(module, error_buf, 128);
    wasm_instantiate(module, 4080, error_buf, 128);

    if (!wasm_runtime_init_wasi(
                module,
                dir_list, dir_list_size,
                NULL, 0,
                env_list, env_list_size,
                addr_pool, addr_pool_size,
                ns_lookup_pool,ns_lookup_pool_size, argv,
                argc, error_buf, 128)) {
            goto fail;
    }
    
    printf("%d\n",execute_main(module, argc, argv));
    printf("%s\n",module->cur_exception);

fail:
    return 0;
}