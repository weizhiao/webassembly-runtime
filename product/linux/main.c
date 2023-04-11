#include <stdio.h>
#include "wasm_runtime_api.h"

static int app_argc;
static char **app_argv;

static char **
split_string(char *str, int *count)
{
    char **res = NULL, **res1;
    char *p;
    int idx = 0;

    /* split string and append tokens to 'res' */
    do {
        p = strtok(str, " ");
        str = NULL;
        res1 = res;
        res = (char **)realloc(res1, sizeof(char *) * (uint32)(idx + 1));
        if (res == NULL) {
            free(res1);
            return NULL;
        }
        res[idx++] = p;
    } while (p);

    /**
     * Due to the function name,
     * res[0] might contain a '\' to indicate a space
     * func\name -> func name
     */
    p = strchr(res[0], '\\');
    while (p) {
        *p = ' ';
        p = strchr(p, '\\');
    }

    if (count) {
        *count = idx - 1;
    }
    return res;
}

static void *
app_instance_repl(WASMModuleInstance *module_inst)
{
    char *cmd = NULL;
    size_t len = 0;
    ssize_t n;

    while ((printf("webassembly> "), fflush(stdout),
            n = getline(&cmd, &len, stdin))
           != -1) {
        if (cmd[n - 1] == '\n') {
            if (n == 1)
                continue;
            else
                cmd[n - 1] = '\0';
        }
        if (!strcmp(cmd, "__exit__")) {
            printf("exit repl mode\n");
            break;
        }
        app_argv = split_string(cmd, &app_argc);
        if (app_argv == NULL) {
            LOG_ERROR("Wasm prepare param failed: split string failed.\n");
            break;
        }
        if (app_argc != 0) {
            execute_func(module_inst, app_argv[0],
                                          app_argc - 1, app_argv + 1);
        }
        free(app_argv);
    }
    free(cmd);
    return NULL;
}

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
    // wasm_instantiate(module, 1024, 1024, error_buf, 128);

    // if (!wasm_runtime_wasi_init(
    //             module,
    //             dir_list, dir_list_size,
    //             NULL, 0,
    //             env_list, env_list_size,
    //             addr_pool, addr_pool_size,
    //             ns_lookup_pool,ns_lookup_pool_size, argv,
    //             argc, -1, -1, -1, error_buf, 128)) {
    //         goto fail;
    // }
    
    // execute_main(module,argc,argv);

fail:
    return 0;
}