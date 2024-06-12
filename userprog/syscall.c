#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "lib/stdio.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "vm/vm.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

void check_address(void *addr);
void get_argument(void *rsp, int *argv, int argc);
// int add_file_descriptor(struct file *f);
// struct file *get_file_from_fdt(int fd);
// void remove_file_from_fdt(int fd);

/* Projects 2 and later. */

void halt(void);
void exit(int status);
pid_t fork(const char *thread_name, struct intr_frame *f UNUSED);
int exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

/* project 3*/
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK, FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock); // to avoid race condition protect filesystem
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	// printf("system call!\n");

	// hex_dump(f->rsp, f->rsp, USER_STACK, true); // 무슨 인자가 넘어오는지 궁금

	/* from lib/user/syscall.c */
	/*
		인자 들어오는 순서:
		1번째 인자: %rdi
		2번째 인자: %rsi
		3번째 인자: %rdx
		4번째 인자: %r10
		5번째 인자: %r8
		6번째 인자: %r9
	*/

	// Getting the system call number from the interrupt frame /* %rax 는 시스템 콜 번호 */
	int syscall_number = f->R.rax;

	// /* project 3 stack growth */
	// struct thread *t = thread_current();
	// t->_rsp = f->rsp;

	switch (syscall_number)
	{
	case SYS_HALT: /* Halt the operating system. */
		halt();
	case SYS_EXIT: /* Terminate this process. */
		exit((int)f->R.rdi);
		break;
	case SYS_FORK: /* Clone current process. */
		f->R.rax = fork((const char *)f->R.rdi, f);
		break;
	case SYS_EXEC: /* Switch current process. */
		f->R.rax = exec((const char *)f->R.rdi);
		break;
	case SYS_WAIT: /* Wait for a child process to die. */
		f->R.rax = wait((pid_t)f->R.rdi);
		break;
	case SYS_CREATE: /* Create a file with the given name and initial size. */
		f->R.rax = create((const char *)f->R.rdi, (unsigned)f->R.rsi);
		break;
	case SYS_REMOVE: /* Remove the file with the given name. */
		f->R.rax = remove((const char *)f->R.rdi);
		break;
	case SYS_OPEN: /* Open the file with the given name. */
		f->R.rax = open((const char *)f->R.rdi);
		break;
	case SYS_FILESIZE: /* Get the size of the open file. */
		f->R.rax = filesize((int)f->R.rdi);
		break;
	case SYS_READ: /* Read from an open file. */
		f->R.rax = read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
		break;
	case SYS_WRITE: /* Write to an open file. */
		f->R.rax = write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned)f->R.rdx);
		break;
	case SYS_SEEK: /* Change the next byte to be read or written in an open file. */
		seek((int)f->R.rdi, (unsigned)f->R.rsi);
		break;
	case SYS_TELL: /* Get the position of the next byte to be read or written in an open file. */
		f->R.rax = tell((int)f->R.rdi);
		break;
	case SYS_CLOSE: /* Close an open file. */
		close((int)f->R.rdi);
		break;
	case SYS_MMAP:
		lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		f->R.rax = mmap((void *)f->R.rdi, (size_t)f->R.rsi, (int)f->R.rdx, (struct file *)f->R.r10, (off_t)f->R.r8);
		lock_release(&filesys_lock); // ADD: filesys_lock at file system
		break;
	case SYS_MUNMAP:
		lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		munmap((void *)f->R.rdi);
		lock_release(&filesys_lock); // ADD: filesys_lock at file system
		break;
	// case SYS_DUP2: /* 구현 실패... */
	// 	dup2((int)f->R.rdi, (int)f->R.rsi);
	// 	break;
	default:
		// 지원되지 않는 시스템 콜 처리
		printf("Unknown system call: %d\n", syscall_number);
		thread_exit();
	}
}

/* Check if the address is in user space */
void check_address(void *addr)
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	if (addr == NULL || !is_user_vaddr(addr))
	{
		// 잘못된 접근일 경우 프로세스 종료
		exit(-1);
	}
}

