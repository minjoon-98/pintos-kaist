/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "userprog/process.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */ /* upage가 이미 할당되어 있는지 확인 */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */

		/* 페이지를 보조 페이지 테이블에 삽입 */
		struct page *page = (struct page *)calloc(sizeof(struct page), 1);
		if (page == NULL)
		{
			free(page);
			goto err;
		}

		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			page_initializer = anon_initializer;
			break;
		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}

		uninit_new(page, upage, init, type, aux, page_initializer);

		page->writable = writable;

		if (!spt_insert_page(spt, page)) // 페이지를 보조 페이지 테이블에 삽입
		{
			free(page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

// 주어진 가상 주소에 해당하는 보조 페이지를 찾는 함수
struct supplemental_page *
spt_find_supplemental_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct supplemental_page sp;
	sp.va = va; // 가상 주소 설정

	struct hash_elem *h_elem = hash_find(&spt->spt_hash, &sp.spt_elem); // 해시 테이블에서 검색

	if (h_elem == NULL)
		return NULL;
	struct supplemental_page *sp_found = hash_entry(h_elem, struct supplemental_page, spt_elem);

	return sp_found; // 발견된 원소 반환
}

/* Find VA from spt and return page. On error, return NULL. */ // 주어진 가상 주소에 해당하는 페이지를 찾는 함수
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function. */
	// printf("체크포인트 find_page 1 : %p\n", va);
	struct supplemental_page *sp_found = spt_find_supplemental_page(spt, va); // 보조 페이지 검색
	if (sp_found == NULL)
		return NULL;

	page = sp_found->page;
	return page; // 페이지 반환
}

/* Insert PAGE into spt with validation. */ // 보조 페이지 테이블에 페이지를 삽입하는 함수
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED)
{
	int succ = false;
	/* TODO: Fill this function. */
	struct supplemental_page *new_sp = (struct supplemental_page *)malloc(sizeof(struct supplemental_page)); // 새로운 보조 페이지 할당
	ASSERT(new_sp != NULL)
	new_sp->page = page;
	new_sp->va = page->va;

	struct hash_elem *old = hash_insert(&spt->spt_hash, &new_sp->spt_elem); // 해시 테이블에 삽입

	if (old != NULL) // 같은 원소가 hash에 이미 있을 때 그 원소를 반환
	{
		printf("can't insert to supplemental page because addr: %p have already been.\n", page->va);
		free(new_sp); // 새로 할당된 보조 페이지 해제
	}
	else // 원소 삽입 성공 시 true 반환
	{
		succ = true;
	}

	return succ;
}

// 보조 페이지 테이블에서 페이지를 제거하는 함수
void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	struct supplemental_page sp;
	sp.page = page;
	sp.va = page->va;
	struct hash_elem *old = hash_delete(&spt->spt_hash, &sp.spt_elem); // 해시 테이블에서 삭제

	if (old != NULL) // 원소 삭제 성공 시 삭제한 원소 반환
	{
		struct supplemental_page *old_sp = hash_entry(old, struct supplemental_page, spt_elem);
		free(old_sp); // 삭제된 보조 페이지 해제
	}
	else
		printf("supplemental_page doesn't exist in table when calling spt_remove_page.\n");

	vm_dealloc_page(page); // 페이지 비할당
}

// 프레임 리스트 초기화 함수
void frame_table_init(void)
{
	list_init(&frame_list); // frame_list 초기화
}

/* 새로운 프레임을 할당하는 함수 */
static struct frame *frame_alloc(void)
{
	struct frame *frame = malloc(sizeof(struct frame)); // 프레임 구조체 할당
	if (frame != NULL)
	{
		frame->kva = palloc_get_page(PAL_USER | PAL_ZERO); // 가상 주소 할당
		if (frame->kva != NULL)
		{
			list_push_back(&frame_list, &frame->frame_elem); // 프레임을 리스트에 추가
			frame->page = NULL;
		}
		else
		{
			free(frame); // 가상 주소 할당 실패 시 프레임 구조체 해제
			frame = NULL;
		}
	}
	return frame;
}

