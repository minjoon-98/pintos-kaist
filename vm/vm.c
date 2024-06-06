/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"

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

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc(sizeof(struct page));
		if (page == NULL)
			goto err;
		// printf("page type = %d", type);
		// printf("logical addr = %p\n", upage);
		// unchecked : uninit_new 의 마지막인자로 완전한 initializer의 호출을 넣어줘야하는지 몰?루
		if (VM_TYPE(type) == VM_ANON)
			uninit_new(page, upage, init, type, aux, anon_initializer);
		if (VM_TYPE(type) == VM_FILE)
			uninit_new(page, upage, init, type, aux, file_backed_initializer);
		page->writable = writable;
		/* TODO: Insert the page into the spt. */
		if (spt_insert_page(spt, page) == false)
		{
			free(page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt, void *va)
{
	struct page *page;
	struct hash_elem *found_elem;
	page = (struct page *)malloc(sizeof(struct page));
	page->va = va;

	found_elem = hash_find(&spt->spt_hash, &page->hash_elem);
	if (found_elem != NULL)
	{
		return hash_entry(found_elem, struct page, hash_elem);
	}
	free(page);
	return NULL;
}
/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	int succ = hash_insert(&spt->spt_hash, &page->hash_elem) == NULL;
	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
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
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	// unchecked : zero 초기화 해야하는지 몰?루
	void *addr = palloc_get_page(PAL_USER || PAL_ZERO);
	if (addr == NULL)
		PANIC("todo");

	frame = calloc(sizeof(struct frame), 1);

	frame->kva = addr;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

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

	void *page_addr = pg_round_down(addr);

	if (not_present) // 접근한 메모리의 physical page가 존재하지 않은 경우
	{
		page = spt_find_page(spt, page_addr);
		if (page == NULL)
			return false;
		if (write == 1 && page->writable == 0) // write 불가능한 페이지에 write 요청한 경우
			return false;
		return vm_do_claim_page(page);
	}
	return false;
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
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame(); // 프레임 할당
	if (frame == NULL)
		return false;

	/* Set links */ // 프레임과 페이지를 연결
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 페이지 테이블 엔트리에 페이지의 가상 주소(VA)를 프레임의 물리 주소(PA)로 매핑
	struct thread *cur_t = thread_current();
	bool success = pml4_set_page(cur_t->pml4, page->va, frame->kva, page->writable);
	if (!success)
	{
		frame_free(frame); // 매핑 실패 시 프레임 해제
		return false;
	}

	return swap_in(page, frame->kva); // 페이지를 스왑 인
}

// 가상 주소를 해시 값으로 변환하는 함수
uint64_t spt_hash_func(const struct hash_elem *e, void *aux)
{
	/* hash_entry()로 element에 대한 supplemental_page 구조체 검색 */
	/* hash_int()를 이용해서 supplemental_page의 멤버 vaddr에 대한 해시값을 구하고 반환 */
	struct page *page = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&page->va, sizeof page->va); // va의 값을 바이트로 변환하여 해시 값 계산
}

// 두 가상 주소를 비교하는 함수
bool page_table_entry_less_function(struct hash_elem *a, struct hash_elem *b, void *aux UNUSED)
{
	/* hash_entry()로 각각의 element에 대한 supplemental_page 구조체를 얻은 후 vaddr 비교 (b가 크다면 true, a가 크다면 false) */
	struct page *page_a = hash_entry(a, struct page, hash_elem);
	struct page *page_b = hash_entry(b, struct page, hash_elem);
	return page_a->va < page_b->va;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	/* hash_init()으로 해시테이블 초기화 */
	/* 인자로 해시 테이블과 va_hash_func과 va_less_func 사용 */
	hash_init(&spt->spt_hash, spt_hash_func, page_table_entry_less_function, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	while (hash_next(&i))
	{
		// src_page 정보
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;

		/* type이 uninit이면 */
		if (type == VM_UNINIT)
		{ // uninit page 생성 & 초기화
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			vm_alloc_page_with_initializer(page_get_type(src_page), upage, writable, init, aux);
			continue;
		}

		/* type이 uninit이 아니면 */
		if (!vm_alloc_page(type, upage, writable)) // uninit page 생성 & 초기화
			// init이랑 aux는 Lazy Loading에 필요함
			// 지금 만드는 페이지는 기다리지 않고 바로 내용을 넣어줄 것이므로 필요 없음
			return false;

		// vm_claim_page으로 요청해서 매핑 & 페이지 타입에 맞게 초기화
		if (!vm_claim_page(upage))
			return false;

		// 매핑된 프레임에 내용 로딩
		struct page *dst_page = spt_find_page(dst, upage);
		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}
	return true;
}

// 해시 테이블의 원소를 삭제하기 위한 해시 제거자 함수
void hash_destructor(struct hash_elem *e, void *aux UNUSED)
{
	struct page *p = hash_entry(e, struct page, hash_elem);

	vm_dealloc_page(p);
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	if (hash_empty(&spt->spt_hash))
		return;

	// 해시 테이블을 비웁니다.

	/* 1트 실패... */
	hash_clear(&spt->spt_hash, hash_destructor); // 해시 테이블의 모든 요소를 제거

	/* 2트 실패... */
	/**
	 * 여기서는 hash table은 두고 안의 요소들만 지워줘야 한다.
	 * 	hash_destroy 함수를 사용하면 hash가 사용하던 메모리(hash->bucket) 자체도 반환하므로, hash_destroy가 아닌 hash_clear를 사용해야 한다.
	 * 	Why? process가 실행될 때 hash table을 생성한 이후에 process_cleanup()이 호출되는데, 이때는 hash table은 남겨두고 안의 요소들만 제거되어야 한다.
	 *  hash table까지 지워버리면 만들자마자 지워버리는 게 된다.. process가 실행될 때 빈 hash table이 있어야 하므로 hash table은 남겨두고 안의 요소들만 지워야 하는 것이다!
	 */
	hash_destroy(&spt->spt_hash, hash_destructor);

	/* 3트 실패... */
	struct hash_iterator i;
	hash_first(&i, &spt->spt_hash);
	while (hash_next(&i))
	{
		hash_delete(&spt->spt_hash, hash_cur(&i));
	}
}