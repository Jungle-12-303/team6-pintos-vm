#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "lib/kernel/stdio.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void syscall_exit (int status);
static void validate_user_addr (const void *);
static void validate_user_buffer (const void *, unsigned);
static void validate_user_string (const char *);
static char *copy_user_string (const char *);
static struct fd_entry *find_fd (int);
static int add_file (struct file *);
static void close_fd (int);

struct lock filesys_lock;

/* 시스템 콜.
 *
 * 예전에는 시스템 콜 서비스를 인터럽트 핸들러가 처리했다
 * (예: Linux의 int 0x80). 하지만 x86-64에서는 제조사가 시스템 콜 요청을
 * 위한 효율적인 경로인 `syscall` 명령을 제공한다.
 *
 * syscall 명령은 Model Specific Register(MSR)의 값을 읽어 동작한다.
 * 자세한 내용은 매뉴얼을 참고한다. */

#define MSR_STAR 0xc0000081         /* 세그먼트 셀렉터 MSR. */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL 진입 주소. */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags 마스크. */

void
syscall_init (void) {
	lock_init (&filesys_lock);
	write_msr (MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr (MSR_LSTAR, (uint64_t) syscall_entry);

	/* syscall_entry가 사용자 스택을 커널 모드 스택으로 바꾸기 전까지
	 * 인터럽트 서비스 루틴이 인터럽트를 처리하면 안 된다.
	 * 따라서 FLAG_IF를 마스크한다. */
	write_msr (MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 메인 시스템 콜 인터페이스. */
void
syscall_handler (struct intr_frame *f) {
	thread_current()->user_rsp = (void *) f->rsp;
	uint64_t number = f->R.rax;

	switch (number) {
		case SYS_HALT:
			power_off ();
			break;
		case SYS_EXIT:
			syscall_exit ((int) f->R.rdi);
			break;
		case SYS_FORK:
			validate_user_string ((const char *) f->R.rdi);
			f->R.rax = process_fork ((const char *) f->R.rdi, f);
			break;
		case SYS_EXEC:
		{
			char *file_name = copy_user_string ((const char *) f->R.rdi);

			if (file_name == NULL)
				syscall_exit (-1);
			if (process_exec (file_name) < 0)
				syscall_exit (-1);
			NOT_REACHED ();
			break;
		}
		case SYS_WAIT:
			f->R.rax = process_wait ((tid_t) f->R.rdi);
			break;
		case SYS_CREATE:
			validate_user_string ((const char *) f->R.rdi);
			lock_acquire (&filesys_lock);
			f->R.rax = filesys_create ((const char *) f->R.rdi,
					(off_t) f->R.rsi);
			lock_release (&filesys_lock);
			break;
		case SYS_REMOVE:
			validate_user_string ((const char *) f->R.rdi);
			lock_acquire (&filesys_lock);
			f->R.rax = filesys_remove ((const char *) f->R.rdi);
			lock_release (&filesys_lock);
			break;
		case SYS_OPEN:
		{
			struct file *file;

			validate_user_string ((const char *) f->R.rdi);
			lock_acquire (&filesys_lock);
			file = filesys_open ((const char *) f->R.rdi);
			lock_release (&filesys_lock);
			f->R.rax = file == NULL ? -1 : add_file (file);
			break;
		}
		case SYS_FILESIZE:
		{
			struct fd_entry *fd = find_fd ((int) f->R.rdi);

			if (fd == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = file_length (fd->file);
			lock_release (&filesys_lock);
			break;
		}
		case SYS_READ:
		{
			int fd = (int) f->R.rdi;
			void *buffer = (void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;

			validate_user_buffer (buffer, size);
			if (size == 0) {
				f->R.rax = 0;
			} else if (fd == 0) {
				unsigned i;

				for (i = 0; i < size; i++)
					((uint8_t *) buffer)[i] = input_getc ();
				f->R.rax = size;
			} else {
				struct fd_entry *fde = find_fd (fd);

				if (fde == NULL || fd == 1) {
					f->R.rax = -1;
					break;
				}
				lock_acquire (&filesys_lock);
				f->R.rax = file_read (fde->file, buffer, size);
				lock_release (&filesys_lock);
			}
			break;
		}
		case SYS_WRITE:
		{
			int fd = (int) f->R.rdi;
			const void *buffer = (const void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;

			validate_user_buffer (buffer, size);
			if (size == 0) {
				f->R.rax = 0;
			} else if (fd == 1) {
				putbuf (buffer, size);
				f->R.rax = size;
			} else {
				struct fd_entry *fde = find_fd (fd);

				if (fde == NULL || fd == 0) {
					f->R.rax = -1;
					break;
				}
				lock_acquire (&filesys_lock);
				f->R.rax = file_write (fde->file, buffer, size);
				lock_release (&filesys_lock);
			}
			break;
		}
		case SYS_SEEK:
		{
			struct fd_entry *fd = find_fd ((int) f->R.rdi);

			if (fd != NULL) {
				lock_acquire (&filesys_lock);
				file_seek (fd->file, (off_t) f->R.rsi);
				lock_release (&filesys_lock);
			}
			break;
		}
		case SYS_TELL:
		{
			struct fd_entry *fd = find_fd ((int) f->R.rdi);

			if (fd == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = file_tell (fd->file);
			lock_release (&filesys_lock);
			break;
		}
		case SYS_CLOSE:
			close_fd ((int) f->R.rdi);
			break;
		default:
			syscall_exit (-1);
	}
}

static void
syscall_exit (int status) {
	thread_current ()->exit_status = status;
	thread_exit ();
}

static void
validate_user_addr (const void *uaddr) {
	if (uaddr == NULL || !is_user_vaddr (uaddr)
			|| pml4_get_page (thread_current ()->pml4, uaddr) == NULL)
		syscall_exit (-1);
}

static void
validate_user_buffer (const void *buffer, unsigned size) {
	uintptr_t start = (uintptr_t) buffer;
	uintptr_t end = start + size;
	uintptr_t page;

	if (size == 0)
		return;
	if (buffer == NULL || end < start)
		syscall_exit (-1);
	for (page = start; page < end; page = (page & ~PGMASK) + PGSIZE)
		validate_user_addr ((const void *) page);
	validate_user_addr ((const void *) (end - 1));
}

static void
validate_user_string (const char *str) {
	validate_user_addr (str);
	while (*str != '\0') {
		str++;
		validate_user_addr (str);
	}
}

static char *
copy_user_string (const char *str) {
	char *copy;
	size_t i;

	validate_user_string (str);
	copy = palloc_get_page (0);
	if (copy == NULL)
		return NULL;
	for (i = 0; i < PGSIZE; i++) {
		copy[i] = str[i];
		if (copy[i] == '\0')
			return copy;
	}
	palloc_free_page (copy);
	return NULL;
}

static struct fd_entry *
find_fd (int fd) {
	struct list_elem *e;

	for (e = list_begin (&thread_current ()->fd_list);
			e != list_end (&thread_current ()->fd_list); e = list_next (e)) {
		struct fd_entry *fde = list_entry (e, struct fd_entry, elem);

		if (fde->fd == fd)
			return fde;
	}
	return NULL;
}

static int
add_file (struct file *file) {
	struct fd_entry *fd = malloc (sizeof *fd);

	if (fd == NULL) {
		lock_acquire (&filesys_lock);
		file_close (file);
		lock_release (&filesys_lock);
		return -1;
	}
	fd->fd = thread_current ()->next_fd++;
	fd->file = file;
	list_push_back (&thread_current ()->fd_list, &fd->elem);
	return fd->fd;
}

static void
close_fd (int fd_num) {
	struct fd_entry *fd = find_fd (fd_num);

	if (fd == NULL)
		return;
	list_remove (&fd->elem);
	lock_acquire (&filesys_lock);
	file_close (fd->file);
	lock_release (&filesys_lock);
	free (fd);
}