/* 프레임을 해제하는 함수 */
void frame_free(struct frame *frm)
{
	// 프레임을 frame_list에서 제거
	list_remove(&frm->frame_elem);

	// 프레임의 자원을 해제
	palloc_free_page(frm->kva); // 가상 주소 해제 (예: palloc_free_page 등)
	free(frm);					// 프레임 구조체 메모리 해제
}

/* Get the struct frame, that will be evicted. */
// 페이지 교체 대상 프레임 선택 함수
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	if (!list_empty(&frame_list))
	{
		victim = list_entry(list_pop_front(&frame_list), struct frame, frame_elem); // FIFO 알고리즘 사용
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
// 프레임 교체 함수
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim(); // 교체 대상 프레임 선택
	/* TODO: swap out the victim and return the evicted frame. */
	if (victim != NULL)
	{
		if (swap_out(victim->page))
			return victim;
		else
		{
			printf("swapping out failed when evicting frame.\n");
			ASSERT(false)
		}
	}
	else
		printf("can't find victim.\n");
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
// static struct frame *
// vm_get_frame(void)
// {
// 	struct frame *frame = NULL;
// 	/* TODO: Fill this function. */
// 	frame = frame_alloc(); // 새로운 프레임 할당 시도
// 	if (frame == NULL)
// 	{
// 		frame = vm_evict_frame(); // 프레임 할당 실패 시 페이지 교체 시도
// 		if (frame == NULL)
// 		{
// 			PANIC("Unable to allocate or evict a frame.");
// 		}
// 		else
// 		{
// 			frame->kva = palloc_get_page(PAL_ASSERT | PAL_USER | PAL_ZERO);
// 		}
// 	}
// 	ASSERT(frame != NULL);
// 	ASSERT(frame->page == NULL);
// 	return frame;
// }

static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER); // user pool에서 새로운 physical page를 가져온다.

	if (kva == NULL)   // page 할당 실패 -> 나중에 swap_out 처리
		PANIC("todo"); // OS를 중지시키고, 소스 파일명, 라인 번호, 함수명 등의 정보와 함께 사용자 지정 메시지를 출력

	frame = calloc(sizeof(struct frame), sizeof(struct frame)); // 프레임 할당 // callo해줘야함
	frame->kva = kva;											// 프레임 멤버 초기화
	// frame->page = NULL;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

// static struct frame *
// vm_get_frame(void)
// {
// 	struct frame *frame = NULL;
// 	/* TODO: Fill this function. */
// 	frame = (struct frame *)calloc(sizeof(struct frame), 1);
// 	ASSERT(frame != NULL);
// 	void *phys_page = palloc_get_page(PAL_USER | PAL_ZERO);
// 	if (phys_page != NULL)
// 		frame->kva = phys_page;
// 	else
// 	{
// 		if (vm_evict_frame() != NULL)
// 			frame->kva = palloc_get_page(PAL_ASSERT | PAL_USER | PAL_ZERO);
// 		else
// 			PANIC("It failed to evict frame.\n");
// 	}
// 	return frame;
// }

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	/* 잘못된 접근 확인 */
	if (addr == NULL || is_kernel_vaddr(addr))
	{
		return false;
	}
	printf("뭐가 문제야 addr %p \n", addr);
	/* 보조 페이지 테이블에서 페이지 찾기 */
	page = spt_find_page(spt, addr);
	if (page == NULL)
	{
		return false;
	}
	printf("뭐가 문제야 page %p \n", page);

	/* 페이지를 클레임 (프레임 할당 및 데이터 로드) */
	if (not_present && !vm_do_claim_page(page))
	{
		return false;
	}

	/* 읽기 전용 페이지에 대한 쓰기 접근 시도 확인 */
	if (write)
	{
		return false;
	}

	return true;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */

	// 현재 스레드의 보조 페이지 테이블에서 페이지를 찾음
	page = spt_find_page(&thread_current()->spt, va);

	// 페이지가 존재하지 않으면 false 반환
	if (page == NULL)
	{
		return false;
	}

	// 페이지를 클레임
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame(); // 프레임 할당
	if (frame == NULL)
	{
		return false;
	}

	/* Set links */ // 프레임과 페이지를 연결
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 페이지 테이블 엔트리에 페이지의 가상 주소(VA)를 프레임의 물리 주소(PA)로 매핑
	bool success = pml4_set_page(thread_current()->pml4, page->va, frame->kva, true);
	if (!success)
	{
		// 매핑 실패 시 프레임 해제
		frame_free(frame);
		return false;
	}

	// 페이지를 스왑 인
	return swap_in(page, frame->kva);
}

