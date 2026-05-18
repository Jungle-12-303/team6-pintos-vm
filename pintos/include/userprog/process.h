#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct lazy_load_info {
	struct file *file; 		 /* 읽어올 실행파일 */
	off_t ofs; 				 /* page 데이터가 시작되는 파일 offset */
	size_t page_read_bytes;  /* pgae에 파일에서 읽어 넣을 byte 수  */
	size_t page_zero_bytes;  /* page에서 0으로 채울 byte 수 */
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
