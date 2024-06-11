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
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/malloc.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);

void argument_stack(char **argv, int argc, void **rsp);
struct thread *get_child_process(int pid);
void remove_child_process(struct thread *cp);

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */

/**
 * @brief 주어진 파일 이름으로 초기화된 프로세스를 생성합니다.
 *
 * @param file_name 실행할 파일 이름.
 * @return 생성된 스레드의 TID.
 */
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy;	 // 파일 이름의 복사본을 저장할 포인터
	char *exec_name; // 실행할 파일의 이름을 추출하여 저장할 포인터
	char *save_ptr;	 // strtok_r 함수를 사용하여 토큰을 추출할 때 사용할 포인터
	tid_t tid;		 // 생성된 스레드의 ID를 저장할 변수

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0); // 페이지 할당 함수를 통해 메모리를 할당받습니다.
	if (fn_copy == NULL)		  // 할당된 메모리가 없을 경우 에러를 반환
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE); // file_name의 복사본을 fn_copy에 저장 (PGSIZE를 넘지 않도록 주의)

	// /* Extract the first token from FILE_NAME. */	 /* FILE_NAME에서 첫 번째 토큰을 추출합니다. */
	exec_name = strtok_r(file_name, " ", &save_ptr);			 // 공백을 기준으로 첫 번째 토큰을 추출하여 save_ptr에 저장합니다.
	/* Create a new thread to execute FILE_NAME. */				 /* FILE_NAME을 실행할 새로운 스레드를 생성합니다. */
	tid = thread_create(exec_name, PRI_DEFAULT, initd, fn_copy); // file_name을 실행할 새로운 스레드를 생성합니다.
	if (tid == TID_ERROR)										 // 스레드 생성에 실패한 경우
		palloc_free_page(fn_copy);								 // 할당된 메모리를 해제합니다.
	return tid;													 // 생성된 스레드의 ID를 반환합니다.
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init();

	// lock_init(&load_lock);

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{

	// // /* Clone current thread to new thread.*/
	// // return thread_create(name, PRI_DEFAULT, __do_fork, thread_current());
	/* project 2 system call */
	struct thread *parent = thread_current();

	/* Save the parent's intr_frame in a global variable */
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame)); // 부모 프로세스 메모리를 복사

	tid_t child_tid = thread_create(name, PRI_DEFAULT, __do_fork, parent); // 전달받은 thread_name으로 __do_fork()를 진행

	if (child_tid == TID_ERROR)
	{
		return TID_ERROR;
	}

	struct thread *child = get_child_process(child_tid);
	if (child == NULL)
		return TID_ERROR;

	/* Ensure parent waits for the child to successfully clone */
	sema_down(&child->load_sema);

	if (child->exit_status == -1)
	{
		return TID_ERROR;
	}

	return child_tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */

/**
 * @brief 부모의 주소 공간을 복사하기 위해 pml4_for_each에 전달되는 함수입니다.
 *
 * @param pte 페이지 테이블 엔트리에 대한 포인터입니다.
 * @param va 가상 주소입니다.
 * @param aux 부모 스레드에 대한 포인터입니다.
 *
 * @return 성공하면 true, 실패하면 false를 반환합니다.
 */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */ /* 1. 부모의 페이지가 커널 페이지이면 즉시 반환 */
	if (is_kernel_vaddr(va))
	{
		// return false ends pml4_for_each. which is undesirable - just return true to pass this kernel va
		return true;
	}
	/* 2. Resolve VA from the parent's page map level 4. */ /* 2. 부모의 pml4에서 VA를 해석하여 페이지를 가져옵니다. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		printf("Virtual address(%llx) is not assigned in parent thread's page table.\n", va);
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	/* 3. 자식을 위해 새로운 PAL_USER 페이지를 할당하고 NEWPAGE에 설정합니다. */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO); // PAL_USER 플래그 : 사용자 페이지를 할당, PAL_ZERO 플래그 : 할당된 페이지를 0으로 초기화
	if (newpage == NULL)
	{
		printf("New page can't be allocated in a current thread.\n");
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	/* 4. 부모의 페이지를 새로운 페이지로 복사하고, 부모의 페이지가 쓰기 가능한지 확인합니다. */
	memcpy(newpage, parent_page, PGSIZE);
	// if (pte && (*pte & PTE_W))
	writable = is_writable(pte); // pte는 parent_page를 가리키는 주소

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	/* 5. VA 주소에 새로운 페이지를 쓰기 가능한 권한으로 자식의 페이지 테이블에 추가합니다. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		/* 6. 페이지 삽입에 실패하면 오류를 처리합니다. */
		// printf("Failed to add new page(%p) to current thread's page table at va(%p).\n", newpage, va); // (multi-oom) 이 메세지가 뜨기는 하는데 주석 처리하면 테스트 통과는 함
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));
	if_.R.rax = 0; // 자식 프로세스의 리턴값은 0

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current); // tss를 업데이트 해준다.
#ifdef VM
	supplemental_page_table_init(&current->spt);
	bool lock = false;
	if (!lock_held_by_current_thread(&filesys_lock))
	{
		lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		lock = true;
	}
	bool copy = supplemental_page_table_copy(&current->spt, &parent->spt);
	if (lock)
	{
		lock_release(&filesys_lock); // ADD: filesys_lock at file system
		lock = false;
	}
	if (!copy)
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	enum intr_level old_level = intr_disable();
	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	// lock_acquire(&filesys_lock); // ADD: filesys_lock at file system

	/* Duplicate file descriptors */ /* 파일 디스크립터 복제 */
	for (int fd = 2; fd < MAX_FILES; fd++)
	{
		if (parent->fd_table[fd] != NULL)
		{
			current->fd_table[fd] = file_duplicate(parent->fd_table[fd]);
			if (current->fd_table[fd] == NULL)
				goto error;
		}
	}

	// lock_release(&filesys_lock); // ADD: filesys_lock at file system

	current->next_fd = parent->next_fd; // next_fd도 복제

	// /* Notify parent that fork is successful */
	sema_up(&current->load_sema); // Notify parent that fork is successful // 로드가 완료될 때까지 기다리고 있던 부모 대기 해제

	intr_set_level(old_level);

	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->load_sema);
	exit(TID_ERROR);
	// thread_exit();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */

