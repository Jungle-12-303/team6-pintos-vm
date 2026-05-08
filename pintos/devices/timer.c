#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 흐른 타이머 tick 수. */
static int64_t ticks;

/* 깨어날 tick 순서로 정렬한 sleeping 스레드 목록. */
static struct list sleep_list;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool wakeup_tick_less (const struct list_elem *,
		const struct list_elem *, void *);
static void timer_wakeup (int64_t);
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 Programmable Interval Timer(PIT)가 초당 PIT_FREQ번 인터럽트를
   일으키도록 설정하고, 해당 인터럽트를 등록한다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값을 반올림한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	list_init (&sleep_list);
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연에 사용하는 loops_per_tick을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* 한 타이머 tick보다 아직 작은 2의 거듭제곱 중 가장 큰 값으로
	   loops_per_tick을 근사한다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 세밀하게 보정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후 지난 타이머 tick 수를 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN 이후 지난 타이머 tick 수를 반환한다.
   THEN은 이전에 timer_ticks()가 반환한 값이어야 한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 대략 TICKS 타이머 tick 동안 실행을 멈춘다. */
void
timer_sleep (int64_t ticks) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks <= 0)
		return;

	old_level = intr_disable ();
	curr->wakeup_tick = timer_ticks () + ticks;
	list_insert_ordered (&sleep_list, &curr->elem, wakeup_tick_less, NULL);
	thread_block ();
	intr_set_level (old_level);
}

/* 대략 MS 밀리초 동안 실행을 멈춘다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 대략 US 마이크로초 동안 실행을 멈춘다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 대략 NS 나노초 동안 실행을 멈춘다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	timer_wakeup (ticks);
	thread_tick ();
}

static bool
wakeup_tick_less (const struct list_elem *a,
		const struct list_elem *b,
		void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);

	if (ta->wakeup_tick != tb->wakeup_tick)
		return ta->wakeup_tick < tb->wakeup_tick;
	return ta->priority > tb->priority;
}

static void
timer_wakeup (int64_t now) {
	while (!list_empty (&sleep_list)) {
		struct thread *t = list_entry (list_front (&sleep_list),
				struct thread, elem);

		if (t->wakeup_tick > now)
			break;
		list_pop_front (&sleep_list);
		thread_unblock (t);
	}
	thread_yield_if_lower_priority ();
}

/* LOOPS번 반복하는 데 타이머 tick 하나보다 오래 걸리면 true,
   아니면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 tick 하나를 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번 반복한다. */
	start = ticks;
	busy_wait (loops);

	/* tick 수가 바뀌었다면 너무 오래 반복한 것이다. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