void check_buffer(uint64_t *buffer)
{
	struct page *p = spt_find_page(&thread_current()->spt, pg_round_down(buffer));
	if (!p) // spt 테이블에 해당 버퍼에 대한 페이지가 없으면 프로세스 종료
		exit(-1);
	// if (!p->writable && !p->origin_writable)
	if (!p->writable && !p->copy_on_write) // 읽기 권한이 없는데, copy on write가 아닐 경우 종료
		exit(-1);
}

bool check_address_overlap(void *addr, size_t length)
{
	struct thread *t = thread_current();
	void *end_addr = addr + length;

	// 주소 범위를 페이지 단위로 확인
	for (void *page_addr = addr; page_addr < end_addr; page_addr += PGSIZE)
	{
		if (spt_find_page(&t->spt, page_addr))
		{
			// 페이지가 이미 매핑되어 있음
			return true;
		}
	}
	return false;
}

/* Retrieve arguments from the user stack and store them in argv */
void get_argument(void *rsp, int *argv, int argc)
{
	for (int i = 0; i < argc; i++)
	{
		void *arg_ptr = (void *)((uint8_t *)rsp + i * sizeof(uint64_t));
		check_address(arg_ptr);
		argv[i] = *(int *)arg_ptr;
	}
}

// 파일 객체에 대한 파일 디스크립터를 생성하는 함수
int add_file_to_fdt(struct file *f)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	// limit을 넘지 않는 범위 안에서 빈 자리 탐색
	while (curr->next_fd < MAX_FILES && fdt[curr->next_fd])
	{
		curr->next_fd++;
	}
	if (curr->next_fd >= MAX_FILES)
	{
		return -1;
	}
	fdt[curr->next_fd] = f;

	return curr->next_fd;
}

// 파일 객체를 검색하는 함수
struct file *get_file_from_fdt(int fd)
{
	if (fd < 0 || MAX_FILES <= fd)
		return NULL;

	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	return fdt[fd]; /* 파일 디스크립터에 해당하는 파일 객체를 리턴 */
}

// 파일 디스크립터 테이블에서 파일 객체를 제거하는 함수
void remove_file_from_fdt(int fd)
{
	if (fd < 0 || MAX_FILES <= fd)
		return NULL;

	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;
	fdt[fd] = NULL;
}

/**
 * This function calls power_off() to shut down Pintos.
 * It should be used sparingly, as it might result in losing important information
 * such as deadlock situations.
 */

/**
 * @brief Halts the operating system.
 */
void halt(void)
{
	// power_off()를 호출해서 Pintos를 종료
	power_off(); // 이 함수는 웬만하면 사용되지 않아야 합니다. deadlock 상황에 대한 정보 등등 뭔가 조금 잃어 버릴지도 모릅니다.
}

/**
 * This function retrieves the currently running thread's structure,
 * prints the process termination message, and terminates the thread.
 */

/**
 * @brief Exits the current process.
 *
 * @param status The exit status of the process.
 */
void exit(int status)
{
	/* 실행중인 스레드 구조체를 가져옴 */
	/* 프로세스 종료 메시지 출력,
	출력 양식: “프로세스이름: exit(종료상태)” */
	/* 스레드 종료 */
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status); // Process Termination Message /* 정상적으로 종료됐다면 status는 0 */
	thread_exit();
}

pid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

/**
 * @brief Executes a new process by running the command line specified.
 * 지정된 명령어 줄을 실행하여 새로운 프로세스를 실행합니다.
 *
 * This function creates a new process by running the command line given in
 * the `cmd_line` argument. It first checks the validity of the given address,
 * then copies the command line to a new page of memory, and finally executes
 * the new process.
 * 이 함수는 `cmd_line` 인수로 주어진 명령어 줄을 실행하여 새로운 프로세스를 생성합니다.
 * 먼저 주어진 주소의 유효성을 확인한 다음, 명령어 줄을 새로운 메모리 페이지에 복사하고,
 * 마지막으로 새로운 프로세스를 실행합니다.
 *
 * @param cmd_line A pointer to the command line string that specifies the new
 * process to be executed.
 * 실행할 새로운 프로세스를 지정하는 명령어 줄 문자열에 대한 포인터
 *
 * @return This function does not return a value. If execution fails, it calls
 * exit with a status of -1.
 * 이 함수는 값을 반환하지 않습니다. 실행에 실패하면 상태 -1로 exit를 호출합니다.
 */