/**
 * @brief 주어진 파일 이름으로 실행 컨텍스트를 전환합니다.
 * 실패 시 -1을 반환합니다.
 *
 * @param f_name 실행할 파일 이름과 인자 문자열.
 * @return 성공 시 0, 실패 시 -1.
 */
int process_exec(void *f_name)
{
	char *file_name = f_name;
	bool success;

	// printf("Starting process_exec 시작!\n");	  /* Debug */
	// printf("파일명(file_name): %s\n", file_name); /* Debug */

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	/* intr_frame을 thread 구조체 내에서 사용할 수 없습니다.
	 * 스케줄링될 때 현재 스레드가 실행 정보를 구조체 멤버에 저장하기 때문입니다. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */ /* 현재 문맥을 정리합니다. */
	process_cleanup();

	/* arguments passing - kmj */
	/* Parse file_name and save tokens on user stack. */ /* file_name을 파싱하고 실행 파일 이름과 인자들을 분리하여 사용자 스택에 토큰을 저장합니다. */
	char *token, *save_ptr;
	int argc = 0;
	char *argv[128]; // 최대 128개의 인자를 처리한다고 가정

	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; // 명령줄을 파싱합니다.
		 token = strtok_r(NULL, " ", &save_ptr))
	{
		argv[argc++] = token;
	}

	// printf("파스 ! Parsed %d arguments:\n", argc); /* Debug */
	// for (int i = 0; i < argc; i++)
	// {
	// 	printf("인자 ! argv[%d]: %s\n", i, argv[i]); /* Debug */
	// }

	/* And then load the binary */ /* 바이너리를 로드합니다. */
	// success = load(file_name, &_if);
	/* 실행 파일 이름을 load 함수의 첫 번째 인자로 전달합니다. */

	// lock_acquire(&load_lock); // ADD: load_lock
	lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
	success = load(argv[0], &_if);
	lock_release(&filesys_lock); // ADD: filesys_lock at file system
	// lock_release(&load_lock); // ADD: load_lock

	/* If load failed, quit. */ /* 로드에 실패하면 종료합니다. */
	if (!success)
	{
		// printf("로드 실패... Load failed\n"); /* Debug */
		return -1;
	}

	/* Save arguments on user stack */ /* 인자를 사용자 스택에 저장합니다. */
	argument_stack(argv, argc, &_if.rsp);
	_if.R.rsi = (uint64_t)_if.rsp + sizeof(void *);
	_if.R.rdi = argc;

	// 유저 스택 메모리 확인 (디버깅용)
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true); /* 유저 스택의 내용을 16진수로 출력합니다 */

	/* 페이지 할당 해제 */
	palloc_free_page(file_name);
	/* Start switched process. */ /* 프로세스를 시작합니다. */
	// printf("프로세스 시작 ! Starting switched process\n"); /* Debug */
	do_iret(&_if);
	NOT_REACHED();
}

