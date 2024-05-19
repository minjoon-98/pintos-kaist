#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "devices/timer.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

// 스레드 sleep 상태를 보관하기 위한 list
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* 4BSD */
static int load_avg = LOAD_AVG_DEFAULT;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
// T가 유효한 스레드를 가리키는 것으로 보이면 true를 반환한다.
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
/********************************************************************
 * <현재 실행 중인 스레드를 식별하는 방법을 설명>
 * 실행 중인 스레드를 반환한다.
 * cpu의 스택 포인터 rsp를 읽고, 그 값을 페이지의 시작 부분으로 반올림한다.
 * struct thread는 항상 페이지의 시작 부분에 위치하고
 * 스택 포인터는 중간 어딘가에 있기 때문에, 이것은 현재 스레드를 찾는다. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
/*******************************************************************
 * 스레드 시작을 위한 전역 디스크립터 테이블이다.
 * gdt는 thread_init 이후에 설정될 것이기 때문에, 우리는 먼저 임시 gdt를 설정해야한다. */
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
/****************************************************************
   [스레드 시스템을 초기화하고, 스레드 생성전에 필요한 단계 설명]

   현재 실행 중인 코드를 스레드로 변환함으로써 스레딩 시스템을 초기화한다.
   이것은 일반적으로 작동할 수 없으며,
   loader.S가 스택의 바닥을 페이지 경계에 맞추어 놓았기 때문에
   가능한 일이다.

   또한 실행 큐와 tid잠금을 초기화한다.

   이 함수를 호출한 후에는 thread_create()로
	어떤 스레드도 생성하기 전에 페이지 할당자를 초기화해야 한다.

   이 함수가 완료될 때까지 thread_current()를
   호출하는 것은 안전하지 않다. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	/****************************************************************************
	 [초기 커널 스레드를 설정할때 사용되는 임시 글로벌 디스크립터 테이블(gdt)
	 을 다시 로드하고,
	 나중에 사용자 컨텍스트를 포함한 gdt로 업데이트하는 과정을 설명]

	 * 커널을 위한 임시 gdt를 다시 로드한다.
	 * 이 gdt는 사용자 컨텍스트를 포함하지 않는다.
	 * 커널은 gdt_init()에서 사용자 컨텍스트와 함께 gdt를 재구축할 것이다.*/
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	// 전역 스레드 컨텍스트를 초기화한다
	lock_init(&tid_lock);
	list_init(&all_list); // all_list 초기화 코드 추가
	list_init(&ready_list);
	list_init(&sleep_list); // sleep_list 초기화 코드 추가
	list_init(&destruction_req);

	/* Set up a thread structure for the running thread. */
	// 실행 중인 스레드를 위한 스레드 구조를 설정한다.
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
/***************************************************************
 * 인터럽트를 활성화함으로써 선점형 스레드 스케줄링을 시작한다.
 * 또한 idle 스레드를 생성한다. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	// 선점형 스레드 스케줄링 시작
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	// idel_thread가 초기화될 때까지 idle 스레드를 기다린다.
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
/********************************************************************
 * 각 타이머 틱에서 인터럽트 핸들러에 의해 호출된다.
 * 따라서, 이 함수는 외부 인터럽트 컨텍스트에서 실행된다. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	// 통계를 업데이트한다. (데이터나 이벤트의 통계 정보를 최신 상태로 갱신하는 작업을 의미함.)
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	// 선점을 강제한다.
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
/********************************************************************
 * 주어진 초기 우선순위를 가진 NAME이라는 이름의 새로운 커널 스레드를 생성하며,
 * AUX를 인자로 전달하면서 FUNCTION을 실행하고, 준비 큐에 추가한다. 새 스레드의 스레드 식별자를
 * 반환하거나, 생성에 실패하면 TID_ERROR를 반환한다.
 *
 * thread_start()가 호출되엇다면, 새 스레드는 thread_create()가 반환되기 전에 스케줄될 수 있다.
 * 심지어 thread_create()가 반환되기 전에 종료될 수도 있다. 반대로, 원래 스레드는 새 스레드가 스케줄될 때까지
 * 얼마든지 실행될 수 있다. 순서를 보장해야 할 경우 세마포어 또는 다른 형태의 동기화를 사용하라.
 *
 * 제공된 코드는 새 스레드의 'priority'멤버를 PRIORITY로 설정하지만, 실제 우선 순위 스케줄링은 구현되지 않았다.
 * 우선순위 스케줄링은 문제 1-3의 목표이다.
 */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);

	/* Allocate thread. */
	// 스레드 할당
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	// 스레드 초기화
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// 스케줄된 경우 kernel_thread를 호출한다.
	// 참고) rdi는 첫 번째 인자이며, rsi는 두 번째 인자이다.
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	// 실행 대기열에 추가한다
	thread_unblock(t);

	// 우선순위에 따른 CPU 선점
	preemption_priority(); /* project 1 priority */

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
/******************************************************************
 * 현재 스레드를 대기 상태로 전환한다. thread_unblock()에 의해 깨어날 때까지 다시 스케줄되지 않는다.
 * 이 함수는 인터럽트가 꺼진 상태에서 호출되어야 한다. synch.h에 있는 동기화 기본 요소 중 하나를 사용하는 것이
 * 보통 더 좋은 생각이다.
 */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF); // block 위해 interrupt off
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/***************************************************************
 * 차단된 스레드 T를 준비-실행 상태로 전환한다. T가 차단되지 않았다면 이는 오류다.
 * (실행중인 스레드를 준비 상태로 만들기 위해서는 thread_yield()를 사용하라.)
 *
 * 이 함수는 실행 중인 스레드를 선점하지 않는다. 이는 중요할 수 있다: 호출자가 스스로 인터럽트를 비활성화했다면,
 * 스레드를 원자적으로 차단 해제하고 다른 데이터를 업데이트할 수 있기를 기대할 수 있다.
 */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	/* project 1 priority */
	// list_push_back(&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, compare_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
