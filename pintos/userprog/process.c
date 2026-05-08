#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static struct child_status *child_status_create (void);
static void child_status_release (struct child_status *);
static struct child_status *find_child_status (tid_t);
static void get_program_name (const char *, char *, size_t);
static bool setup_arguments (char *, struct intr_frame *);

extern struct lock filesys_lock;

struct child_status {
	tid_t tid;
	int exit_status;
	bool loaded;
	bool load_success;
	bool waited;
	int ref_cnt;
	struct semaphore load_sema;
	struct semaphore wait_sema;
	struct list_elem elem;
};

struct exec_info {
	char *file_name;
	struct child_status *child;
};

struct fork_info {
	struct thread *parent;
	struct intr_frame parent_if;
	struct child_status *child;
};

/* initd와 다른 프로세스에서 공통으로 사용하는 프로세스 초기화 함수. */
static void
process_init (void) {
}

static struct child_status *
child_status_create (void) {
	struct child_status *child = malloc (sizeof *child);

	if (child == NULL)
		return NULL;
	child->tid = TID_ERROR;
	child->exit_status = -1;
	child->loaded = false;
	child->load_success = false;
	child->waited = false;
	child->ref_cnt = 2;
	sema_init (&child->load_sema, 0);
	sema_init (&child->wait_sema, 0);
	return child;
}

static void
child_status_release (struct child_status *child) {
	child->ref_cnt--;
	if (child->ref_cnt == 0)
		free (child);
}

static struct child_status *
find_child_status (tid_t tid) {
	struct list_elem *e;

	for (e = list_begin (&thread_current ()->children);
			e != list_end (&thread_current ()->children); e = list_next (e)) {
		struct child_status *child = list_entry (e, struct child_status, elem);

		if (child->tid == tid)
			return child;
	}
	return NULL;
}

static void
get_program_name (const char *file_name, char *name, size_t size) {
	size_t i;

	for (i = 0; i + 1 < size && file_name[i] != '\0' && file_name[i] != ' '; i++)
		name[i] = file_name[i];
	name[i] = '\0';
}

/* FILE_NAME에서 첫 사용자 프로그램인 "initd"를 적재해 시작한다.
 * 새 스레드는 process_create_initd()가 반환하기 전에 스케줄될 수 있고,
 * 심지어 먼저 종료될 수도 있다. initd의 스레드 id를 반환하며,
 * 스레드를 만들 수 없으면 TID_ERROR를 반환한다.
 * 이 함수는 한 번만 호출해야 한다. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	char thread_name[16];
	struct child_status *child;
	struct exec_info *info;
	tid_t tid;

	/* FILE_NAME을 복사한다.
	 * 복사하지 않으면 호출자와 load() 사이에 경쟁 조건이 생긴다. */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	child = child_status_create ();
	info = malloc (sizeof *info);
	if (child == NULL || info == NULL) {
		palloc_free_page (fn_copy);
		if (child != NULL) {
			child_status_release (child);
			child_status_release (child);
		}
		free (info);
		return TID_ERROR;
	}
	info->file_name = fn_copy;
	info->child = child;
	list_push_back (&thread_current ()->children, &child->elem);
	get_program_name (file_name, thread_name, sizeof thread_name);

	/* FILE_NAME을 실행할 새 스레드를 만든다. */
	tid = thread_create (thread_name, PRI_DEFAULT, initd, info);
	if (tid == TID_ERROR) {
		list_remove (&child->elem);
		palloc_free_page (fn_copy);
		free (info);
		child_status_release (child);
		child_status_release (child);
		return TID_ERROR;
	}
	child->tid = tid;
	sema_down (&child->load_sema);
	if (!child->load_success) {
		list_remove (&child->elem);
		child_status_release (child);
		return TID_ERROR;
	}
	return tid;
}

/* 첫 사용자 프로세스를 시작하는 스레드 함수. */
static void
initd (void *aux) {
	struct exec_info *info = aux;
	struct thread *curr = thread_current ();
	struct child_status *child = info->child;
	char *file_name = info->file_name;

	curr->child_status = child;
	free (info);
#ifdef VM
	supplemental_page_table_init (&curr->spt);
#endif

	process_init ();

	if (process_exec (file_name) < 0) {
		curr->exit_status = -1;
		child->loaded = true;
		child->load_success = false;
		sema_up (&child->load_sema);
		thread_exit ();
	}
	NOT_REACHED ();
}

