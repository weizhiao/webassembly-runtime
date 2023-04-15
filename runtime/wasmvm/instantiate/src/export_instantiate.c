#include "instantiate.h"

static uint32
get_export_count(const WASMModule *module, uint8 kind)
{
    WASMExport *export = module->exports;
    uint32 count = 0, i;

    for (i = 0; i < module->export_count; i++, export ++)
        if (export->kind == kind)
            count++;

    return count;
}

bool 
export_instantiate(WASMModule *module)
{
    WASMExportFuncInstance *export_func;
    WASMExport *export = module->exports;
    uint32 i, export_func_count;
    uint64 total_size;
    
    export_func_count = module->export_func_count = get_export_count(module, EXPORT_KIND_FUNC);
    total_size = sizeof(WASMExportFuncInstance) * (uint64)export_func_count;

    if (!( export_func = module->export_functions = wasm_runtime_malloc(total_size))) {
        goto fail;
    }

    for (i = 0; i < module->export_count; i++, export++){
        switch(export->kind){
            case EXPORT_KIND_FUNC:
                export_func->name = export->name;
                export_func->function = module->functions + export->index;
                export_func++;
            break;
            case EXPORT_KIND_GLOBAL:
            case EXPORT_KIND_MEMORY:
            case EXPORT_KIND_TABLE:
            break;
        }
    }

    LOG_VERBOSE("Instantiate export success.\n");
    return  true;
fail:
    LOG_VERBOSE("Instantiate export fail.\n");
    wasm_set_exception(module, "Instantiate export fail.\n");
    return false;
}