// 실행 중인 스레드의 이름을 반환한다.f
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
/************************************************************
 * 실행 중인 스레드를 반환한다. 이것은 running_thread()에 몇 가지 정상성 검사를 추가한 것이다.
 * 자세한 내용은 thread.h 상단의 큰 주석을 참조하라.
 */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	/***********************************************************
	 * T가 정말 스레드인지 확인하라. 이러한 단언 중 하나라도 발생한다면, 스레드가 스택을 오버플로우했을 수 있다.
	 * 각 스레드는 4kb 미만의 스택을 가지고 있으므로, 몇 개의 큰 자동 배열이나 적당한 재귀는 스택 오버플로우를
	 * 일으킬 수 있다.
	 */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);
	// TODO:
	/* compare the priorities of the currently running thread and the newly inserted one. Yield the CPU if the newly arriving thread has higher priority*/
	return t;
}

/* Returns the running thread's tid. */
// 실행 중인 스레드의 tid를 반환한다.
// 여기서 tid는 thread identifier 즉, 스레드 식별자를 의미한다.
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
// 현재 스레드의 스케줄을 취소하고 파괴한다. 호출자에게 절대 반환되지 않는다.
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	// list_remove(&thread_current()->all_elem); // 여기가 아닌가보다 쓰레드가 완전하게 지워지는 곳은 do_schedule

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	/* 단순히 우리의 상태를 '종료됨'으로 설정하고 다른 프로세스를 스케줄한다.
	 * 'schedule_tail()'을 호출하는 동안 우리는 파괴될 것이다.

	 이 코드는 현재 스레드가 종료되어가는 상태임을 설정하고,
	 다른 프로세스를 실행 스케줄로 넘기는 것을 의미합니다.
	 그리고 schedule_tail() 함수가 호출될 때 현재 스레드는 시스템에서 제거됩니다.
	즉, 이 주석에 설명된 기능은 스레드가 종료 절차를 밟고 있으며 곧 시스템 자원을 반환하고 스스로를 해제할 것임을 나타냅니다.*/

	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* CPU를 양보합니다. 현재 스레드는 대기 상태로 전환되지 않으며,
   스케줄러의 재량에 따라 즉시 다시 스케줄될 수 있습니다.

   이 주석은 현재 실행 중인 스레드가 CPU 사용을 중단하고
	다른 스레드에게 CPU를 넘겨주지만,
   대기 상태로 전환되지는 않으며, 스케줄러가 결정하기에 따라 바로
	다시 CPU를 할당받아 실행될 수 있음을 의미합니다.
   즉, 스레드가 CPU를 사용하지 않는 시간을 다른 스레드에게 제공하지만,
   그 스레드가 완전히 중지되는 것은 아니며 언제든지 다시 실행될 준비가
	되어 있다는 것을 나타냅니다. */

void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (curr != idle_thread)
		/* project 1 priority */
		// list_push_back(&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, compare_priority, NULL);
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

