#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

/* 처리된 페이지 폴트 수. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램에 의해 발생할 수 있는 인터럽트의 핸들러를 등록한다.

   실제 유닉스 계열 OS에서는 [SV-386] 3-24와 3-25에 설명된 것처럼
   이 인터럽트 대부분을 시그널 형태로 사용자 프로세스에 전달한다.
   하지만 우리는 시그널을 구현하지 않는다.
   대신 단순히 사용자 프로세스를 종료하도록 만들 것이다.

   페이지 폴트는 예외적인 경우다. 여기서는 다른 예외와 같은 방식으로 처리하지만,
   가상 메모리를 구현하려면 이 부분을 변경해야 한다.

   각각의 예외에 대한 설명은 [IA32-v3a] 5.15절 "Exception and Interrupt Reference"를 참조하라. */
void
exception_init (void) {
	/* 이 예외들은 예를 들어 INT, INT3, INTO, BOUND 명령처럼 사용자 프로그램이 명시적으로 발생시킬 수 있다.
	   따라서 DPL==3으로 설정해 사용자 프로그램이 이 명령들을 통해 예외를 호출할 수 있도록 허용한다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이 예외들은 DPL==0이므로 사용자 프로세스가 INT 명령으로 호출할
	   수 없다. 그래도 간접적으로는 발생할 수 있는데, 예를 들어 0으로
	   나누면 #DE가 발생할 수 있다. */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트를 켠 상태로 처리할 수 있다.
	   페이지 폴트의 경우 폴트 주소가 CR2에 저장되고 보존되어야 하므로
	   인터럽트를 비활성화해야 한다. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력한다. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스가 발생시켰을 가능성이 있는 예외의 핸들러. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 사용자 프로세스가 발생시켰을 가능성이 있다.
	   예를 들어 프로세스가 매핑되지 않은 가상 메모리에 접근하려고 했을 수 있다(페이지 폴트).
	   지금은 단순히 사용자 프로세스를 종료한다. 나중에는 커널에서 페이지 폴트를 처리하고 싶을 것이다.
	   실제 유닉스 계열 운영체제는 대부분의 예외를 시그널을 통해 프로세스에 다시 전달하지만, 우리는 시그널을 구현하지 않는다. */

	/* 인터럽트 프레임의 코드 세그먼트 값은 예외가 어디에서 발생했는지 알려준다. */
	switch (f->cs) {
		case SEL_UCSEG:
			thread_current ()->exit_status = -1;
			thread_exit ();

		case SEL_KCSEG:
			/* 커널의 코드 세그먼트이므로 커널 버그를 의미한다.
			   커널 코드는 예외를 던지면 안 된다. (페이지 폴트가 커널 예외를 일으킬 수는 있지만, 여기로 도착해서는 안 된다.)
			   이 점을 분명히 하기 위해 커널 패닉을 일으킨다. */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* 다른 코드 세그먼트인가? 발생해서는 안 된다. 커널 패닉을 일으킨다. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* 페이지 폴트 핸들러. 가상 메모리를 구현하려면 채워 넣어야 하는 뼈대 코드다.
   프로젝트 2의 일부 해법에서도 이 코드를 수정해야 할 수 있다.

   진입 시 폴트를 일으킨 주소는 CR2(Control Register 2)에 있고,
   폴트에 관한 정보는 exception.h의 PF_* 매크로에 설명된 형식으로 F의 error_code 멤버에 들어 있다.
   여기의 예제 코드는 그 정보를 파싱하는 방법을 보여준다.
   두 항목에 대한 자세한 정보는 [IA32-v3a] 5.15절 "Exception and Interrupt Reference"의
   "Interrupt 14--Page Fault Exception (#PF)" 설명에서 찾을 수 있다. */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* True: 존재하지 않는 페이지, False: 읽기 전용 페이지에 쓰기. */
	bool write;        /* True: 쓰기 접근, False: 읽기 접근. */
	bool user;         /* True: 사용자 접근, False: 커널 접근. */
	void *fault_addr;  /* 폴트 주소(가상 주소). */

	/* 폴트를 일으킨 주소, 즉 폴트를 유발하도록 접근된 가상 주소를 얻는다.
	   이 주소는 코드나 데이터를 가리킬 수 있다. 반드시 폴트를 일으킨 명령의 주소인 것은 아니다(그 주소는 f->rip이다). */
	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 켠다(인터럽트를 꺼 둔 이유는 CR2가 변경되기 전에 확실히 읽기 위해서). */
	intr_enable ();


	/* 원인을 판별한다. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* 프로젝트 3 이후를 위한 코드. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

	/* 페이지 폴트 수를 센다. */
	page_fault_cnt++;

	if (thread_current ()->pml4 != NULL) {
		thread_current ()->exit_status = -1;
		thread_exit ();
	}

	/* 폴트가 실제 폴트라면 정보를 출력하고 종료한다. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
}