/* 현재 프로세스를 `name'이라는 이름으로 복제한다.
 * 새 프로세스의 스레드 id를 반환하고, 만들 수 없으면 TID_ERROR를 반환한다. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	struct child_status *child;
	struct fork_info *info;
	tid_t tid;

	child = child_status_create ();
	info = malloc (sizeof *info);
	if (child == NULL || info == NULL) {
		if (child != NULL) {
			child_status_release (child);
			child_status_release (child);
		}
		free (info);
		return TID_ERROR;
	}
	info->parent = thread_current ();
	memcpy (&info->parent_if, if_, sizeof *if_);
	info->child = child;
	list_push_back (&thread_current ()->children, &child->elem);

	tid = thread_create (name, thread_get_priority (), __do_fork, info);
	if (tid == TID_ERROR) {
		list_remove (&child->elem);
		free (info);
		child_status_release (child);
		child_status_release (child);
		return TID_ERROR;
	}
	child->tid = tid;
	sema_down (&child->load_sema);
	if (!child->load_success) {
		list_remove (&child->elem);
		child_status_release (child);
		return TID_ERROR;
	}
	return tid;
}

#ifndef VM
/* 이 함수를 pml4_for_each에 넘겨 부모의 주소 공간을 복제한다.
 * Project 2 전용 구현이다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	if (!is_user_vaddr (va))
		return true;

	/* 2. 부모의 4단계 페이지 맵에서 VA를 해석한다. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL)
		return true;

	newpage = palloc_get_page (PAL_USER);
	if (newpage == NULL)
		return false;

	memcpy (newpage, parent_page, PGSIZE);
	writable = is_writable (pte);

	/* 5. 자식 페이지 테이블의 VA 주소에 새 페이지를 WRITABLE 권한으로
	 *    추가한다. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		palloc_free_page (newpage);
		return false;
	}
	return true;
}
#endif

/* 부모의 실행 컨텍스트를 복사하는 스레드 함수.
 * 힌트: parent->tf에는 프로세스의 사용자 컨텍스트가 들어 있지 않다.
 *       따라서 process_fork의 두 번째 인자를 이 함수로 넘겨야 한다. */