// project 1 thread에서 내가 직접 새로 구현하는 함수
// 스레드 재우는 함수
void thread_sleep(int64_t wakeup_ticks)
{
	/* 여기에 스레드 block 처리해서 sleep_list에 넣는 작업 필요*/
	/* if the current thread is not idle thread,
	   change the state of the caller thread to BLOCKED,
	   store the local tick to wake up,
	   update the global tick if necessary,
	   and call schedule()
	   만약 idle 스레드가 아니라면, 스레드 상태를 blocked로 변경하고,
	   로컬tick에 깨어날 시간을 저장해놔라 (변경하고 sleep_list에 삽입). 필요하다면
	   글로벌 tick을 업데이트하고,
	   스케줄함수를 호출해라 (컨텍스트 스위칭)*/
	/* When you manipulate thread list, disable interrupt!
	스레드를 전환하고 있으면 인터럽트를 비활성화 해놔라!*/

	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable(); // 인터럽트 비활성화

	// 현재 스레드가 idle 스레드가 아니면 준비리스트-> 수면리스트로 삽입
	if (curr != idle_thread)
	{
		curr->local_tick = wakeup_ticks;									// local tick에 깨어날 시간 저장해주기
		list_insert_ordered(&sleep_list, &curr->elem, compare_ticks, NULL); // 수면큐에 삽입
		thread_block();														// 현재 스레드 blocked 상태로 변경
	}
	intr_set_level(old_level); // 인터럽트 활성화
}

// 잠자는 스레드 깨우는 함수
void thread_wakeup(int64_t wakeup_ticks)
{

	while (true)
	{
		// local_tick이 최소값을 가지는 스레드 반환
		struct list_elem *curr = list_min(&sleep_list, compare_ticks, NULL);
		struct thread *thread = list_entry(curr, struct thread, elem);

		if (thread->local_tick <= wakeup_ticks)
		{
			enum intr_level old_level;
			old_level = intr_disable(); // 인터럽트 비활성화
			list_remove(curr);			// 수면큐에서 깨울 스레드 지우기
			thread_unblock(thread);		// 스레드 차단 해제
		}
		// 깨어날 스레드가 없으면 return
		else
			return;
	}
}

/* Sets the current thread's priority to NEW_PRIORITY. */
/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정합니다.

 이 코드는 현재 실행 중인 스레드의 우선순위를 새로운 값인 NEW_PRIORITY로 변경하는 기능을 설명합니다.
 이는 스레드 스케줄링에 있어서 해당 스레드의 실행 우선 순위를 조정하는 데 사용됩니다.*/

void thread_set_priority(int new_priority)
{
	if (thread_mlfqs)
		return; /* advanced scheduler 사용 시 우선순위 설정 비활성화 */

	// TODO: Set priority of the current thread.
	// TODO: Reorder the ready_list
	thread_current()->original_priority = new_priority;
	// thread_current()->priority = new_priority; // original_priority만 바꿔줘도 문제 없음
	refresh_priority();

	// 우선순위에 따른 CPU 선점
	preemption_priority(); /* project 1 priority */
}

/* Returns the current thread's priority. */
/* 현재 스레드의 우선순위를 반환합니다. */

