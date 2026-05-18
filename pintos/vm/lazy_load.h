#ifdef VM_LAZY_LOAD_H
#define VM_LAZY_LOAD_H

#include <stddef.h>
#include "filesys/off_t.h"

struct file;

struct lazy_load_info {
    struct file *file;
    off_t ofs;
    size_t page_read_bytes;
    size_t page_zero_bytes;
};

#endif