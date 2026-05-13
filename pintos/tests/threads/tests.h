#ifndef TESTS_THREADS_TESTS_H
#define TESTS_THREADS_TESTS_H

void run_test (const char *);

typedef void test_func (void);

extern test_func test_alarm_single;
extern test_func test_alarm_multiple;
extern test_func test_alarm_simultaneous;
extern test_func test_alarm_priority;
extern test_func test_alarm_zero;
extern test_func test_alarm_negative;
extern test_func test_priority_change;
extern test_func test_priority_donate_one;
extern test_func test_priority_donate_multiple;
extern test_func test_priority_donate_multiple2;
extern test_func test_priority_donate_sema;
extern test_func test_priority_donate_nest;
extern test_func test_priority_donate_lower;
extern test_func test_priority_donate_chain;
extern test_func test_priority_fifo;
extern test_func test_priority_preempt;
extern test_func test_priority_sema;
extern test_func test_priority_condvar;
#ifdef VM
extern test_func test_vm_page_get_type;
extern test_func test_vm_anon_initializer;
extern test_func test_vm_file_initializer;
extern test_func test_vm_spt_init;
extern test_func test_vm_spt_insert;
extern test_func test_vm_spt_find;
extern test_func test_vm_spt_remove;
extern test_func test_vm_alloc_page;
extern test_func test_vm_spt_copy;
extern test_func test_vm_spt_kill;
extern test_func test_vm_claim_page;
extern test_func test_vm_fault_invalid;
extern test_func test_vm_fault_claim;
#endif
extern test_func test_mlfqs_load_1;
extern test_func test_mlfqs_load_60;
extern test_func test_mlfqs_load_avg;
extern test_func test_mlfqs_recent_1;
extern test_func test_mlfqs_fair_2;
extern test_func test_mlfqs_fair_20;
extern test_func test_mlfqs_nice_2;
extern test_func test_mlfqs_nice_10;
extern test_func test_mlfqs_block;

void msg (const char *, ...);
void fail (const char *, ...);
void pass (void);

#endif /* tests/threads/tests.h */
