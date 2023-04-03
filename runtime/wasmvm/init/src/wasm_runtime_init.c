#include "wasm_runtime_init_api.h"
#include "wasm_native.h"
#include "wasm_wasi.h"
#include "wasm_memory.h"
#include "wasm_executor.h"
#include "wasm_exec_env.h"
#include "wasm_exception.h"

WASMFunction *
wasm_lookup_function(const WASMModule *module_inst, const char *name)
{
    uint32 i;
    for (i = 0; i < module_inst->export_func_count; i++)
        if (!strcmp(module_inst->export_functions[i].name, name))
            return module_inst->export_functions[i].function;
    return NULL;
}

static bool
execute_post_instantiate_functions(WASMModule *module)
{
    WASMFunction *start_func = NULL;
    WASMFunction *initialize_func = NULL;
    WASMExecEnv *exec_env = NULL;
    bool ret = false;

    if(module->start_function !=(uint32)-1) {
        start_func = module->functions + module->start_function;
    }

#if WASM_ENABLE_LIBC_WASI != 0
    /*
     * WASI reactor instances may assume that _initialize will be called by
     * the environment at most once, and that none of their other exports
     * are accessed before that call.
     */
    initialize_func =
            wasm_lookup_function(module, "_initialize");
#endif

    if (!start_func && !initialize_func) {
        /* No post instantiation functions to call */
        return true;
    }

    if (!exec_env
        && !(exec_env =
                 wasm_exec_env_create(module,
                                      module->default_wasm_stack_size))) {
        wasm_set_exception(module, "allocate memory failed");
        return false;
    }

    if (initialize_func
        && !wasm_call_function(exec_env, initialize_func, 0, NULL)) {
        goto fail;
    }

    /* Execute start function for both main insance and sub instance */
    if (start_func && !wasm_call_function(exec_env, start_func, 0, NULL)) {
        goto fail;
    }

    ret = true;

fail:
    //wasm_exec_env_destroy(exec_env);
    return ret;
}

static bool
copy_string_array(const char *array[], uint32 array_size, char **buf_ptr,
                  char ***list_ptr, uint64 *out_buf_size)
{
    uint64 buf_size = 0, total_size;
    uint32 buf_offset = 0, i;
    char *buf = NULL, **list = NULL;

    for (i = 0; i < array_size; i++)
        buf_size += strlen(array[i]) + 1;

    /* We add +1 to generate null-terminated array of strings */
    total_size = sizeof(char *) * ((uint64)array_size + 1);
    if (total_size >= UINT32_MAX
        || (total_size > 0 && !(list = wasm_runtime_malloc((uint32)total_size)))
        || buf_size >= UINT32_MAX
        || (buf_size > 0 && !(buf = wasm_runtime_malloc((uint32)buf_size)))) {

        if (buf)
            wasm_runtime_free(buf);
        if (list)
            wasm_runtime_free(list);
        return false;
    }

    for (i = 0; i < array_size; i++) {
        list[i] = buf + buf_offset;
        bh_strcpy_s(buf + buf_offset, (uint32)buf_size - buf_offset, array[i]);
        buf_offset += (uint32)(strlen(array[i]) + 1);
    }
    list[array_size] = NULL;

    *list_ptr = list;
    *buf_ptr = buf;
    if (out_buf_size)
        *out_buf_size = buf_size;

    return true;
}