int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
/* 현재 스레드의 nice 값을 NICE로 설정합니다.*/
void thread_set_nice(int new_nice UNUSED)
{
	/* TODO: Your implementation goes here */
	ASSERT(NICE_MIN <= new_nice && new_nice <= NICE_MAX)

	enum intr_level old_level = intr_disable();
	thread_current()->nice = new_nice;
	// mlfqs_calculate_recent_cpu(thread_current()); // 빼야함
	mlfqs_calculate_priority(thread_current()); // 변경된 nice 값으로 우선순위 재계산
	// list_sort(&ready_list, compare_priority, NULL);
	preemption_priority(); // 변경된 우선순위로 스케쥴링
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
/* 현재 스레드의 nice 값을 반환합니다. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	/* get nice 할때 인터럽트 비활성화 */
	// enum intr_level old_level = intr_disable();
	// int curr_nice = thread_current()->nice;
	// intr_set_level(old_level);
	// return curr_nice;
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
/* 시스템 부하 평균을 100배하여 반환합니다. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return CONVERT_FP_TO_INT_NEAR(100 * load_avg);
}

/* Returns 100 times the current thread's recent_cpu value. */
/* 현재 스레드의 recent_cpu 값의 100배를 반환합니다. */
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return CONVERT_FP_TO_INT_NEAR(100 * thread_current()->recent_cpu);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */

/* 유휴 스레드. 다른 스레드가 실행 준비가 되어 있지 않을 때 실행됩니다.

   유휴 스레드는 처음에 thread_start()에 의해 준비 목록에 올라갑니다.
   처음에 한 번 스케줄되며, 이때 idle_thread를 초기화하고, thread_start()가
   계속될 수 있도록 전달된 세마포어를 "up"시키고 즉시 블록됩니다.
   그 후에는 유휴 스레드가 준비 목록에 나타나지 않습니다.
   준비 목록이 비어 있을 때 특별한 경우로서 next_thread_to_run()에 의해 반환됩니다. */

/* 이 코드는 시스템에 다른 스레드가 실행할 준비가 되어 있지 않을 때 실행되는 특별한 스레드인
 '유휴 스레드(idle thread)'에 대해 설명하고 있습니다. 유휴 스레드는 시스템이 시작될 때 thread_start() 함수에
  의해 준비 목록에 추가되고, 처음에 한 번 스케줄되어 필요한 초기화 작업을 수행합니다.
  이 초기화 작업에는 세마포어를 'up’하는 것이 포함되어, thread_start() 함수가 계속 진행될 수 있도록 합니다.
   초기화가 완료된 후 유휴 스레드는 더 이상 준비 목록에 나타나지 않으며,
   준비 목록이 비어 있을 때만 next_thread_to_run() 함수에 의해 선택되어 실행됩니다.
   이는 시스템에 실행할 준비가 된 다른 스레드가 없을 때 CPU가 놀지 않고 유휴 상태를 유지하도록 하는 역할을 합니다.*/
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		/* 다른 누군가에게 실행을 양보하세요.

		이 코드는 현재 실행 중인 스레드나 프로세스가 CPU 사용을 중단하고
		다른 스레드나 프로세스에게 실행 기회를 넘겨주는 것을 의미합니다.
		이는 다중 스레딩 환경에서 자원을 공유하고 효율적인 작업 스케줄링을 가능하게 하는 중요한 기능입니다.*/
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */

		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

		 'sti' 명령어는 다음 명령어가 완료될 때까지 인터럽트를 비활성화하므로,
		 이 두 명령어는 원자적으로 실행됩니다. 이러한 원자성은 중요합니다;
		 그렇지 않으면, 인터럽트를 다시 활성화하고 다음 인터럽트가 발생하기를
		 기다리는 사이에 인터럽트가 처리될 수 있으며, 이는 최대 한 클록 틱의
		 시간을 낭비할 수 있습니다.

		 [IA32-v2a] "HLT", [IA32-v2b] "STI", 그리고 [IA32-v3a] 7.11.1 "HLT 명령어"를
		 참조하세요.

		 이 주석은 시스템의 인터럽트를 다시 활성화하고 다음 인터럽트가 발생할 때까지 기다리는 프로세스를 설명합니다.
		 ‘sti’ 명령어는 다음 명령어가 완료될 때까지 인터럽트를 비활성화하는데,
		  이는 인터럽트 처리와 다음 인터럽트 대기 사이에 발생할 수 있는 시간 낭비를 방지하기 위해 중요합니다.
		  또한, 이 주석은 관련 IA32 아키텍처 문서의 섹션을 참조하라고 안내하고 있습니다.
		  이는 프로그래머가 해당 명령어의 상세한 작동 방식을 이해하고 올바르게 사용할 수 있도록 돕기 위함입니다.
		  */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
/* 커널 스레드의 기초로 사용되는 함수입니다. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
/* T를 'NAME'이라는 이름의 차단된 스레드로 기본 초기화합니다.

이 코드는 스레드 T를 생성하고, 그것을 차단(blocked) 상태로 초기화하는 과정을 설명합니다.
여기서 'NAME’은 초기화되는 스레드의 이름을 나타냅니다.
차단된 스레드는 일반적으로 실행을 시작할 준비가 되지 않았거나,
특정 조건이 충족될 때까지 실행을 중지한 상태를 의미합니다.
이 초기화 과정은 스레드가 시스템에 올바르게 등록되고 관리될 수 있도록 하는 필수적인 단계입니다.
*/
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);

	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/* 4BSD */
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;

	// 디버그 출력문
	// printf("Adding thread: %s to all_list\n", t->name);
	list_push_back(&all_list, &t->all_elem);
	// printf("Thread: %s added to all_list\n", t->name);

	// donation
	t->original_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&(t->donations));
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
/* 스케줄될 다음 스레드를 선택하고 반환합니다. 실행 대기열에서 스레드를 반환해야 합니다,
실행 대기열이 비어 있지 않는 한. (실행 중인 스레드가 계속 실행될 수 있다면,
그 스레드는 실행 대기열에 있을 것입니다.) 실행 대기열이 비어 있다면,
idle_thread를 반환합니다.

이 코드는 스케줄러가 실행할 다음 스레드를 선택하는 과정을 설명합니다.
일반적으로 실행 대기열(run queue)에 있는 스레드 중 하나를 선택하여 반환하지만,
실행 대기열이 비어 있을 경우에는 유휴 스레드(idle_thread)를 반환합니다.
이는 시스템이 효율적으로 스레드를 관리하고, 실행 준비가 완료된 스레드를 적절히 실행시키기 위한 프로세스의 일부입니다.*/
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else // ready_list의 맨 앞에서 꺼냄
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
/* 스레드를 시작하기 위해 iretq를 사용합니다.

이 코드는 iretq 명령어를 사용하여 스레드를 시작하는 방법을 설명합니다.
 iretq는 인터럽트 서비스 루틴에서 사용되는 명령어로,
 스레드나 프로세스의 컨텍스트를 복원하고 실행을 재개하는 데 사용됩니다.
 이는 프로그램의 실행 흐름을 제어하는 데 중요한 역할을 하는 명령어입니다.
 */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
