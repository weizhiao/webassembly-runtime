#include "wasm_runtime_init_api.h"
#include "wasm_native.h"
#include "wasm_memory.h"
#include "wasm_exception.h"

#if WASM_ENABLE_WASI != 0
#include "wasm_wasi.h"

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
    if (total_size >= UINT32_MAX || (total_size > 0 && !(list = wasm_runtime_malloc((uint32)total_size))) || buf_size >= UINT32_MAX || (buf_size > 0 && !(buf = wasm_runtime_malloc((uint32)buf_size))))
    {

        if (buf)
            wasm_runtime_free(buf);
        if (list)
            wasm_runtime_free(list);
        return false;
    }

    for (i = 0; i < array_size; i++)
    {
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

bool wasm_runtime_wasi_init(WASMModule *module,
                            const char *dir_list[], uint32 dir_count,
                            const char *map_dir_list[], uint32 map_dir_count,
                            const char *env[], uint32 env_count,
                            const char *addr_pool[], uint32 addr_pool_size,
                            const char *ns_lookup_pool[], uint32 ns_lookup_pool_size,
                            char *argv[], uint32 argc, int stdinfd, int stdoutfd,
                            int stderrfd)
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
    char error_buf[128];
    uint32 error_buf_size = 128;
    char *path, resolved_path[PATH_MAX];
    uint32 i;

    if (!(wasi_ctx = module->wasi_ctx = wasm_runtime_malloc(sizeof(WASIContext))))
    {
        return false;
    }

    if (!copy_string_array((const char **)argv, argc, &argv_buf, &argv_list,
                           &argv_buf_size))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    if (!copy_string_array(env, env_count, &env_buf, &env_list,
                           &env_buf_size))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    if (!(curfds = wasm_runtime_malloc(sizeof(struct fd_table))) || !(prestats = wasm_runtime_malloc(sizeof(struct fd_prestats))) || !(argv_environ = wasm_runtime_malloc(sizeof(struct argv_environ_values))) || !(apool = wasm_runtime_malloc(sizeof(struct addr_pool))))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: allocate memory failed");
        goto fail;
    }

    if (!fd_table_init(curfds))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: "
                           "init fd table failed");
        goto fail;
    }
    fd_table_inited = true;

    if (!fd_prestats_init(prestats))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: "
                           "init fd prestats failed");
        goto fail;
    }
    fd_prestats_inited = true;

    if (!argv_environ_init(argv_environ, argv_buf, argv_buf_size, argv_list,
                           argc, env_buf, env_buf_size, env_list, env_count))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: "
                           "init argument environment failed");
        goto fail;
    }
    argv_environ_inited = true;

    if (!addr_pool_init(apool))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: "
                           "init the address pool failed");
        goto fail;
    }
    addr_pool_inited = true;

    if (!fd_table_insert_existing(curfds, 0, (stdinfd != -1) ? stdinfd : 0) || !fd_table_insert_existing(curfds, 1, (stdoutfd != -1) ? stdoutfd : 1) || !fd_table_insert_existing(curfds, 2, (stderrfd != -1) ? stderrfd : 2))
    {
        wasm_set_exception(module,
                           "Init wasi environment failed: init fd table failed");
        goto fail;
    }

    wasm_fd = 3;
    for (i = 0; i < dir_count; i++, wasm_fd++)
    {
        path = realpath(dir_list[i], resolved_path);
        if (!path)
        {
            snprintf(error_buf, error_buf_size,
                     "error while pre-opening directory %s: %d\n",
                     dir_list[i], errno);
            wasm_set_exception(module, error_buf);

            goto fail;
        }

        raw_fd = open(path, O_RDONLY | O_DIRECTORY, 0);
        if (raw_fd == -1)
        {
            snprintf(error_buf, error_buf_size,
                     "error while pre-opening directory %s: %d\n",
                     dir_list[i], errno);
            wasm_set_exception(module, error_buf);

            goto fail;
        }

        fd_table_insert_existing(curfds, wasm_fd, raw_fd);
        fd_prestats_insert(prestats, dir_list[i], wasm_fd);
    }

    for (i = 0; i < addr_pool_size; i++)
    {
        char *cp, *address, *mask;
        bool ret = false;

        cp = strdup(addr_pool[i]);
        if (!cp)
        {
            wasm_set_exception(module,
                               "Init wasi environment failed: copy address failed");
            goto fail;
        }

        address = strtok(cp, "/");
        mask = strtok(NULL, "/");

        ret = addr_pool_insert(apool, address, (uint8)(mask ? atoi(mask) : 0));
        wasm_runtime_free(cp);
        if (!ret)
        {
            wasm_set_exception(module,
                               "Init wasi environment failed: store address failed");
            goto fail;
        }
    }

    if (!copy_string_array(ns_lookup_pool, ns_lookup_pool_size, &ns_lookup_buf,
                           &ns_lookup_list, NULL))
    {
        wasm_set_exception(module,
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

#endif

bool wasm_runtime_init_env()
{
    if (platform_init() != 0)
        goto fail;

    if (wasm_native_init() == false)
    {
        goto fail;
    }

    return true;

fail:
    platform_destroy();

    return false;
}