#include <stdio.h>
#include "wasm_runtime_api.h"

static int app_argc;
static char **app_argv;

static int
print_help()
{
    printf("Usage: iwasm [-options] wasm_file [args...]\n");
    printf("options:\n");
    printf("  -f|--function name       Specify a function name of the module to run rather\n"
           "                           than main\n");
#if WASM_ENABLE_LOG != 0
    printf("  -v=n                     Set log verbose level (0 to 5, default is 2) larger\n"
           "                           level with more log\n");
#endif
    printf("  --stack-size=n           Set maximum stack size in bytes, default is 64 KB\n");
    printf("  --heap-size=n            Set maximum heap size in bytes, default is 16 KB\n");
#if WASM_ENABLE_FAST_JIT != 0
    printf("  --jit-codecache-size=n   Set fast jit maximum code cache size in bytes,\n");
    printf("                           default is %u KB\n", FAST_JIT_DEFAULT_CODE_CACHE_SIZE / 1024);
#endif
    printf("  --repl                   Start a very simple REPL (read-eval-print-loop) mode\n"
           "                           that runs commands in the form of \"FUNC ARG...\"\n");
#if WASM_ENABLE_LIBC_WASI != 0
    printf("  --env=<env>              Pass wasi environment variables with \"key=value\"\n");
    printf("                           to the program, for example:\n");
    printf("                             --env=\"key1=value1\" --env=\"key2=value2\"\n");
    printf("  --dir=<dir>              Grant wasi access to the given host directories\n");
    printf("                           to the program, for example:\n");
    printf("                             --dir=<dir1> --dir=<dir2>\n");
    printf("  --addr-pool=<addrs>      Grant wasi access to the given network addresses in\n");
    printf("                           CIRD notation to the program, seperated with ',',\n");
    printf("                           for example:\n");
    printf("                             --addr-pool=1.2.3.4/15,2.3.4.5/16\n");
    printf("  --allow-resolve=<domain> Allow the lookup of the specific domain name or domain\n");
    printf("                           name suffixes using a wildcard, for example:\n");
    printf("                           --allow-resolve=example.com # allow the lookup of the specific domain\n");
    printf("                           --allow-resolve=*.example.com # allow the lookup of all subdomains\n");
    printf("                           --allow-resolve=* # allow any lookup\n");
#endif
#if BH_HAS_DLFCN
    printf("  --native-lib=<lib>       Register native libraries to the WASM module, which\n");
    printf("                           are shared object (.so) files, for example:\n");
    printf("                             --native-lib=test1.so --native-lib=test2.so\n");
#endif
#if WASM_ENABLE_MULTI_MODULE != 0
    printf("  --module-path=<path>     Indicate a module search path. default is current\n"
           "                           directory('./')\n");
#endif
#if WASM_ENABLE_LIB_PTHREAD != 0
    printf("  --max-threads=n          Set maximum thread number per cluster, default is 4\n");
#endif
#if WASM_ENABLE_DEBUG_INTERP != 0
    printf("  -g=ip:port               Set the debug sever address, default is debug disabled\n");
    printf("                             if port is 0, then a random port will be used\n");
#endif
    printf("  --version                Show version information\n");
    return 1;
}