/* 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고,
   이전 스레드가 종료되고 있다면 그것을 파괴합니다.

   이 함수가 호출될 때, 우리는 방금 PREV 스레드에서 전환했으며,
   새 스레드는 이미 실행 중이고 인터럽트는 여전히 비활성화 상태입니다.

   스레드 전환이 완료될 때까지 printf()를 호출하는 것은 안전하지 않습니다.
   실제로 이는 함수의 끝부분에 printf()를 추가해야 함을 의미합니다.

   이 주석은 새로운 스레드로의 전환 과정을 설명하고 있습니다.
   새 스레드의 페이지 테이블을 활성화함으로써 스레드 전환이 이루어지고,
   만약 이전 스레드가 종료 과정에 있다면 그 스레드를 시스템에서 제거합니다.
   또한, 이 함수가 호출되는 시점에서는 이미 새 스레드가 실행 중이며,
   인터럽트는 비활성화된 상태로 유지됩니다. printf() 함수는 스레드 전환이 완전히 완료될 때까지 호출해서는
   안 되며, 이는 보통 함수의 마지막 부분에서 printf()를 사용해야 함을 의미합니다.
   이는 스레드 전환 과정 중에 발생할 수 있는 문제를 방지하기 위한 안전 조치입니다.
   */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	/* 주요 스위칭 로직입니다.
	* 우리는 먼저 전체 실행 컨텍스트를 intr_frame에 복원합니다
	* 그리고 do_iret을 호출하여 다음 스레드로 전환합니다.
	* 주의할 점은, 여기서부터 전환이 완료될 때까지
	* 어떠한 스택도 사용해서는 안 된다는 것입니다.

	이 주석은 스레드 전환 과정에서 수행해야 할 주요 단계들을 설명하고 있습니다.
	스레드가 다른 스레드로 전환하기 전에 현재 실행 중인 컨텍스트를 intr_frame에 저장하고,
	 do_iret 함수를 호출하여 실제로 다음 스레드로의 전환을 수행한다는 내용을 담고 있습니다.
	 또한, 전환 과정이 완료될 때까지 스택을 사용하지 말아야 한다는 중요한 지침을 강조하고 있습니다.
	  이는 스택의 내용이 변경되어 현재 실행 중인 스레드의 상태가 손상될 수 있기 때문입니다.
	*/

	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
