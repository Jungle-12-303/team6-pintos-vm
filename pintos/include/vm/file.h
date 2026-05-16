#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

/**
 * @brief 
 * 
 * @author iamnuked
 * @date 2026-05-16
 */
struct file_page {
	struct file *file;
	off_t offset;
	char* file_name; /* file_name으로 할 지, program_name으로 해야 할 지 고민 중 */
	void *aux;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset);
void do_munmap (void *va);
#endif