static char **
split_string(char *str, int *count)
{
    char **res = NULL, **res1;
    char *p;
    int idx = 0;

    /* split string and append tokens to 'res' */
    do
    {
        p = strtok(str, " ");
        str = NULL;
        res1 = res;
        res = (char **)realloc(res1, sizeof(char *) * (uint32)(idx + 1));
        if (res == NULL)
        {
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
    while (p)
    {
        *p = ' ';
        p = strchr(p, '\\');
    }

    if (count)
    {
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
            n = getline(&cmd, &len, stdin)) != -1)
    {
        if (cmd[n - 1] == '\n')
        {
            if (n == 1)
                continue;
            else
                cmd[n - 1] = '\0';
        }
        if (!strcmp(cmd, "__exit__"))
        {
            printf("exit repl mode\n");
            break;
        }
        app_argv = split_string(cmd, &app_argc);
        if (app_argv == NULL)
        {
            LOG_ERROR("Wasm prepare param failed: split string failed.\n");
            break;
        }
        if (app_argc != 0)
        {
            execute_func(module_inst, app_argv[0],
                         app_argc - 1, app_argv + 1);
        }
        free(app_argv);
    }
    free(cmd);
    return NULL;
}

int main(int argc, char *argv[])
{
    wasm_runtime_init_env();
    const char *dir_list[8] = {NULL};
    uint32 dir_list_size = 0;
    const char *env_list[8] = {NULL};
    uint32 env_list_size = 0;
    const char *addr_pool[8] = {NULL};
    uint32 addr_pool_size = 0;
    const char *ns_lookup_pool[8] = {NULL};
    uint32 ns_lookup_pool_size = 0;
    unsigned ret_size;
    bool is_repl_mode = false;
    int log_verbose_level = 2;
    char *wasm_file = NULL;
    uint32 value_stack_size = 64 * 1024;
    uint32 exectution_stack_size = 1024;
    for (argc--, argv++; argc > 0 && argv[0][0] == '-'; argc--, argv++)
    {
        if (!strncmp(argv[0], "-v=", 3))
        {
            log_verbose_level = atoi(argv[0] + 3);
            if (log_verbose_level < 0 || log_verbose_level > 5)
                return print_help();
        }
        else if (!strcmp(argv[0], "--repl"))
        {
            is_repl_mode = true;
        }
        else if (!strncmp(argv[0], "--stack-size=", 13))
        {
            if (argv[0][13] == '\0')
                return print_help();
            value_stack_size = atoi(argv[0] + 13);
        }
#if WASM_ENABLE_FAST_JIT != 0
        else if (!strncmp(argv[0], "--jit-codecache-size=", 21))
        {
            if (argv[0][21] == '\0')
                return print_help();
            jit_code_cache_size = atoi(argv[0] + 21);
        }
#endif
#if WASM_ENABLE_LIBC_WASI != 0
        else if (!strncmp(argv[0], "--dir=", 6))
        {
            if (argv[0][6] == '\0')
                return print_help();
            if (dir_list_size >= sizeof(dir_list) / sizeof(char *))
            {
                printf("Only allow max dir number %d\n",
                       (int)(sizeof(dir_list) / sizeof(char *)));
                return 1;
            }
            dir_list[dir_list_size++] = argv[0] + 6;
        }
        else if (!strncmp(argv[0], "--env=", 6))
        {
            char *tmp_env;

            if (argv[0][6] == '\0')
                return print_help();
            if (env_list_size >= sizeof(env_list) / sizeof(char *))
            {
                printf("Only allow max env number %d\n",
                       (int)(sizeof(env_list) / sizeof(char *)));
                return 1;
            }
            tmp_env = argv[0] + 6;

            printf("Wasm parse env string failed: expect \"key=value\", "
                   "got \"%s\"\n",
                   tmp_env);
            return print_help();
        }
        /* TODO: parse the configuration file via --addr-pool-file */
        else if (!strncmp(argv[0], "--addr-pool=", strlen("--addr-pool=")))
        {
            /* like: --addr-pool=100.200.244.255/30 */
            char *token = NULL;

            if ('\0' == argv[0][12])
                return print_help();

            token = strtok(argv[0] + strlen("--addr-pool="), ",");
            while (token)
            {
                if (addr_pool_size >= sizeof(addr_pool) / sizeof(char *))
                {
                    printf("Only allow max address number %d\n",
                           (int)(sizeof(addr_pool) / sizeof(char *)));
                    return 1;
                }

                addr_pool[addr_pool_size++] = token;
                token = strtok(NULL, ";");
            }
        }
        else if (!strncmp(argv[0], "--allow-resolve=", 16))
        {
            if (argv[0][16] == '\0')
                return print_help();
            if (ns_lookup_pool_size >= sizeof(ns_lookup_pool) / sizeof(ns_lookup_pool[0]))
            {
                printf(
                    "Only allow max ns lookup number %d\n",
                    (int)(sizeof(ns_lookup_pool) / sizeof(ns_lookup_pool[0])));
                return 1;
            }
            ns_lookup_pool[ns_lookup_pool_size++] = argv[0] + 16;
        }
#endif /* WASM_ENABLE_LIBC_WASI */
        else
            return print_help();
    }

    wasm_file = argv[0];
    app_argc = argc;
    app_argv = argv;

    log_set_verbose_level(log_verbose_level);
    uint8 *file_buf = platform_read_file(wasm_file, &ret_size);
    if (!file_buf)
    {
        goto fail;
    }

    char error_buf[128] = {0};
    WASMModule *module = wasm_loader(file_buf, ret_size, error_buf, 128);
    if (!module)
    {
        goto fail;
    }
    if (!wasm_validator(module, error_buf, 128))
    {
        goto fail;
    }

    if (!wasm_instantiate(module, value_stack_size, exectution_stack_size, error_buf, 128))
    {
        goto fail;
    }

    if (!wasm_runtime_wasi_init(
            module,
            dir_list, dir_list_size,
            NULL, 0,
            env_list, env_list_size,
            addr_pool, addr_pool_size,
            ns_lookup_pool, ns_lookup_pool_size, argv,
            argc, -1, -1, -1, error_buf, 128))
    {
        goto fail;
    }

    if (is_repl_mode)
    {
        app_instance_repl(module);
    }
    else
    {
        execute_main(module, argc, argv);
    }

fail:
    os_printf("%s", error_buf);
    return 0;
}