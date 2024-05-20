#ifndef THREAD_H
#define THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */

#define MAX_NESTED_DEPTH 8 // 우선순위 기부의 최대 재귀 깊이

/*---------------------------mlfqs 매크로 함수-------------------------------*/
/* threads/fixed-point.h */
// #ifndef THREADS_FIXED_POINT_H
// #define THREADS_FIXED_POINT_H

#define NICE_MAX 20
#define NICE_DEFAULT 0
#define NICE_MIN -20
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

// Define the fixed-point type and constants
#define FIXED_POINT int
#define FP_P 17
#define FP_Q 14
#define FP_FRACTION (1 << FP_Q)

// Ensure P + Q equals 31
#if FP_P + FP_Q != 31
#error "FATAL ERROR: FP_P + FP_Q != 31."
#endif

// Fixed-point arithmetic operations
#define CONVERT_INT_TO_FP(n) ((n) * (FP_FRACTION))																					   // Convert integer to fixed-point
#define CONVERT_FP_TO_INT_ZERO(x) ((x) / (FP_FRACTION))																				   // Convert fixed-point to integer (rounding toward zero)
#define CONVERT_FP_TO_INT_NEAR(x) (((x) >= 0) ? ((x) + (FP_FRACTION) / 2) / (FP_FRACTION) : ((x) - (FP_FRACTION) / 2) / (FP_FRACTION)) // Convert fixed-point to integer (rounding to nearest)
#define ADD_FP_INT(x, n) ((x) + (n) * (FP_FRACTION))																				   // Add fixed-point and integer
#define SUB_FP_INT(x, n) ((x) - (n) * (FP_FRACTION))																				   // Subtract integer from fixed-point
#define MUL_FP(x, y) (((int64_t)(x)) * (y) / (FP_FRACTION))																			   // Multiply two fixed-point numbers
#define DIV_FP(x, y) (((int64_t)(x)) * (FP_FRACTION) / (y))																			   // Divide two fixed-point numbers

// #endif
/* threads/fixed-point.h */
/*----------------------------------------------------------------------*/

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
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid; /* Thread identifier. */					// 스레드 식별자
	enum thread_status status; /* Thread state. */		// 스레드 상태를 나타내는 열거형 변수
	char name[16]; /* Name (for debugging purposes). */ // 디버깅 목적으로 사용되는 스레드 이름을 저장하는 문자열 배열
	int priority; /* Priority. */						// 스레드의 우선순위를 나타내는 정수 변수
	int64_t local_tick;									// 스레드의 일어날 시간 변수를 저장하는 정수형 변수

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */ // 스레드 리스트에 연결될 때 사용되는 리스트 요소

	struct list_elem all_elem; /* List element for all threads list. */

	// for priority donation
	int original_priority;			// 스레드의 원래 우선순위를 저장하는 변수
	struct lock *wait_on_lock;		// 스레드가 대기 중인 락(장치)을 나타내는 포인터 변수
	struct list donations;			// 우선순위 기부를 추적하기 위한 스레드 리스트
	struct list_elem donation_elem; // 우선순위 기부 리스트에 연결될 때 사용되는 리스트 요소

	/* 4BSD */
	int nice;
	int recent_cpu;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;		  /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

void thread_sleep(int64_t wakeup_ticks);
void thread_wakeup(int64_t wakeup_ticks);

bool compare_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
// bool compare_donate_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED); // 그냥 compare_priority 써도 무방

void preemption_priority(void);
void refresh_priority(void);

void donate_priority(void);
void remove_donation(struct lock *lock);

/* 4BSD */
void mlfqs_calculate_priority(struct thread *t);
void mlfqs_calculate_recent_cpu(struct thread *t);
void mlfqs_calculate_load_avg(void);
void mlfqs_increase_recent_cpu(void);
void mlfqs_recalculate_priority(void);
void mlfqs_recalculate_recent_cpu(void);

#endif /* thread.h */