int exec(const char *cmd_line)
{
	// 주어진 명령어 줄 주소의 유효성을 확인합니다.
	check_address(cmd_line);

	// 명령어 줄 복사를 위한 새로운 메모리 페이지를 할당합니다.
	char *cl_copy;
	cl_copy = palloc_get_page(PAL_ZERO); // page를 할당받고 해당 page에 file_name을 저장해줌
	if (cl_copy == NULL)
	{
		// 메모리 할당에 실패하면 상태 -1로 프로세스를 종료합니다.
		palloc_free_page(cl_copy);
		// exit(-1);
		return TID_ERROR;
	}

	// 명령어 줄을 새로 할당한 메모리 페이지에 복사합니다.
	strlcpy(cl_copy, cmd_line, PGSIZE);
	// 복사된 명령어 줄을 사용하여 새로운 프로세스를 실행합니다.
	// 실행에 실패하면 상태 -1로 프로세스를 종료합니다.
	if (process_exec(cl_copy) < 0)
	{
		exit(-1);
	}
}

int wait(int pid)
{
	return process_wait(pid);
}

/**
 * This function creates a new file with the specified name and initial size.
 * It checks the validity of the file name address before creating the file.
 */

/**
 * @brief Creates a new file.
 *
 * @param file The name of the file to create.
 * @param initial_size The initial size of the file.
 * @return True if the file was successfully created, false otherwise.
 */
bool create(const char *file, unsigned initial_size)
{
	/* 파일 이름과 크기에 해당하는 파일 생성 */
	/* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */
	check_address((void *)file);

	lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
	bool succ = filesys_create(file, initial_size);
	lock_release(&filesys_lock); // ADD: filesys_lock at file system

	return succ;
}

/**
 * This function removes the file with the specified name.
 * It checks the validity of the file name address before removing the file.
 */

/**
 * @brief Removes a file.
 *
 * @param file The name of the file to remove.
 * @return True if the file was successfully removed, false otherwise.
 */
bool remove(const char *file)
{
	/* 파일 이름에 해당하는 파일을 제거 */
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	check_address((void *)file);

	lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
	bool succ = filesys_remove(file);
	lock_release(&filesys_lock); // ADD: filesys_lock at file system

	return succ;
}

/**
 * @brief Opens a file and returns a file descriptor.
 *
 * @param file The name of the file to open.
 * @return The file descriptor if successful, -1 otherwise.
 */
int open(const char *file)
{
	check_address(file); // 주어진 파일 이름 주소가 유효한지 확인합니다.

	lock_acquire(&filesys_lock);		 // ADD: filesys_lock at file system
	struct file *f = filesys_open(file); // 파일 시스템에서 파일을 엽니다.
	lock_release(&filesys_lock);		 // ADD: filesys_lock at file system
	if (!f)
	{
		return -1; // 파일을 열 수 없는 경우 -1을 반환합니다.
	}
	// int fd = thread_current()->next_fd++; // 다음 파일 디스크립터를 가져오고 증가시킵니다.
	// thread_current()->fd_table[fd] = f;	  // 파일 디스크립터 테이블에 파일 포인터를 저장합니다.
	// return fd;							  // 파일 디스크립터를 반환합니다.
	int fd = add_file_to_fdt(f);
	if (fd == -1)
		file_close(f);
	return fd;
}

/**
 * @brief Returns the size of the file in bytes.
 *
 * @param fd The file descriptor of the file.
 * @return The size of the file in bytes if successful, -1 otherwise.
 */
int filesize(int fd)
{
	// struct file *f = thread_current()->fd_table[fd]; // 파일 디스크립터 테이블에서 파일 포인터를 가져옵니다.
	struct file *f = get_file_from_fdt(fd);

	if (!f)
		return -1; // 파일이 열려 있지 않은 경우 -1을 반환합니다.

	lock_acquire(&filesys_lock);   // ADD: filesys_lock at file system
	off_t length = file_length(f); // 파일의 길이를 반환합니다.
	lock_release(&filesys_lock);   // ADD: filesys_lock at file system

	return length;
}