/**
 * @brief 프로그램 이름과 인자들을 유저 스택에 저장하는 함수
 *
 * @param argv 프로그램 이름과 인자가 저장된 메모리 공간의 포인터 배열
 * @param argc 인자의 개수
 * @param rsp 스택 포인터를 가리키는 주소
 */
void argument_stack(char **argv, int argc, void **rsp)
{
	int i;
	char *arg_addresses[argc];

	// printf("Starting argument_stack\n"); /* Debug */
	// printf("Initial rsp: %p\n", *rsp);	 /* Debug */

	// 1. 데이터를 스택에 넣어준다. // 스택에 인자들을 저장합니다.
	for (i = argc - 1; i >= 0; i--)
	{
		*rsp -= strlen(argv[i]) + 1;
		memcpy(*rsp, argv[i], strlen(argv[i]) + 1);
		arg_addresses[i] = *rsp;
		// printf("Pushed argument %d: %s at %p\n", i, argv[i], *rsp); /* Debug */
	}

	// 2. 단어 정렬을 위해 8의 배수로 맞춰준다. // 스택 포인터를 8바이트 단위로 정렬합니다.
	while ((uintptr_t)*rsp % 8 != 0)
	{
		*rsp -= 1;
		*(uint8_t *)(*rsp) = 0;
	}
	// printf("Stack aligned to 8 bytes: %p\n", *rsp); /* Debug */

	// NULL 포인터를 스택에 저장합니다 (argv[argc] = NULL). // "\0"을 통해 스트링이 끝났다는 것을 C standard가 알 수 있음.
	*rsp -= sizeof(char *);
	*(char **)(*rsp) = NULL;
	// printf("Pushed NULL sentinel at %p\n", *rsp); /* Debug */

	// 인자들의 주소를 스택에 저장합니다.
	for (i = argc - 1; i >= 0; i--)
	{
		*rsp -= sizeof(char *);
		*(char **)(*rsp) = arg_addresses[i];
		// printf("Pushed argv[%d] address: %p\n", i, arg_addresses[i]); /* Debug */
	}

	// // argv (첫 번째 인자의 포인터)를 스택에 저장합니다.
	// *rsp -= sizeof(char **);
	// *(char ***)(*rsp) = (char **)(*rsp + sizeof(char **));
	// // printf("Pushed argv pointer at %p\n", *rsp); /* Debug */

	// // argc (인자의 개수)를 스택에 저장합니다.
	// *rsp -= sizeof(int);
	// *(int *)(*rsp) = argc;
	// // printf("Pushed argc: %d at %p\n", argc, *rsp); /* Debug */

	// 가짜 반환 주소를 스택에 저장합니다.
	*rsp -= sizeof(void *);
	*(void **)(*rsp) = NULL;
	// printf("Pushed fake return address at %p\n", *rsp); /* Debug */

	// printf("Finished argument_stack\n"); /* Debug */
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	// /* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	//  * XXX:       to add infinite loop here before
	//  * XXX:       implementing the process_wait. */
	// for (int i = 0; i < 1000000000; i++)
	// {
	// 	/* code */
	// }
	// return -1;

	struct thread *child = get_child_process(child_tid); // 자식 프로세스를 가져옵니다.
	if (child == NULL)
	{
		return -1; // 자식 프로세스가 없으면 -1을 반환합니다.
	}

	// 자식이 종료될 때까지 부모를 재운다. (process_exit에서 자식이 종료될 때 sema_up 해줄 것이다.)
	sema_down(&child->wait_sema);
	// 자식이 종료됨을 알리는 `wait_sema` signal을 받으면 -> 재운 부모가 깨어남
	// 자식의 종료 상태를 가져온다.
	int exit_status = child->exit_status;
	// 현재 스레드(부모)의 자식 리스트에서 제거한다.
	list_remove(&child->child_elem);
	// 자식이 완전히 종료되고 스케줄링이 이어질 수 있도록 자식에게 signal을 보낸다.
	sema_up(&child->exit_sema);

	remove_child_process(child); // 자식 프로세스 메모리 해제
	return exit_status;			 // 자식의 exit_status를 반환한다.
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *curr = thread_current();

	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	// lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
	// Close all open file descriptors. /* 모든 파일 디스크립터를 닫습니다. */
	for (int fd = 2; fd < MAX_FILES; fd++)
	{
		if (curr->fd_table[fd] != NULL)
		{
			file_close(curr->fd_table[fd]);
			curr->fd_table[fd] = NULL;
		}
	}
	// lock_release(&filesys_lock); // ADD: filesys_lock at file system

	palloc_free_page(curr->fd_table);
	// palloc_free_multiple(curr->fd_table, 2); // for multi-oom(메모리 누수) fd 0,1 은 stdin, stdout

	// Close the running file.
	if (curr->run_file != NULL)
	{
		// lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		file_close(curr->run_file);
		// lock_release(&filesys_lock); // ADD: filesys_lock at file system
		curr->run_file = NULL;
	}
	// file_close(curr->run_file); // 현재 실행 중인 파일을 닫는다. // for rox- (실행중에 수정 못하도록)
	// curr->run_file = NULL;

	// Clean up process resources.
	process_cleanup(); // pml4를 해제(이 함수를 call 한 thread의 pml4)
	// 🚨 위치 변경 exit을 할 때, 부모보다 먼저 종료된 후에 부모를 깨워준다...? 🫠

	// Notify parent that we are exiting. /* 부모에게 종료 상태를 알려줍니다. */
	sema_up(&curr->wait_sema); // 자식 스레드가 종료될 때 대기하고 있는 부모에게 signal을 보낸다. // 종료되었다고 기다리고 있는 부모 thread에게 signal 보냄-> sema_up에서 val을 올려줌

	// Wait for parent to acknowledge exit.
	sema_down(&curr->exit_sema); // 자식 스레드가 완료되었음을 알리는 세마포어를 사용합니다. // 부모의 signal을 기다린다. 대기가 풀리고 나서 do_schedule(THREAD_DYING)이 이어져 다른 스레드가 실행된다. // 부모의 exit_Status가 정확히 전달되었는지 확인(wait)
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

struct thread *get_child_process(int pid)
{
	/* 자식 리스트에 접근하여 프로세스 디스크립터 검색 */
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, child_elem);
		/* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
		if (t->tid == pid)
		{
			return t;
		}
	}
	/* 리스트에 존재하지 않으면 NULL 리턴 */
	return NULL;
}

