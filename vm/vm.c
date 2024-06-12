/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"
#include "kernel/hash.h"
#include "string.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/pte.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/inspect.h"

static const int STACK_LIMIT = (1 << 20);  // 1MB

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
#endif
  register_inspect_intr();
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
  list_init(&frame_list);  // frame_list 초기화
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
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
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {

  ASSERT(VM_TYPE(type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) goto err;
    // printf("page type = %d", type);
    // printf("logical addr = %p\n", upage);
    if (VM_TYPE(type) == VM_ANON)
      uninit_new(page, upage, init, type, aux, anon_initializer);
    if (VM_TYPE(type) == VM_FILE)
      uninit_new(page, upage, init, type, aux, file_backed_initializer);
    page->writable = writable;
    page->is_loaded = false;
    /* TODO: Insert the page into the spt. */
    if (spt_insert_page(spt, page) == false) {
      free(page);
      // uncheckec : err발생시 aux 처리에 대한 검증 필요.
      goto err;
    }
    // printf("allocated vm address is : %p\n", spt_find_page(spt, upage)->va);
    return true;
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
  // malloc으로 동적 할당 시, 해당 시점에 메모리를 할당하기 때문에 메모리가 꽉 차있을 때, 할당 실패 가능성이 있음. by santaiscoming
  // 반대로 정적 할당은 프로그램 실행 전에 stack에 공간을 미리 할당받고 시작하기에 문제를 일으키지 않음 (but 우리가 사용하는 메모리의 영역이 적어짐)
  struct page page;
  struct hash_elem *found_elem;
  page.va = pg_round_down(va);

  found_elem = hash_find(&spt->spt_hash, &page.hash_elem);
  if (found_elem != NULL) {
    return hash_entry(found_elem, struct page, hash_elem);
  }

  return NULL;
}
/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
                     struct page *page UNUSED) {
  int succ = hash_insert(&spt->spt_hash, &page->hash_elem) == NULL;
  return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  // list_remove(&page->frame->frame_elem);
  hash_delete(&spt->spt_hash, &page->hash_elem);
  // palloc_free_page(page);
  // vm_dealloc_page(page);
  return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  if (!list_empty(&frame_list)) {
    victim = list_entry(list_pop_front(&frame_list), struct frame,
                        frame_elem);  // FIFO 알고리즘 사용
  }
  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  struct frame *victim UNUSED = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */

  if (victim == NULL) return NULL;

  /* TODO: swap out the victim and return the evicted frame. */
  swap_out(victim->page);
  // victim->page = NULL;

  // printf("victim : %p, victim kva : %d, victim page : %p\n", victim, victim->kva, victim->page);

  // 프레임을 frame_list에서 제거
  // list_remove(&victim->frame_elem);

  return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */

  void *kva = palloc_get_page(
      PAL_USER);  // user pool에서 새로운 physical page를 가져온다.
  if (kva == NULL) {
    /* 스왑을 구현할 수 있는 경우를 여기에 추가 */
    struct frame *victim = vm_evict_frame();
    // victim->page = NULL;
    return victim;
  }

  frame = calloc(sizeof(struct frame), 1);  // 프레임 할당
  frame->kva = kva;
  // list_push_back(&frame_list, &frame->frame_elem); // 프레임을 리스트에 추가 // do_claim으로 옮김

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

static void vm_stack_growth(void *addr UNUSED) {
  bool succ = true;
  struct supplement_page_table *spt = &thread_current()->spt;
  struct page *page = NULL;
  void *page_addr = pg_round_down(addr);

  // 페이지를 찾을 때까지 루프를 돌며 스택을 확장
  while (spt_find_page(spt, page_addr) == NULL) {
    succ = vm_alloc_page(VM_ANON | VM_MARKER_0, page_addr,
                         true);      // 새로운 페이지 할당
    if (!succ) PANIC("BAAAAAM !!");  // 할당 실패 시 패닉
    page_addr += PGSIZE;             // 다음 페이지 주소로 이동
    if (addr >= page_addr) break;  // 필요한 만큼 페이지를 할당했으면 루프 종료
  }
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/**
 * @brief page fault시에 handling을 시도한다.
 *
 * @param f interrupt frame
 * @param addr fault address
 * @param user bool ? user로부터 접근 : kernel로부터 접근
 * @param write bool ? 쓰기 권한으로 접근 : 읽기 권한으로 접근
 * @param not_present bool ? not-present(non load P.M) page 접근 : Read-only page 접근
 *
 * @ref `page_fault()` from process.c
 *
 * @return bool
 *
 * TODO: Validate the fault
 */
/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED,
                         bool not_present UNUSED) {
  struct supplemental_page_table *spt = &thread_current()->spt;
  void *page_addr = pg_round_down(addr);
  struct page *page = NULL;

  // 유저 권한인데, 커널 주소에 접근하려는 경우 반환
  if (user && is_kernel_vaddr(addr)) return false;

  page = spt_find_page(spt, page_addr);  // 페이지 테이블에서 페이지를 찾음

  if (!page)  // 페이지가 없을 때
  {
    if (!not_present)  // 물리 메모리에 이미 적재가 되어있는 경우
      return false;

    // 스택 확장이 필요한지 검사
    if (addr < (void *)USER_STACK &&  // 접근 주소가 사용자 스택 내에 있고
        addr >= (void *)(f->rsp - 8) &&  // rsp - 8보다 크거나 같으며
        addr >=
            (void *)(USER_STACK - STACK_LIMIT))  // 스택 크기가 1MB 이하인 경우)
    {
      vm_stack_growth(addr);  // 스택 확장
      return true;            // 스택 확장 성공
    }
    return false;  // 스택 확장 조건에 맞지 않음
  } else {
    if (page->writable == false && write == true) {
      return false;
    }

    bool lock = false;
    if (!lock_held_by_current_thread(&filesys_lock)) {
      lock_acquire(&filesys_lock);  // ADD: filesys_lock at file system
      lock = true;
    }
    bool succ = vm_claim_page(page_addr);  // 페이지 클레임
    if (lock) {
      lock_release(&filesys_lock);  // ADD: filesys_lock at file system
      lock = false;
    }
    return succ;
  }
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
  struct page *page = NULL;
  /* TODO: Fill this function */
  page = spt_find_page(&thread_current()->spt, va);
  if (page == NULL) return false;
  return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
  struct frame *frame = vm_get_frame();

  /* Set links */
  frame->page = page;
  page->frame = frame;
  page->is_loaded = true;
  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  struct thread *cur_t = thread_current();
  if (!pml4_set_page(cur_t->pml4, page->va, frame->kva, page->writable)) {
    printf("Failed to set page table entry: va=%p, kva=%p\n", page->va,
           frame->kva);
    return false;
  }
  list_push_back(&frame_list, &frame->frame_elem);  // 프레임을 리스트에 추가

  return swap_in(page, frame->kva);  // 페이지 내용을 스왑인
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  hash_init(&spt->spt_hash, spt_hash_func, page_table_entry_less_function,
            NULL);
}

/*
 * 부모의 supplemental_page_table을 복사하여 자식의 supplemental_page_table에 추가합니다.
 * 부모 페이지 테이블의 각 페이지를 순회하면서 복사하고, 자식 페이지 테이블에 추가합니다.
 * 성공하면 true를 반환하고, 실패하면 false를 반환합니다.
 */
/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
  struct page *parent_page, *child_page = NULL;
  struct page_info_transmitter *src_aux, *dst_aux = NULL;
  uint8_t parent_type;

  struct hash_iterator iterator;
  hash_first(&iterator, &src->spt_hash);

  while (hash_next(&iterator)) {
    parent_page = hash_entry(hash_cur(&iterator), struct page, hash_elem);
    parent_type = parent_page->operations->type;

    // 초기화되지 않은 페이지인 경우
    switch (VM_TYPE(parent_type)) {
      case VM_UNINIT:
        src_aux = (struct page_info_transmitter *)parent_page->uninit.aux;
        dst_aux = malloc(sizeof(struct page_info_transmitter));
        if (!dst_aux) PANIC("malloc FAIL !!");

        dst_aux->file = file_reopen(src_aux->file);
        dst_aux->size = src_aux->size;
        dst_aux->start_addr = src_aux->start_addr;
        dst_aux->read_bytes = src_aux->read_bytes;
        dst_aux->zero_bytes = src_aux->zero_bytes;
        dst_aux->ofs = src_aux->ofs;

        if (!vm_alloc_page_with_initializer(
                parent_page->uninit.type, parent_page->va,
                parent_page->writable, parent_page->uninit.init, dst_aux)) {
          free(dst_aux);
          return false;
        }
        break;

      case VM_ANON:
        if (!vm_alloc_page(parent_type, parent_page->va, parent_page->writable))
          return false;
        if (!vm_claim_page(parent_page->va)) return false;

        child_page = spt_find_page(dst, parent_page->va);
        if (!child_page) return false;

        memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
        break;

      case VM_FILE:
        if (!vm_alloc_page(VM_ANON, parent_page->va, parent_page->writable))
          return false;
        child_page = spt_find_page(dst, parent_page->va);
        if (!child_page) return false;

        if (!vm_claim_page(parent_page->va)) return false;
        child_page->type = VM_FILE;
        memcpy(&child_page->file, &parent_page->file, sizeof(struct file_page));
        memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
        child_page->file.file = file_reopen(parent_page->file.file);
        break;

      default:
        PANIC("{supplemental_page_table_copy()} unknown type !!");
        break;
    }
  }

  return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
  bool lock = false;
  if (!lock_held_by_current_thread(&filesys_lock)) {
    lock_acquire(&filesys_lock);  // ADD: filesys_lock at file system
    lock = true;
  }
  hash_clear(&spt->spt_hash, spt_destory);
  if (lock) {
    lock_release(&filesys_lock);  // ADD: filesys_lock at file system
    lock = false;
  }
}

bool page_table_entry_less_function(struct hash_elem *a, struct hash_elem *b,
                                    void *aux UNUSED) {
  struct page *page_a = hash_entry(a, struct page, hash_elem);
  struct page *page_b = hash_entry(b, struct page, hash_elem);
  return page_a->va < page_b->va;
}

void spt_destory(struct hash_elem *hash_elem, void *aux UNUSED) {
  struct page *page = hash_entry(hash_elem, struct page, hash_elem);
  vm_dealloc_page(page);
}