/**
 * @brief Reads data from a file into a buffer.
 *
 * @param fd The file descriptor of the file.
 * @param buffer The buffer to store the data.
 * @param size The number of bytes to read.
 * @return The number of bytes read if successful, -1 otherwise.
 */
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer); // 주어진 버퍼 주소가 유효한지 확인합니다.
	check_buffer(buffer);

	off_t read_byte;
	uint8_t *read_buffer = buffer;
	if (fd == STDIN_FILENO)
	{
		char key;
		for (read_byte = 0; read_byte < size; read_byte++)
		{
			key = input_getc();
			*read_buffer++ = key;
			if (key == '\0')
				break;
		}
	}
	else if (fd == STDOUT_FILENO)
	{
		return -1;
	}
	// struct file *f = thread_current()->fd_table[fd]; // 파일 디스크립터 테이블에서 파일 포인터를 가져옵니다.
	else
	{
		struct file *f = get_file_from_fdt(fd);
		if (!f)
			return -1;				 // 파일이 열려 있지 않은 경우 -1을 반환합니다.
		lock_acquire(&filesys_lock); // file을 읽을 때 다른 프로세스의 접근을 막기 위해 lock
		read_byte = file_read(f, buffer, size);
		lock_release(&filesys_lock);
	}

	uint8_t *dirty_buffer = buffer;
	off_t dirty_byte = read_byte;
	while (0 < dirty_byte)
	{
		pml4_set_dirty(thread_current()->pml4, dirty_buffer, true);
		dirty_buffer += PGSIZE;
		dirty_byte -= PGSIZE;
	}

	return read_byte; // 파일에서 데이터를 읽고, 읽은 바이트 수를 반환합니다.
}

/**
 * @brief Writes data from a buffer to a file.
 *
 * @param fd The file descriptor of the file.
 * @param buffer The buffer containing the data.
 * @param size The number of bytes to write.
 * @return The number of bytes written if successful, -1 otherwise.
 */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer); // 주어진 버퍼 주소가 유효한지 확인합니다.

	off_t write_byte;
	if (fd == STDIN_FILENO)
		return -1;
	else if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, size); // 표준 출력에 데이터를 씁니다.
		return size;		  // 쓴 바이트 수를 반환합니다.
	}
	else
	{
		// struct file *f = thread_current()->fd_table[fd]; // 파일 디스크립터 테이블에서 파일 포인터를 가져옵니다.
		struct file *f = get_file_from_fdt(fd);
		if (!f)
		{
			return -1; // 파일이 열려 있지 않은 경우 -1을 반환합니다.
		}
		lock_acquire(&filesys_lock); // file을 읽을 때 다른 프로세스의 접근을 막기 위해 lock
		write_byte = file_write(f, buffer, size);
		lock_release(&filesys_lock);
	}
	return write_byte; // 파일에 데이터를 쓰고, 쓴 바이트 수를 반환합니다.
}

/**
 * @brief Sets the file position to a given value.
 *
 * @param fd The file descriptor of the file.
 * @param position The new position in the file.
 */
void seek(int fd, unsigned position)
{
	// struct file *f = thread_current()->fd_table[fd]; // 파일 디스크립터 테이블에서 파일 포인터를 가져옵니다.
	struct file *f = get_file_from_fdt(fd);
	if (f)
	{
		// if (f <= 2) // 0,1,2는 이미 정의되어 있음
		// {
		// 	return;
		// }
		lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		file_seek(f, position);		 // 파일의 위치를 지정한 위치로 이동합니다.
		lock_release(&filesys_lock); // ADD: filesys_lock at file system
	}
}

/**
 * @brief Returns the current position in the file.
 *
 * @param fd The file descriptor of the file.
 * @return The current position in the file if successful, -1 otherwise.
 */
unsigned tell(int fd)
{
	// struct file *f = thread_current()->fd_table[fd]; // 파일 디스크립터 테이블에서 파일 포인터를 가져옵니다.
	struct file *f = get_file_from_fdt(fd);
	if (f)
	{
		// if (f <= 2) // 0,1,2는 이미 정의되어 있음
		// {
		// 	return;
		// }
		lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		off_t loc = file_tell(f);	 // 파일의 현재 위치를 반환합니다.
		lock_release(&filesys_lock); // ADD: filesys_lock at file system
		return loc;
	}
	return -1; // 파일이 열려 있지 않은 경우 -1을 반환합니다.
}

