#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
struct gp_registers {
	uint64_t r15; /* 일반 목적 레지스터, 함수 호출 후에도 보존해야 함 */
	uint64_t r14; /* 일반 목적 레지스터, 함수 호출 후에도 보존해야 함 */
	uint64_t r13; /* 일반 목적 레지스터, 함수 호출 후에도 보존해야 함 */
	uint64_t r12; /* 일반 목적 레지스터, 함수 호출 후에도 보존해야 함 */
	uint64_t r11; /* syscall 때 플래그 저장에 사용됨 */
	uint64_t r10; /* 시스템 콜의 4번째 인자 */
	uint64_t r9;  /* 함수/시스템 콜의 6번째 인자 */
	uint64_t r8;  /* 함수/시스템 콜의 5번째 인자 */
	uint64_t rsi; /* 함수/시스템 콜의 2번째 인자 */
	uint64_t rdi; /* 함수/시스템 콜의 1번째 인자 */
	uint64_t rbp; /* 스택 프레임 기준 포인터 */
	uint64_t rdx; /* 일반 목적 레지스터, 함수/시스템 콜의 3번째 인자 */
	uint64_t rcx; /* 일반 목적 레지스터, syscall 복귀 주소와도 연관 있음 */
	uint64_t rbx; /* 일반 목적 레지스터,함수 호출 후에도 보존해야 함 */
	uint64_t rax; /* 반환값, 시스템 콜 번호 저장 */
} __attribute__((packed));

struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R;
	/* 추가 데이터 세그먼트(extra segment)를 가리키는 레지스터.
      인터럽트 처리 중 기존 실행 흐름의 세그먼트 상태를 저장해 두기 위해 보관.*/
	uint16_t es;

	uint16_t __pad1; /* 패딩 */
	uint32_t __pad2;

	/* 기본 데이터 세그먼트를 가리키는 레지스터.
      커널이 인터럽트 처리 후 원래 실행 상태로 돌아갈 수 있도록 저장 */
	uint16_t ds;

	uint16_t __pad3; /* 패딩 */
	uint32_t __pad4;

	/* Pushed by intrNN_stub in intr-stubs.S. */
	/* intr-stubs.S 안의 intrNN_stub에 의해 스택에 push됨. */
	uint64_t vec_no; /* Interrupt vector number. */

	/* Sometimes pushed by the CPU,
      otherwise for consistency pushed as 0 by intrNN_stub.
      The CPU puts it just under `eip', but we move it here. */
	/* CPU가 가끔 이 값을 스택에 넣고,
      그렇지 않은 경우에는 일관성을 위해 intrNN_stub이 0을 넣는다.
      CPU는 이 값을 `eip` 바로 아래에 두지만, 우리는 여기로 옮긴다. */
	uint64_t error_code;

	/* Pushed by the CPU.
      These are the interrupted task's saved registers. */
	/* CPU에 의해 푸시됨.
      이는 인터럽트된 작업의 저장된 레지스터들. */
	uintptr_t rip; /* 인터럽트 발생 당시 실행 중이던 명령어 주소. 인터럽트 끝나면 이 주소로 돌아감 */
	uint16_t cs; /* 현재 실행 코드가 어떤 코드 세그먼트에서 실행 중이었는지 저장 */
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags; /* 인터럽트 발생 당시의 플래그 레지스터 값 조건 플래그, 인터럽트 활성화 여부 같은 CPU 상태가 들어 있음 */
	uintptr_t rsp; /* 인터럽트 발생 당시의 스택 포인터. 원래 실행 흐름으로 돌아갈 때 스택 위치를 복구하는 데 사용 */
	uint16_t ss; /* rsp가 가리키는 스택이 어떤 스택 세그먼트에 속하는지 표시 */
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
