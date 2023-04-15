#ifndef _INSTANTIATE_H
#define _INSTANTIATE_H

#include "instantiate_common.h"

//实例化全局变量
bool
globals_instantiate(WASMModule *module);

//实例化内存
bool
memories_instantiate(WASMModule *module);

//实例化表
bool
tables_instantiate(WASMModule *module);

//实例化导出
bool 
export_instantiate(WASMModule *module);

//实例化函数
bool
functions_instantiate(WASMModule *module);

#endif