// 가상 주소를 해시 값으로 변환하는 함수
static uint64_t va_hash_func(const struct hash_elem *e, void *aux)
{
	/* hash_entry()로 element에 대한 supplemental_page 구조체 검색 */
	/* hash_int()를 이용해서 supplemental_page의 멤버 vaddr에 대한 해시값을 구하고 반환 */
	struct supplemental_page *sp;
	sp = (struct supplemental_page *)hash_entry(e, struct supplemental_page, spt_elem);
	// return sp->va;
	return hash_bytes(&sp->va, sizeof(sp->va)); // va의 값을 바이트로 변환하여 해시 값 계산
}

// 두 가상 주소를 비교하는 함수
static bool va_less_func(const struct hash_elem *a,
						 const struct hash_elem *b, void *aux)
{
	/* hash_entry()로 각각의 element에 대한 supplemental_page 구조체를 얻은 후 vaddr 비교 (b가 크다면 true, a가 크다면 false) */
	struct supplemental_page *sp_a = (struct supplemental_page *)hash_entry(a, struct supplemental_page, spt_elem);
	struct supplemental_page *sp_b = (struct supplemental_page *)hash_entry(b, struct supplemental_page, spt_elem);
	return sp_a->va < sp_b->va;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	/* hash_init()으로 해시테이블 초기화 */
	/* 인자로 해시 테이블과 va_hash_func과 va_less_func 사용 */
	hash_init(&spt->spt_hash, va_hash_func, va_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	// src의 모든 supplemental_page를 dst에 복사
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	while (hash_next(&i))
	{
		struct supplemental_page *sp = hash_entry(hash_cur(&i), struct supplemental_page, spt_elem);

		// 새로운 supplemental_page를 할당하고 초기화
		struct supplemental_page *new_sp = malloc(sizeof(struct supplemental_page));
		if (new_sp == NULL)
			return false; // 메모리 할당 실패 시 false 반환

		new_sp->page = sp->page; // 페이지 복사 (필요시 페이지 내용도 복사해야 함)
		new_sp->va = sp->va;

		// 새로운 supplemental_page를 dst 해시 테이블에 삽입
		if (hash_insert(&dst->spt_hash, &new_sp->spt_elem) != NULL)
		{
			free(new_sp);
			return false; // 삽입 실패 시 false 반환
		}
	}
	return true; // 성공 시 true 반환
}

// 해시 테이블의 원소를 삭제하기 위한 해시 제거자 함수
void hash_destructor(struct hash_elem *e, void *aux)
{
	struct supplemental_page *sp = hash_entry(e, struct supplemental_page, spt_elem);
	// 페이지 자원 해제
	vm_dealloc_page(sp->page);
	// supplemental_page 자원 해제
	free(sp);
}

/* Free the resource hold by the supplemental page table */ /* 보조 페이지 테이블이 보유한 자원을 해제 */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	if (hash_empty(&spt->spt_hash))
		return;

	/* 해시 테이블의 모든 supplemental_page를 제거하고 자원을 해제 */
	hash_destroy(&spt->spt_hash, hash_destructor);
}
