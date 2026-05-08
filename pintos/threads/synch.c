/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool cond_priority_more (const struct list_elem *,
		const struct list_elem *, void *);

/* 세마포어 SEMA를 VALUE로 초기화한다. 세마포어는 음수가 아닌 정수와
   이를 조작하는 두 원자 연산으로 이루어진다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 1 감소시킨다.

   - up 또는 "V": 값을 1 증가시키고, 기다리는 스레드가 있으면 하나를
   깨운다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어의 down 또는 "P" 연산.
   SEMA 값이 양수가 될 때까지 기다린 뒤 원자적으로 1 감소시킨다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 된다.
   인터럽트가 꺼진 상태에서 호출할 수는 있지만, 잠들 경우 다음에
   스케줄되는 스레드가 보통 인터럽트를 다시 켠다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered (&sema->waiters, &thread_current ()->elem,
				thread_priority_more, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어의 up 또는 "V" 연산.
   SEMA 값을 1 증가시키고, SEMA를 기다리는 스레드가 있으면 하나를 깨운다.

   이 함수는 인터럽트 핸들러에서도 호출할 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *unblocked = NULL;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		list_sort (&sema->waiters, thread_priority_more, NULL);
		unblocked = list_entry (list_pop_front (&sema->waiters),
				struct thread, elem);
		thread_unblock (unblocked);
	}
	sema->value++;
	intr_set_level (old_level);
	if (unblocked != NULL)
		thread_yield_if_lower_priority ();
}

static void sema_test_helper (void *sema_);

/* 두 스레드 사이에서 제어가 "핑퐁"처럼 오가게 하는 세마포어 자체 테스트.
   흐름을 보고 싶으면 printf() 호출을 넣어 확인할 수 있다. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득한다. 필요하면 사용 가능해질 때까지 잠든다.
   현재 스레드가 이미 이 락을 들고 있으면 안 된다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 된다.
   인터럽트가 꺼진 상태에서 호출할 수는 있지만, 잠들어야 하면 인터럽트는
   다시 켜진다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	thread_donate_priority (lock);
	sema_down (&lock->semaphore);
	thread_current ()->wait_on_lock = NULL;
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 보유한 LOCK을 해제한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 락을
   해제하려고 하는 것은 의미가 없다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	thread_remove_lock_donations (lock);
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 들고 있으면 true, 아니면 false를 반환한다.
   다른 스레드가 락을 들고 있는지 검사하는 것은 경쟁 조건이 될 수 있다. */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 목록 안에 들어가는 세마포어 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 목록 원소. */
	struct semaphore semaphore;         /* 이 세마포어. */
	int priority;                       /* 가장 높은 대기자 우선순위. */
};

static bool
cond_priority_more (const struct list_elem *a,
		const struct list_elem *b,
		void *aux UNUSED) {
	const struct semaphore_elem *sa =
		list_entry (a, struct semaphore_elem, elem);
	const struct semaphore_elem *sb =
		list_entry (b, struct semaphore_elem, elem);

	return sa->priority > sb->priority;
}

/* 조건 변수 COND를 초기화한다.
   조건 변수는 한 코드가 조건을 알리고, 협력하는 코드가 그 신호를 받아
   동작할 수 있게 한다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	waiter.priority = thread_get_priority ();
	list_insert_ordered (&cond->waiters, &waiter.elem,
			cond_priority_more, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* LOCK으로 보호되는 COND를 기다리는 스레드가 있으면 그중 하나에 신호를
   보내 대기에서 깨운다. 이 함수를 호출하기 전에 LOCK을 들고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서
   조건 변수에 신호를 보내려 하는 것은 의미가 없다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
	{
		list_sort (&cond->waiters, cond_priority_more, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* LOCK으로 보호되는 COND를 기다리는 모든 스레드를 깨운다.
   이 함수를 호출하기 전에 LOCK을 들고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서
   조건 변수에 신호를 보내려 하는 것은 의미가 없다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