/* 새 프로세스를 스케줄합니다. 함수가 호출될 때, 인터럽트는 꺼져 있어야 합니다.
 * 이 함수는 현재 스레드의 상태를 변경한 후
 * 실행할 다른 스레드를 찾아 그것으로 전환합니다.
 * schedule() 내에서 printf()를 호출하는 것은 안전하지 않습니다.
 *
 * 이 코드는 새로운 프로세스를 스케줄링하는 함수에 대한 설명입니다.
 *  함수가 호출되기 전에 인터럽트가 비활성화되어 있어야 하며,
 * 이 함수는 현재 스레드의 상태를 변경한 다음 실행할 준비가 된 다른 스레드를 찾아 그 스레드로 전환하는 작업을
 *  수행합니다. 또한, schedule() 함수 내에서 printf() 함수를 호출하는 것은 안전하지 않다고 언급되어 있습니다.
 *  이는 schedule() 함수가 중요한 시스템 자원을 조작하는 과정에서 발생할 수 있는 문제를 방지하기
 *  위한 주의 사항입니다.
 *
 *  */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);				// 인터럽트가 비활성화 상태여야 함을 확인
	ASSERT(thread_current()->status == THREAD_RUNNING); // 현재 스레드가 실행 중 상태임을 확인
	while (!list_empty(&destruction_req))
	{
		// destruction_req 리스트에서 스레드를 하나씩 가져와서 메모리 해제
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		list_remove(&victim->all_elem); // all_list에서 스레드 제거 // all_elem 삭제를 thread_exit()이 아닌 do_schedule에서 해주어야한다.
		palloc_free_page(victim);		// 스레드의 메모리 해제
	}
	thread_current()->status = status; // 현재 스레드의 상태를 설정
	schedule();						   // 스케줄링을 다시 수행
}

static void
schedule(void)
{
	struct thread *curr = running_thread();		// 현재 실행 중인 스레드를 가져옴
	struct thread *next = next_thread_to_run(); // 다음에 실행할 스레드를 선택

	ASSERT(intr_get_level() == INTR_OFF);	// 인터럽트가 비활성화 상태여야 함을 확인
	ASSERT(curr->status != THREAD_RUNNING); // 현재 스레드가 실행 중 상태가 아님을 확인
	ASSERT(is_thread(next));				// 다음 스레드가 올바른 스레드인지 확인
	/* Mark us as running. */
	next->status = THREAD_RUNNING; /* 다음 스레드를 실행 상태로 표시 */

	/* Start new time slice. */
	thread_ticks = 0; /* 새로운 time slice를 시작 */

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next); /* 새로운 주소 공간을 활성화 */
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		/* 우리가 전환한 스레드가 종료되고 있다면, 그 스레드의 구조체를
		 파괴합니다. 이 작업은 thread_exit() 함수가 자기 자신을
		 무너뜨리지 않도록 늦게 수행되어야 합니다.
		 여기서는 페이지 해제 요청을 큐에 넣기만 합니다. 왜냐하면 해당 페이지가
		 현재 스택에 의해 사용되고 있기 때문입니다.
		 실제 파괴 로직은 schedule()의 시작 부분에서 호출될 것입니다.

		 이 주석은 현재 종료되고 있는 스레드가 있을 경우,
		 해당 스레드의 struct thread를 파괴해야 한다고 언급하고 있습니다.
		 이러한 파괴 작업은 thread_exit() 함수가 실행 중인 스레드의 컨텍스트를 손상시키지 않도록,
		  즉 ‘자기 자신 아래의 바닥을 빼앗기지’ 않도록 스케줄링 과정의 뒷부분에서 이루어져야 합니다.
		  또한, 페이지를 해제하는 요청은 현재 스택에 의해 사용되고 있기 때문에,
		  이 시점에서는 요청을 큐에 넣기만 하고, 페이지의 실제 해제는 스케줄러가 시작될 때 수행될 것임을
		  설명하고 있습니다. 이는 스레드의 안전한 종료와 자원의 정확한 회수를 보장하기 위한 조치입니다.
		  */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);						   // 현재 스레드와 다음 스레드가 다름을 확인
			list_push_back(&destruction_req, &curr->elem); // 현재 스레드를 destruction_req 리스트에 추가
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		/* 스레드를 전환하기 전에, 우리는 먼저 현재 실행 중인 정보를 저장합니다.

		이 코드는 스레드 전환 과정에서 현재 실행 중인 스레드의 정보를 먼저 저장하는 절차를 설명합니다.
		이는 스레드가 나중에 다시 실행될 때 필요한 상태 정보를 보존하기 위한 중요한 단계입니다.
		이렇게 정보를 저장함으로써, 시스템은 스레드가 중단된 지점부터 안전하게 재개할 수 있습니다.
		 */
		thread_launch(next); // 다음 스레드를 실행
	}
}

/* Returns a tid to use for a new thread. */
/* 새로운 스레드를 위한 스레드 식별자(tid)를 반환합니다.

이 코드는 새로 생성되는 스레드에 고유한 식별자를 할당하는 함수나 메커니즘을 설명하는 주석입니다.
여기서 'tid’는 'thread identifier’의 약자로, 각 스레드를 구별하기 위한 고유한 번호를 의미합니다.
이 식별자는 스레드를 관리하고, 스레드 간의 통신이나 동기화를 수행할 때 중요한 역할을 합니다.
*/
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

