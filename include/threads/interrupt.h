#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level
{
	INTR_OFF, /* Interrupts disabled. */
	INTR_ON	  /* Interrupts enabled. */
};

enum intr_level intr_get_level(void);
enum intr_level intr_set_level(enum intr_level);
enum intr_level intr_enable(void);
enum intr_level intr_disable(void);

struct gp_registers
{
	uint64_t r15; // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r14; // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r13; // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r12; // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r11; // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r10; // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r9;  // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t r8;  // 일반 목적으로 사용되는 64비트 레지스터
	uint64_t rsi; // 매개변수 전달에 사용되는 64비트 레지스터 (여기서는 RDI를 저장)
	uint64_t rdi; // 매개변수 전달에 사용되는 64비트 레지스터 (여기서는 RSI를 저장)
	uint64_t rbp; // 프레임 포인터로 사용되는 64비트 레지스터
	uint64_t rdx; // 매개변수 전달 및 입출력에 사용되는 64비트 레지스터
	uint64_t rcx; // 루프 카운터 및 매개변수 전달에 사용되는 64비트 레지스터
	uint64_t rbx; // 베이스 레지스터로 사용되는 64비트 레지스터
	uint64_t rax; // 반환 값 및 연산에 사용되는 64비트 레지스터
} __attribute__((packed));

/* Interrupt stack frame. */
struct intr_frame
{
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R; // 인터럽트 발생 시 저장된 일반 레지스터들 /* 스레드가 실행 중에 이용한 CPU의 범용 레지스터 값들 */

	uint16_t es;	 // 세그먼트 레지스터 ES
	uint16_t __pad1; // 정렬을 위한 패딩
	uint32_t __pad2; // 정렬을 위한 패딩
	uint16_t ds;	 // 세그먼트 레지스터 DS  /* 세그먼트 관리 */
	uint16_t __pad3; // 정렬을 위한 패딩
	uint32_t __pad4; // 정렬을 위한 패딩

	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* Interrupt vector number. */ // 인터럽트 벡터 번호 (어떤 인터럽트가 발생했는지를 나타냄)
													/* Sometimes pushed by the CPU,
													   otherwise for consistency pushed as 0 by intrNN_stub.
													   The CPU puts it just under `eip', but we move it here. */
	uint64_t error_code;							// 에러 코드 (특정 인터럽트에서 발생한 에러를 나타냄)

	/* Pushed by the CPU.
	   These are the interrupted task's saved registers. */
	uintptr_t rip;	 // 다음에 실행할 명령어의 주소 (명령어 포인터) /* CPU의 레지스터 정보 */
	uint16_t cs;	 // rip에 대한 코드 세그먼트 /* 세그먼트 관리 */
	uint16_t __pad5; // 정렬을 위한 패딩
	uint32_t __pad6; // 정렬을 위한 패딩
	uint64_t eflags; // CPU 플래그 레지스터 /* CPU 상태를 나타내는 정보 */
	uintptr_t rsp;	 // 저장된 스택 포인터 /* 스택 포인터, 스택의 어느 부분을 사용하고 있었는지에 대해 저장 */
	uint16_t ss;	 // rsp에 대한 스택 세그먼트
	uint16_t __pad7; // 정렬을 위한 패딩
	uint32_t __pad8; // 정렬을 위한 패딩
} __attribute__((packed));

typedef void intr_handler_func(struct intr_frame *);

void intr_init(void);
void intr_register_ext(uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int(uint8_t vec, int dpl, enum intr_level,
					   intr_handler_func *, const char *name);
bool intr_context(void);
void intr_yield_on_return(void);

void intr_dump_frame(const struct intr_frame *);
const char *intr_name(uint8_t vec);

#endif /* threads/interrupt.h */
