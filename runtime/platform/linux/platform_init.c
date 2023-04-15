#include "platform_api.h"

int
platform_init()
{
    return 0;
}

void
platform_destroy()
{}

int
os_printf(const char *format, ...)
{
    int ret = 0;
    va_list ap;

    va_start(ap, format);
    ret += vprintf(format, ap);
    va_end(ap);

    return ret;
}

int
os_vprintf(const char *format, va_list ap)
{
    return vprintf(format, ap);
}

unsigned char *
platform_read_file(const char *filename, unsigned int *ret_size)
{
    unsigned char *buffer;
    int file;
    uint32 file_size, buf_size, read_size;
    struct stat stat_buf;

    if (!filename || !ret_size) {
        printf("Read file to buffer failed: invalid filename or ret size.\n");
        return NULL;
    }

    if ((file = open(filename, O_RDONLY, 0)) == -1) {
        printf("Read file to buffer failed: open file %s failed.\n", filename);
        return NULL;
    }

    if (fstat(file, &stat_buf) != 0) {
        printf("Read file to buffer failed: fstat file %s failed.\n", filename);
        close(file);
        return NULL;
    }

    file_size = (uint32)stat_buf.st_size;

    /* At lease alloc 1 byte to avoid malloc failed */
    buf_size = file_size > 0 ? file_size : 1;

    if (!(buffer = BH_MALLOC(buf_size))) {
        printf("Read file to buffer failed: alloc memory failed.\n");
        close(file);
        return NULL;
    }

    read_size = (uint32)read(file, buffer, file_size);
    close(file);

    if (read_size < file_size) {
        printf("Read file to buffer failed: read file content failed.\n");
        BH_FREE(buffer);
        return NULL;
    }

    *ret_size = file_size;
    return buffer;
}