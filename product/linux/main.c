#include <stdio.h>
#include "platform.h"
#include "loader_export.h"

int main(){
    platform_init();
    unsigned ret_size;
    char * file_buf = platform_read_file("test.wasm",&ret_size);
    char error_buf[128] = { 0 };
    wasm_loader(file_buf, ret_size, error_buf, 128);
    printf("%s",error_buf);
    return 0;
}