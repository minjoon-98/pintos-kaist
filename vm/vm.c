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

struct lock frame_lock;

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
  lock_init(&frame_lock);
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

  // printf("@@ initializer start\n");
  struct supplemental_page_table *spt = &thread_current()->spt;
  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    // printf("@@ initializer spt_find_page(spt, upage) == NULL)\n");
    /* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) goto err;
    // printf("page type = %d", type);
    // printf("logical addr = %p\n", upage);
    if (VM_TYPE(type) == VM_ANON) {
      // printf("@@ uninit_new() start\n");
      uninit_new(page, upage, init, type, aux, anon_initializer);
    }
    if (VM_TYPE(type) == VM_FILE) {
      // printf("@@ uninit_new() start\n");
      uninit_new(page, upage, init, type, aux, file_backed_initializer);
    }
    page->writable = writable;
    page->original_writable = writable;
    // printf("@@ uninit_new() DONE\n");
    page->is_loaded = false;
    /* TODO: Insert the page into the spt. */
    if (spt_insert_page(spt, page) == false) {
      free(page);
      // uncheckec : err발생시 aux 처리에 대한 검증 필요.
      goto err;
    }
    // printf("allocated vm address is : %p\n", spt_find_page(spt, upage)->va);
    // printf("@@ initializer done type : %d\n", type);
    return true;
  }
  // printf("@@ initializer spt_find_page(spt, upage) != NULL)\n");

err:
  // printf("@@ initializer ERROR !! type : %d\n", type);
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
  if (swap_out(victim->page)) {
    victim->page = NULL;
    memset(victim->kva, 0, PGSIZE);
    victim->ref_count = 1;
    return victim;
  }
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
  frame->ref_count = 1;

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
static bool vm_handle_wp(struct page *page UNUSED) {
  if (!page->original_writable) return false;

  if (page->frame->ref_count > 1) {
    // 물리 frame 새로 할당
    struct frame *new_frame = vm_get_frame();
    if (!new_frame) return false;
    memcpy(new_frame->kva, page->frame->kva, PGSIZE);
    lock_acquire(&frame_lock);
    page->frame->ref_count--;

    page->frame = new_frame;
    page->frame->ref_count = 1;
    lock_release(&frame_lock);
    pml4_clear_page(thread_current()->pml4, page->va);
  }
  page->writable = true;
  pml4_set_page(thread_current()->pml4, page->va, page->frame->kva, true);

  return true;
}

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
    if (write && !not_present) {
      return vm_handle_wp(page);
    }

    bool lock = false;
    if (!lock_held_by_current_thread(&filesys_lock)) {
      lock_acquire(&filesys_lock);  // ADD: filesys_lock at file system
      lock = true;
    }
    bool succ = vm_do_claim_page(page);  // 페이지 클레임
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
  struct hash_iterator i;
  hash_first(&i, &src->spt_hash);
  // printf("@@ spt_copy 1\n");

  // printf("hash table size : %d \n", hash_size(&src->spt_hash));

  while (hash_next(&i)) {

    // printf("@@ --------------- loop start---------------\n");

    struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);

    // printf("src page->va : %p\n", src_page->va);

    if (!src_page) {
      // printf("@@ src_page is NULL\n");
      return false;
    }
    struct page *dst_page;
    enum vm_type src_type = VM_TYPE(src_page->operations->type);
    // printf("@@ src type  : %d\n", src_type);
    if (src_type == VM_UNINIT) {
      if (!vm_alloc_page_with_initializer(
              src_page->uninit.type, src_page->va, src_page->writable,
              src_page->uninit.init, src_page->uninit.aux)) {
        // printf("@@ VM_UNITI die\n");
        return false;
      }
      // printf("@@ VM_UNITI done !\n");

      continue;
    }

    if (src_type == VM_FILE) {
      // printf("@@ is FILE TYPE \n");
      struct page_info_transmitter *info =
          malloc(sizeof(struct page_info_transmitter));
      if (!info) {
        // printf("@@ malloc fail aux\n");
        return false;
      };
      // printf("@@ info 역참조 시작 \n");
      info = (struct page_info_transmitter *)src_page->uninit.aux;
      // printf("@@ info 역참조 DONE \n");

      if (!vm_alloc_page_with_initializer(VM_FILE, src_page->va,
                                          src_page->writable,
                                          src_page->uninit.init, info)) {
        // printf("@@ not alloc page with init\n");

        free(info);
        return false;
      }
      // printf("@@ VM_FILE done !\n");

    } else {
      if (!vm_alloc_page(src_type, src_page->va, src_page->writable)) {
        // printf("@@ not alloc page\n");
        return false;
      }
      // printf("@@ VM_ANON done !\n");
    }

    dst_page = spt_find_page(dst, src_page->va);
    if (dst_page == NULL) {
      // printf("@@ not found dst_page\n");
      return false;
    }

    dst_page->operations = src_page->operations;
    dst_page->frame = src_page->frame;
    dst_page->writable = false;
    dst_page->original_writable = src_page->writable;
    src_page->writable = false;

    // printf("@@ lock 획득 \n");
    lock_acquire(&frame_lock);
    src_page->frame->ref_count++;
    lock_release(&frame_lock);
    // printf("@@ lock 놓기 \n");

    if (!pml4_set_page(thread_current()->pml4, dst_page->va,
                       dst_page->frame->kva, false)) {
      // printf("@@ !pml4_set_page\n");

      return false;
    }

    if (!pml4_set_page(thread_current()->parent_pml4, src_page->va,
                       src_page->frame->kva, false)) {
      // printf("@@ !!pml4_set_page src_page\n");

      return false;
    }

    // printf("@@ loop done \n");
  }

  // printf("@@ spt_copy 3\n");

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

void free_frame(struct frame *frame) {
  lock_acquire(&frame_lock);

  if (frame->ref_count > 1) {
    frame->ref_count--;
    lock_release(&frame_lock);
    return;
  }

  list_remove(&frame->frame_elem);
  palloc_free_page(frame->kva);
  free(frame);

  lock_release(&frame_lock);
}