/* 두 스레드의 wakeup_tick 값을 비교하는 함수 a의 tick이 b의 tick보다 작으면 1(true), 크면 0(false) */
bool compare_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *thread_a = list_entry(a, struct thread, elem);
	struct thread *thread_b = list_entry(b, struct thread, elem);
	return thread_a->local_tick < thread_b->local_tick;
}

// 내림차순 정렬 만드는 함수. a의 우선순위가 b의 우선순위보다 크면 1(true) 리턴. 반대의 경우 0(false) 리턴
bool compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *thread_a = list_entry(a, struct thread, elem);
	struct thread *thread_b = list_entry(b, struct thread, elem);
	return thread_a->priority > thread_b->priority;
}

// 현재 실행 중인 스레드의 우선순위가 ready list의 스레드보다 낮다면 CPU를 양보(yield)하는 함수
void preemption_priority(void) /* project 1 priority */
{
	// 현재 실행 중인 스레드가 idle 스레드인 경우 아무 작업도 필요하지 않으므로 함수 종료
	// ready list가 비어 있는지 확인하고, 비어 있다면 다른 스레드가 대기 중이 아니므로 함수 종료
	if (thread_current() == idle_thread || list_empty(&ready_list))
	{
		return;
	}

	// ready list에서 가장 우선순위가 높은 스레드를 가리키는 포인터를 얻어옴
	struct list_elem *first = list_front(&ready_list);
	struct thread *first_t = list_entry(first, struct thread, elem);

	// 현재 실행 중인 스레드의 우선순위가 ready list의 첫 번째 스레드의 우선순위보다 낮은지 확인
	// 만약 그렇다면, 현재 스레드의 우선순위가 더 낮으므로 다른 스레드에게 CPU를 양보
	if (!list_empty(&ready_list) && thread_current()->priority < first_t->priority)
	{
		thread_yield(); // CPU 양보
	}
}

/**
 * @brief donate_priority 함수는 대기 중인 락의 소유자에게 현재 스레드의 우선순위를 기부합니다.
 *        최대 반복 횟수까지 대기 중인 락을 따라가며 우선순위 기부를 처리합니다.
 */
void donate_priority(void)
{
	int depth;
	struct thread *curr_thread = thread_current();

	// 대기 중인 락의 소유자에게 우선순위를 기부
	// while (curr_thread->wait_on_lock != NULL) // 이렇게 해도 문제 없음
	for (depth = 0; depth < MAX_NESTED_DEPTH; depth++) // MAX_NESTED_DEPTH를 설정하는 이유: 무한한 우선순위 기부 상황 방지
	{
		if (curr_thread->wait_on_lock == NULL)
		{
			// 더 이상 대기 중인 락이 없으면 반복 종료
			break;
		}
		else
		{
			// 대기 중인 락의 소유자에게 우선순위를 기부
			struct thread *holder = curr_thread->wait_on_lock->holder;
			if (holder->priority < curr_thread->priority)
			{
				// 대기 중인 락의 소유자의 우선순위를 현재 스레드의 우선순위로 업데이트
				holder->priority = curr_thread->priority;
			}

			// 대기 중인 락의 소유자를 현재 스레드로 설정하여 다음 반복을 위해 준비
			curr_thread = holder;
		}
	}
}

/**
 * @brief remove_donation 함수는 현재 스레드에게 기부된 스레드 중에서 주어진 lock에 대한 기부를 제거합니다.
 *        기부된 각 스레드를 순회하면서 해당 lock을 기다리는 스레드를 찾고, 그 스레드의 기부를 제거합니다.
 *
 * @param lock 기부를 제거할 lock
 */
void remove_donation(struct lock *lock)
{
	struct list_elem *e;
	struct thread *curr_thread = thread_current(); // thread_current() == lock->holder

	// 현재 스레드에게 기부된 모든 스레드를 순회
	for (e = list_begin(&curr_thread->donations); e != list_end(&curr_thread->donations); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, donation_elem); // 리스트의 요소를 스레드로 변환
		if (t->wait_on_lock == lock)									// 해당 스레드가 지정된 lock을 기다리고 있는지 확인
		{																// wait_on_lock이 이번에 relrease하는 lock이라면
			list_remove(&t->donation_elem);								// 해당 스레드의 기부를 제거(donation_elem 리스트에서 제거)
		}
	}
}