void remove_child_process(struct thread *cp)
{
	/* 자식 리스트에서 제거*/
	/* 프로세스 디스크립터 메모리 해제 */
	// 현재 스레드의 자식 리스트를 가져옵니다.
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;

	// 자식 리스트에서 cp를 찾습니다.
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t == cp)
		{
			// 리스트에서 요소 제거
			list_remove(e);

			// 프로세스 디스크립터 메모리 해제
			palloc_free_page(t); // or `free(t)` depending on how memory is allocated

			return;
		}
	}
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
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

struct ELF64_PHDR
{
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

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current());

	/* Open executable file. */
	file = filesys_open(file_name);
	if (file == NULL)
	{
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* project 2 system call */
	// 현재 실행중인 파일의 경우 write 할 수 없도록 설정 // for rox-simple
	file_deny_write(file);
	// 스레드가 삭제될 때 파일을 닫을 수 있게 구조체에 파일을 저장해둔다.
	t->run_file = file;

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
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
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(if_)) // user stack 초기화
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry; // entry point 초기화
							 // rip: 프로그램 카운터(실행할 다음 인스트럭션의 메모리  주소)

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file); // load에서 file_close(file)을 해주면 file이 닫히면서 lock이 풀리게 된다. 따라서 load에서 닫지 말고 process_exit에서 닫아줌
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
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

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

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
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
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
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
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
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

bool lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct page_info_transmitter *info = (struct page_info_transmitter *)aux;
	if (file_read_at(info->file, page->va, info->read_bytes, info->ofs) != (int)info->read_bytes)
		return false;
	memset(page->va + info->read_bytes, 0, info->zero_bytes);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	// list_push_back(&frame_list, &page->frame->frame_elem);
	return true;
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
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		aux = malloc(sizeof(struct page_info_transmitter));
		if (aux == NULL)
			return false;

		((struct page_info_transmitter *)aux)->file = file;
		((struct page_info_transmitter *)aux)->ofs = ofs;
		((struct page_info_transmitter *)aux)->read_bytes = page_read_bytes;
		((struct page_info_transmitter *)aux)->zero_bytes = page_zero_bytes;
		// printf("\n\n\n*new page* \n");
		// printf("file : %p \n", file);
		// printf("ofs : %d \n", ofs);
		// printf("page_read_bytes : %d \n", page_read_bytes);
		// printf("page_zero_bytes : %d \n", page_zero_bytes);

		if (!vm_alloc_page_with_initializer(VM_ANON, upage,					   // VM_ANON으로 초기화 해야함 -> 실행 파일은 변동 가능한 데이터를 다루기 때문에
											writable, lazy_load_segment, aux)) // VM_FILE은 .txt 파일과 같은 정적 파일들이 대상이다
		{
			free(aux);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1))
	{
		success = vm_claim_page(stack_bottom);
		if (success)
			if_->rsp = USER_STACK;
	}
	return success;
}
#endif /* VM */