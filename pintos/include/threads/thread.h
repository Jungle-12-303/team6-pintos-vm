#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

struct lock;
struct file;
struct child_status;

struct fd_entry {
	int fd;                            /* File descriptor number. */
	struct file *file;                 /* Open file. */
	struct list_elem elem;             /* Element in fd list. */
};

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* `elem' 멤버는 두 가지 용도로 쓰인다. 실행 대기열(thread.c)의 원소가
 * 될 수도 있고, 세마포어 대기 목록(synch.c)의 원소가 될 수도 있다.
 * 준비 상태인 스레드만 실행 대기열에 들어가고, 블록 상태인 스레드만
 * 세마포어 대기 목록에 들어가므로 두 용도가 동시에 겹치지 않는다. */
struct thread {
	/* thread.c가 관리한다. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름(디버깅용). */
	int priority;                       /* 현재 우선순위. */
	int base_priority;                  /* 기부를 제외한 원래 우선순위. */
	struct list donations;              /* 우선순위를 기부한 스레드들. */
	struct list_elem donation_elem;     /* 다른 기부 목록에 들어갈 원소. */
	struct lock *wait_on_lock;          /* 이 스레드가 기다리는 락. */
	int64_t wakeup_tick;                /* timer sleep에서 깨어날 tick. */
	int nice;                           /* MLFQS nice 값. */
	int recent_cpu;                     /* MLFQS 최근 CPU 사용량. */

	/* thread.c와 synch.c가 함께 사용한다. */
	struct list_elem elem;              /* 목록 원소. */
	struct list_elem allelem;           /* 전체 스레드 목록 원소. */

#ifdef USERPROG
	/* userprog/process.c가 관리한다. */
	uint64_t *pml4;                     /* 4단계 페이지 맵. */
	struct list children;              /* 자식 프로세스 상태 목록. */
	struct child_status *child_status;  /* 부모와 공유하는 상태. */
	struct list fd_list;                /* 열린 파일 디스크립터 목록. */
	int next_fd;                        /* 다음 디스크립터 번호. */
	struct file *running_file;          /* 쓰기가 금지된 실행 파일. */
	int exit_status;                    /* 종료 시 보고할 상태. */
	void *user_rsp;
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* thread.c가 관리한다. */
	struct intr_frame tf;               /* 문맥 전환 정보. */
	unsigned magic;                     /* 스택 오버플로 감지용. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

bool thread_priority_more (const struct list_elem *,
		const struct list_elem *, void *);
void thread_yield_if_lower_priority (void);
void thread_donate_priority (struct lock *);
void thread_remove_lock_donations (struct lock *);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