/**
 * @brief Closes the file.
 *
 * @param fd The file descriptor of the file.
 */
void close(int fd)
{
	if (fd < 2 || MAX_FILES <= fd) // for write-bad-fd
	{
		return; /* Ignore stdin and stdout. */
	}
	// struct file *f = thread_current()->fd_table[fd]; // 파일 디스크립터 테이블에서 파일 포인터를 가져옵니다.
	struct file *f = get_file_from_fdt(fd);
	if (f)
	{
		lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
		file_close(f);				 // 파일을 닫습니다.
		lock_release(&filesys_lock); // ADD: filesys_lock at file system
		// thread_current()->fd_table[fd] = NULL; // 파일 디스크립터 테이블에서 파일 포인터를 제거합니다.
		remove_file_from_fdt(fd);
	}
}

/**
 *
 * @brief Maps a file into memory.
 *
 * Maps length bytes of the file open as fd starting from offset byte into
 * the process's virtual address space at addr. If successful, this function
 * returns the virtual address where the file is mapped. On failure, it returns NULL.
 *
 * @param addr The starting address for the new mapping.
 * @param length The length of the mapping.
 * @param writable Whether the mapping is writable.
 * @param fd The file descriptor of the file.
 * @param offset The offset in the file where the mapping starts.
 *
 * @return The virtual address where the file is mapped if successful, NULL otherwise.
 */
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
	// 유효성 검사: NULL 주소, 커널 주소, 페이지 정렬되지 않은 주소, 잘못된 길이, 잘못된 오프셋
	if (addr == NULL ||
		is_kernel_vaddr(addr) ||
		(pg_round_down(addr) != addr) ||
		length >= KERN_BASE ||
		(pg_round_down(offset) != offset))
		return NULL;

	// 콘솔 입출력 파일 디스크립터 확인
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
		return NULL;

	// 이미 매핑된 주소 범위인지 확인
	if (check_address_overlap(addr, length))
		return NULL;

	// 파일 디스크립터로부터 파일 객체 가져오기
	struct file *f = get_file_from_fdt(fd);
	if (length == 0 || f == NULL || file_length(f) == 0)
		exit(-1);

	// 파일 재개방
	struct file *reopen_file = file_reopen(f);
	// lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
	void *result = do_mmap(addr, length, writable, reopen_file, offset);
	// lock_release(&filesys_lock); // ADD: filesys_lock at file system
	// do_mmap 함수를 호출하여 메모리 매핑 수행
	return result;
}

/**
 * @brief Unmaps a memory-mapped file.
 *
 * Unmaps the mapping for the specified address range addr, which must be
 * the virtual address returned by a previous call to mmap by the same process
 * that has not yet been unmapped.
 *
 * @param addr The starting address of the mapping to unmap.
 */
void munmap(void *addr)
{
	// 페이지 테이블에서 해당 주소에 대한 페이지를 찾음
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if (!page || page->operations->type != VM_FILE)
		return;
	// lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
	// do_munmap 함수를 호출하여 매핑 해제 수행
	do_munmap(addr);
	// lock_release(&filesys_lock); // ADD: filesys_lock at file system
}

// /**
//  * @brief Duplicates an existing file descriptor to a new file descriptor.
//  *
//  * This function duplicates the file descriptor specified by oldfd to the file descriptor specified by newfd.
//  *
//  * @param oldfd The file descriptor to duplicate.
//  * @param newfd The file descriptor to duplicate to.
//  *
//  * @return If successful, returns the new file descriptor.
//  *         If oldfd is invalid or if an error occurs, returns -1.
//  */
// int dup2(int oldfd, int newfd)
// {
// 	struct file *old_file = find_file_by_fd(oldfd);
// 	struct file *new_file = find_file_by_fd(newfd);

// 	if (old_file == NULL)
// 		return -1;
// 	if (old_file == new_file)
// 		return newfd;

// 	if (old_file > 2)
// 	{
// 		close(newfd);
// 		lock_acquire(&filesys_lock);
// 		thread_current()->fd_table[newfd] = file_duplicate(old_file);
// 		lock_release(&filesys_lock);
// 	}
// 	else
// 	{
// 		close(newfd);
// 		thread_current()->fd_table[newfd] = old_file;
// 	}
// 	return newfd;
// }