static bool 
wasi_context_init(WASMModule *module_inst,
                       const char *dir_list[], uint32 dir_count,
                       const char *map_dir_list[], uint32 map_dir_count,
                       const char *env[], uint32 env_count,
                       const char *addr_pool[], uint32 addr_pool_size,
                       const char *ns_lookup_pool[], uint32 ns_lookup_pool_size,
                       char *argv[], uint32 argc, int stdinfd, int stdoutfd,
                       int stderrfd, char *error_buf, uint32 error_buf_size)
{
    WASIContext *wasi_ctx;
    char *argv_buf = NULL;
    char **argv_list = NULL;
    char *env_buf = NULL;
    char **env_list = NULL;
    char *ns_lookup_buf = NULL;
    char **ns_lookup_list = NULL;
    uint64 argv_buf_size = 0, env_buf_size = 0;
    struct fd_table *curfds = NULL;
    struct fd_prestats *prestats = NULL;
    struct argv_environ_values *argv_environ = NULL;
    struct addr_pool *apool = NULL;
    bool fd_table_inited = false, fd_prestats_inited = false;
    bool argv_environ_inited = false;
    bool addr_pool_inited = false;
    __wasi_fd_t wasm_fd = 3;
    int32 raw_fd;
    char *path, resolved_path[PATH_MAX];
    uint32 i;

    if (!(wasi_ctx = module_inst->wasi_ctx = wasm_runtime_malloc(sizeof(WASIContext)))) {
        return false;
    }

    /* process argv[0], trip the path and suffix, only keep the program name
     */
    if (!copy_string_array((const char **)argv, argc, &argv_buf, &argv_list,
                           &argv_buf_size)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    if (!copy_string_array(env, env_count, &env_buf, &env_list,
                           &env_buf_size)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    if (!(curfds = wasm_runtime_malloc(sizeof(struct fd_table)))
        || !(prestats = wasm_runtime_malloc(sizeof(struct fd_prestats)))
        || !(argv_environ =
                 wasm_runtime_malloc(sizeof(struct argv_environ_values)))
        || !(apool = wasm_runtime_malloc(sizeof(struct addr_pool)))) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    if (!fd_table_init(curfds)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: "
                      "init fd table failed");
        goto fail;
    }
    fd_table_inited = true;

    if (!fd_prestats_init(prestats)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: "
                      "init fd prestats failed");
        goto fail;
    }
    fd_prestats_inited = true;

    if (!argv_environ_init(argv_environ, argv_buf, argv_buf_size, argv_list,
                           argc, env_buf, env_buf_size, env_list, env_count)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: "
                      "init argument environment failed");
        goto fail;
    }
    argv_environ_inited = true;

    if (!addr_pool_init(apool)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: "
                      "init the address pool failed");
        goto fail;
    }
    addr_pool_inited = true;

    /* Prepopulate curfds with stdin, stdout, and stderr file descriptors.
     *
     * If -1 is given, use STDIN_FILENO (0), STDOUT_FILENO (1),
     * STDERR_FILENO (2) respectively.
     */
    if (!fd_table_insert_existing(curfds, 0, (stdinfd != -1) ? stdinfd : 0)
        || !fd_table_insert_existing(curfds, 1, (stdoutfd != -1) ? stdoutfd : 1)
        || !fd_table_insert_existing(curfds, 2,
                                     (stderrfd != -1) ? stderrfd : 2)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: init fd table failed");
        goto fail;
    }

    wasm_fd = 3;
    for (i = 0; i < dir_count; i++, wasm_fd++) {
        path = realpath(dir_list[i], resolved_path);
        if (!path) {
            if (error_buf)
                snprintf(error_buf, error_buf_size,
                         "error while pre-opening directory %s: %d\n",
                         dir_list[i], errno);
            goto fail;
        }

        raw_fd = open(path, O_RDONLY | O_DIRECTORY, 0);
        if (raw_fd == -1) {
            if (error_buf)
                snprintf(error_buf, error_buf_size,
                         "error while pre-opening directory %s: %d\n",
                         dir_list[i], errno);
            goto fail;
        }

        fd_table_insert_existing(curfds, wasm_fd, raw_fd);
        fd_prestats_insert(prestats, dir_list[i], wasm_fd);
    }

    /* addr_pool(textual) -> apool */
    for (i = 0; i < addr_pool_size; i++) {
        char *cp, *address, *mask;
        bool ret = false;

        cp = strdup(addr_pool[i]);
        if (!cp) {
            set_error_buf(error_buf, error_buf_size,
                          "Init wasi environment failed: copy address failed");
            goto fail;
        }

        address = strtok(cp, "/");
        mask = strtok(NULL, "/");

        ret = addr_pool_insert(apool, address, (uint8)(mask ? atoi(mask) : 0));
        wasm_runtime_free(cp);
        if (!ret) {
            set_error_buf(error_buf, error_buf_size,
                          "Init wasi environment failed: store address failed");
            goto fail;
        }
    }

    if (!copy_string_array(ns_lookup_pool, ns_lookup_pool_size, &ns_lookup_buf,
                           &ns_lookup_list, NULL)) {
        set_error_buf(error_buf, error_buf_size,
                      "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    wasi_ctx->curfds = curfds;
    wasi_ctx->prestats = prestats;
    wasi_ctx->argv_environ = argv_environ;
    wasi_ctx->addr_pool = apool;
    wasi_ctx->argv_buf = argv_buf;
    wasi_ctx->argv_list = argv_list;
    wasi_ctx->env_buf = env_buf;
    wasi_ctx->env_list = env_list;
    wasi_ctx->ns_lookup_buf = ns_lookup_buf;
    wasi_ctx->ns_lookup_list = ns_lookup_list;

    return true;

fail:
    if (argv_environ_inited)
        argv_environ_destroy(argv_environ);
    if (fd_prestats_inited)
        fd_prestats_destroy(prestats);
    if (fd_table_inited)
        fd_table_destroy(curfds);
    if (addr_pool_inited)
        addr_pool_destroy(apool);
    if (curfds)
        wasm_runtime_free(curfds);
    if (prestats)
        wasm_runtime_free(prestats);
    if (argv_environ)
        wasm_runtime_free(argv_environ);
    if (apool)
        wasm_runtime_free(apool);
    if (argv_buf)
        wasm_runtime_free(argv_buf);
    if (argv_list)
        wasm_runtime_free(argv_list);
    if (env_buf)
        wasm_runtime_free(env_buf);
    if (env_list)
        wasm_runtime_free(env_list);
    if (ns_lookup_buf)
        wasm_runtime_free(ns_lookup_buf);
    if (ns_lookup_list)
        wasm_runtime_free(ns_lookup_list);
    return false;
}

bool
wasm_runtime_init_wasi(WASMModule *module_inst,
                       const char *dir_list[], uint32 dir_count,
                       const char *map_dir_list[], uint32 map_dir_count,
                       const char *env[], uint32 env_count,
                       const char *addr_pool[], uint32 addr_pool_size,
                       const char *ns_lookup_pool[], uint32 ns_lookup_pool_size,
                       char *argv[], uint32 argc, 
                       char *error_buf, uint32 error_buf_size)
{
    if(!wasi_context_init(module_inst,
                       dir_list, dir_count,
                       map_dir_list, map_dir_count,
                       env, env_count,
                       addr_pool, addr_pool_size,
                       ns_lookup_pool, ns_lookup_pool_size,
                       argv, argc, -1, -1,
                       -1, error_buf, error_buf_size)){
                            goto fail;
                       }
    if(!execute_post_instantiate_functions(module_inst)){
        goto fail;
    }

    return true;

fail:
    return false;
}

static bool
wasm_native_init()
{
    NativeSymbol *native_symbols;
    uint32 n_native_symbols;

    n_native_symbols = get_libc_wasi_export_apis(&native_symbols);
    if (!wasm_native_register_natives("wasi_unstable", native_symbols,
                                      n_native_symbols))
        goto fail;
    if (!wasm_native_register_natives("wasi_snapshot_preview1", native_symbols,
                                      n_native_symbols))
        goto fail;

    return true;
fail:
    wasm_native_destroy();
    return false;
}

bool
wasm_runtime_init_env()
{
    if (platform_init() != 0)
        goto fail;

    if (wasm_native_init() == false) {
        goto fail;
    }
    
    return true;

fail:
    platform_destroy();

    return false;
}