static void
__do_fork (void *aux) {
	struct fork_info *info = aux;
	struct intr_frame if_;
	struct thread *parent = info->parent;
	struct thread *current = thread_current ();
	bool succ = true;

	current->child_status = info->child;

	/* 1. CPU 컨텍스트를 지역 스택으로 읽어 온다. */
	memcpy (&if_, &info->parent_if, sizeof if_);
	if_.R.rax = 0;
	free (info);

	/* 2. 페이지 테이블을 복제한다. */
	current->pml4 = pml4_create ();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	current->next_fd = parent->next_fd;
	lock_acquire (&filesys_lock);
	for (struct list_elem *e = list_begin (&parent->fd_list);
			e != list_end (&parent->fd_list); e = list_next (e)) {
		struct fd_entry *parent_fd = list_entry (e, struct fd_entry, elem);
		struct fd_entry *child_fd = malloc (sizeof *child_fd);

		if (child_fd == NULL) {
			lock_release (&filesys_lock);
			goto error;
		}
		child_fd->fd = parent_fd->fd;
		child_fd->file = file_duplicate (parent_fd->file);
		if (child_fd->file == NULL) {
			free (child_fd);
			lock_release (&filesys_lock);
			goto error;
		}
		list_push_back (&current->fd_list, &child_fd->elem);
	}
	if (parent->running_file != NULL) {
		current->running_file = file_duplicate (parent->running_file);
		if (current->running_file == NULL) {
			lock_release (&filesys_lock);
			goto error;
		}
	}
	lock_release (&filesys_lock);

	process_init ();
	current->child_status->loaded = true;
	current->child_status->load_success = true;
	sema_up (&current->child_status->load_sema);

	/* 마지막으로 새로 만든 프로세스로 전환한다. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_status = -1;
	current->child_status->loaded = true;
	current->child_status->load_success = false;
	sema_up (&current->child_status->load_sema);
	thread_exit ();
}

/* 현재 실행 컨텍스트를 f_name으로 전환한다.
 * 실패하면 -1을 반환한다. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	struct thread *curr = thread_current ();

	/* thread 구조체의 intr_frame은 사용할 수 없다.
	 * 현재 스레드가 다시 스케줄될 때 실행 정보를 그 멤버에 저장하기
	 * 때문이다. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	if (curr->running_file != NULL) {
		lock_acquire (&filesys_lock);
		file_close (curr->running_file);
		lock_release (&filesys_lock);
		curr->running_file = NULL;
	}

	/* 먼저 현재 컨텍스트를 정리한다. */
	process_cleanup ();

	/* 그 다음 바이너리를 적재한다. */
	success = load (file_name, &_if);

	/* 적재에 실패했으면 종료한다. */
	palloc_free_page (file_name);
	if (curr->child_status != NULL && !curr->child_status->loaded) {
		curr->child_status->loaded = true;
		curr->child_status->load_success = success;
		sema_up (&curr->child_status->load_sema);
	}
	if (!success)
		return -1;

	/* 전환된 프로세스를 시작한다. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* 스레드 TID가 종료될 때까지 기다린 뒤 exit status를 반환한다.
 * 커널에 의해 종료된 경우, 예를 들어 예외 때문에 죽은 경우에는 -1을
 * 반환한다. TID가 유효하지 않거나 호출 프로세스의 자식이 아니거나,
 * 해당 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우에는
 * 기다리지 않고 즉시 -1을 반환한다. */
int
process_wait (tid_t child_tid) {
	struct child_status *child = find_child_status (child_tid);
	int status;

	if (child == NULL || child->waited)
		return -1;
	child->waited = true;
	sema_down (&child->wait_sema);
	status = child->exit_status;
	list_remove (&child->elem);
	child_status_release (child);
	return status;
}

/* 프로세스를 종료한다. thread_exit ()에서 호출된다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();

	if (curr->pml4 != NULL)
		printf ("%s: exit(%d)\n", curr->name, curr->exit_status);

	lock_acquire (&filesys_lock);
	while (!list_empty (&curr->fd_list)) {
		struct fd_entry *fd = list_entry (list_pop_front (&curr->fd_list),
				struct fd_entry, elem);

		file_close (fd->file);
		free (fd);
	}
	if (curr->running_file != NULL) {
		file_close (curr->running_file);
		curr->running_file = NULL;
	}
	lock_release (&filesys_lock);

	while (!list_empty (&curr->children)) {
		struct child_status *child = list_entry (list_pop_front (&curr->children),
				struct child_status, elem);

		child_status_release (child);
	}
	if (curr->child_status != NULL) {
		curr->child_status->exit_status = curr->exit_status;
		sema_up (&curr->child_status->wait_sema);
		child_status_release (curr->child_status);
		curr->child_status = NULL;
	}

	process_cleanup ();
}

/* 현재 프로세스의 자원을 해제한다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉터리를 제거하고 커널 전용 페이지
	 * 디렉터리로 되돌아간다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서는 순서가 매우 중요하다. 페이지 디렉터리를 전환하기 전에
		 * curr->pml4를 NULL로 설정해야 타이머 인터럽트가 프로세스 페이지
		 * 디렉터리로 다시 전환하지 못한다. 또한 프로세스 페이지 디렉터리를
		 * 제거하기 전에 기본 페이지 디렉터리를 활성화해야 한다. 그렇지
		 * 않으면 이미 해제되고 지워진 페이지 디렉터리가 활성 상태로 남을
		 * 수 있다. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 다음 스레드에서 사용자 코드를 실행하도록 CPU를 설정한다.
 * 이 함수는 매 컨텍스트 스위치마다 호출된다. */
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화한다. */
	pml4_activate (next->pml4);

	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정한다. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	char *cmdline = (char *) file_name;
	char program_name[16];
	off_t file_ofs;
	bool success = false;
	bool fs_locked = false;
	int i;

	get_program_name (file_name, program_name, sizeof program_name);
	if (program_name[0] == '\0')
		goto done;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	lock_acquire (&filesys_lock);
	fs_locked = true;
	file = filesys_open (program_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", program_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", program_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	if (!setup_arguments (cmdline, if_))
		goto done;

	file_deny_write (file);
	t->running_file = file;
	file = NULL;

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	if (file != NULL)
		file_close (file);
	if (fs_locked)
		lock_release (&filesys_lock);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

static bool
setup_arguments (char *cmdline, struct intr_frame *if_) {
	char *argv[128];
	void *arg_addr[128];
	char *token;
	char *save_ptr;
	int argc = 0;
	int i;

	for (token = strtok_r (cmdline, " ", &save_ptr); token != NULL;
			token = strtok_r (NULL, " ", &save_ptr)) {
		if (argc >= 128)
			return false;
		argv[argc++] = token;
	}
	if (argc == 0)
		return false;

	for (i = argc - 1; i >= 0; i--) {
		size_t len = strlen (argv[i]) + 1;

		if_->rsp -= len;
		if (if_->rsp < USER_STACK - PGSIZE)
			return false;
		memcpy ((void *) if_->rsp, argv[i], len);
		arg_addr[i] = (void *) if_->rsp;
	}

	while (if_->rsp % 8 != 0) {
		if_->rsp--;
		if (if_->rsp < USER_STACK - PGSIZE)
			return false;
		*(uint8_t *) if_->rsp = 0;
	}

	if_->rsp -= sizeof (char *);
	if (if_->rsp < USER_STACK - PGSIZE)
		return false;
	*(char **) if_->rsp = NULL;
	for (i = argc - 1; i >= 0; i--) {
		if_->rsp -= sizeof (char *);
		if (if_->rsp < USER_STACK - PGSIZE)
			return false;
		*(void **) if_->rsp = arg_addr[i];
	}
	if_->R.rsi = if_->rsp;
	if_->rsp -= sizeof (void *);
	if (if_->rsp < USER_STACK - PGSIZE)
		return false;
	*(void **) if_->rsp = NULL;
	if_->R.rdi = argc;
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
