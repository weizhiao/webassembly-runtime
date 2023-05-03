#include <stdio.h>
#include "wasm_runtime_api.h"
#include "wasm_exception.h"
#include "wasm_memory.h"

static int app_argc;
static char **app_argv;

static int
print_help()
{
    printf("Usage: iwasm [-options] wasm_file [args...]\n");
    printf("options:\n");
    printf("  -f|--function name       Specify a function name of the module to run rather\n"
           "                           than main\n");
    printf("  -v=n                     Set log verbose level (0 to 5, default is 2) larger\n"
           "                           level with more log\n");
    printf("  --stack-size=n           Set maximum stack size in bytes, default is 64 KB\n");
    printf("  --heap-size=n            Set maximum heap size in bytes, default is 16 KB\n");
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
app_instance_repl(WASMModule *module_inst)
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
    uint32 value_stack_size = 1024;
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
        goto read_file_fail;
    }

    WASMModule *module = wasm_module_create();
    if (!module)
        goto create_module_fail;

    if (!wasm_loader(module, file_buf, ret_size))
        goto fail;

    if (!wasm_validator(module))
        goto fail;

    if (!wasm_instantiate(module, value_stack_size, exectution_stack_size))
        goto fail;

    if (!wasm_runtime_wasi_init(
            module,
            dir_list, dir_list_size,
            NULL, 0,
            env_list, env_list_size,
            addr_pool, addr_pool_size,
            ns_lookup_pool, ns_lookup_pool_size, argv,
            argc, -1, -1, -1))
    {
        goto fail;
    }

    if (!execute_main(module, argc, argv))
    {
        goto fail;
    }

    wasm_module_destory(module);
    wasm_runtime_free(file_buf);
    return 0;

fail:
    os_printf("%s", wasm_get_exception(module));
    wasm_module_destory(module);
create_module_fail:
    wasm_runtime_free(file_buf);
read_file_fail:
    return -1;
}