// refresh_priority 함수는 현재 스레드의 우선순위를 갱신하는 함수입니다.
// 현재 스레드의 원래 우선순위로 되돌리고, 다른 스레드로부터의 우선순위 기부가 있는 경우,
// 가장 높은 우선순위를 현재 스레드의 우선순위로 설정합니다.
void refresh_priority(void)
{
	struct thread *curr_thread = thread_current();

	curr_thread->priority = curr_thread->original_priority; // 현재 donation 받은 우선순위를 원래 자신의 우선순위로 바꾸기

	if (!list_empty(&curr_thread->donations))						// 현재 스레드에게 기부된 우선순위가 있는지 확인
	{																// donations list가 비어 있지 않다면(아직 우선순위를 줄 스레드가 있다면)
		list_sort(&curr_thread->donations, compare_priority, NULL); // donations 내림차순으로 정렬(가장 큰 우선순위 맨 앞으로)

		struct thread *front = list_entry(list_front(&curr_thread->donations), struct thread, donation_elem); // 가장 높은 우선순위를 가진 스레드를 가져옴
		if (front->priority > curr_thread->priority)														  // 가장 높은 우선순위가 현재 스레드의 우선순위보다 높으면
		{
			curr_thread->priority = front->priority; // 현재 스레드의 우선순위를 가장 높은 우선순위로 업데이트
		}
	}
}

/* 4BSD */

/* 스레드의 우선순위를 계산하는 함수.
   t->priority = PRI_MAX - (t->recent_cpu / 4) - (t->nice * 2) */
void mlfqs_calculate_priority(struct thread *t)
{
	t->priority = PRI_MAX - CONVERT_FP_TO_INT_ZERO(t->recent_cpu / 4) - (t->nice * 2);
}

/* 스레드의 최근 CPU 사용량을 계산하는 함수.
   decay = (2 * load_avg) / (2 * load_avg + 1)
   t->recent_cpu = decay * t->recent_cpu + t->nice */
void mlfqs_calculate_recent_cpu(struct thread *t)
{
	int decay = DIV_FP((load_avg * 2), ADD_FP_INT((load_avg * 2), 1));
	t->recent_cpu = ADD_FP_INT(MUL_FP(decay, t->recent_cpu), t->nice);
}

/* 평균 부하량(load_avg)을 계산하는 함수.
   load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads */
void mlfqs_calculate_load_avg(void)
{
	// int ready_threads = list_size(&ready_list);
	int ready_threads;

	/* 현재 실행 중인 스레드가 idle_thread인지 확인
       idle_thread는 CPU가 유휴 상태임을 나타냅니다. */
    if (thread_current() == idle_thread)
        /* CPU가 유휴 상태인 경우, ready_list에 있는 스레드 수를 그대로 사용 */
        ready_threads = list_size(&ready_list);
    else
        /* CPU가 유휴 상태가 아닌 경우, 현재 실행 중인 스레드도 준비 상태로 간주
           따라서, ready_list에 있는 스레드 수에 1을 더함 */
        ready_threads = list_size(&ready_list) + 1;
	
	// load_avg = MUL_FP(DIV_FP(CONVERT_INT_TO_FP(59), CONVERT_INT_TO_FP(60)), load_avg) + DIV_FP(CONVERT_INT_TO_FP(1), CONVERT_INT_TO_FP(60)) * ready_threads;
	// 위와 같음
	load_avg = MUL_FP((CONVERT_INT_TO_FP(59)/ 60), load_avg) + (CONVERT_INT_TO_FP(1) / 60) * ready_threads;
}

/* 최근 CPU 사용량을 증가시키는 함수.
   idle 스레드가 아닌 경우 현재 실행 중인 스레드의 recent_cpu를 1 증가시킴 */
void mlfqs_increase_recent_cpu(void)
{
	if (thread_current() != idle_thread)
	{
		// printf("not idle\n");
		thread_current()->recent_cpu = ADD_FP_INT(thread_current()->recent_cpu, 1);
	}
}

/* 모든 스레드의 우선순위를 재계산하는 함수. */
void mlfqs_recalculate_priority(void)
{
	struct list_elem *e;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, all_elem);
		mlfqs_calculate_priority(t);
	}
}

/* 모든 스레드의 최근 CPU 사용량을 재계산하는 함수. */
void mlfqs_recalculate_recent_cpu(void)
{
	struct list_elem *e;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, all_elem);
		mlfqs_calculate_recent_cpu(t);
	}
}
