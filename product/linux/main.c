#include <stdio.h>
#include "platform.h"

int main(){
    platform_init();
    unsigned ret_size;
    platform_read_file("test.wasm",&ret_size);
    printf("%d",ret_size);
    return 0;
}