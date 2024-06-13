/* file.c: Implementation of memory backed file object (mmaped object). */

#include "filesys/file.h"
#include "string.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/vm.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

struct lock file_swap_lock;

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) { lock_init(&file_swap_lock); }

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &file_ops;
  page->type = page->uninit.type;

  struct page_info_transmitter *aux = page->uninit.aux;
  struct file_page *file_page = &page->file;

  memset(file_page, 0, sizeof(*file_page));

  file_page->file = aux->file;
  file_page->ofs = aux->ofs;
  file_page->read_bytes = aux->read_bytes;
  file_page->zero_bytes = aux->zero_bytes;
  file_page->start_addr = aux->start_addr;
  file_page->size = aux->size;
  file_page->owner = aux->owner;
  return true;
}

/* Swap in the page by read contents from the file. */ /* 파일 백업 페이지를 스왑 인합니다. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page = &page->file;
  off_t read_bytes = file_page->read_bytes;
  off_t ofs = file_page->ofs;
  struct file *file = file_page->file;

  if (file_read_at(file, kva, read_bytes, ofs) != (int)read_bytes) {
    printf("File read error: expected=%d, actual=%d\n", (int)read_bytes,
           file_read_at(file, kva, read_bytes, ofs));
    return false;
  }

  // printf("FILE Swapping in page: kva=%p, read_bytes=%d, ofs=%d\n\n", kva, read_bytes, ofs); // debug by minjoon

  list_push_back(&frame_list, &page->frame->frame_elem);
  memset(kva + read_bytes, 0, PGSIZE - read_bytes);
  return true;
  // return lazy_load_segment(page, NULL);
}

/* Swap out the page by writeback contents to the file. */ /* 파일 백업 페이지를 스왑 아웃합니다. */
static bool file_backed_swap_out(struct page *page) {
  if (page == NULL) return false;

  struct file_page *file_page UNUSED = &page->file;
  off_t read_bytes = file_page->read_bytes;
  off_t ofs = file_page->ofs;
  struct file *file = file_page->file;

  struct thread *t = thread_current();

  if (&page->file != NULL && page->writable != NULL &&
      pml4_is_dirty(t->pml4, page->va)) {
    off_t written_bytes =
        PGSIZE < page->file.read_bytes ? PGSIZE : page->file.read_bytes;
    off_t written =
        file_write_at(&page->file, page->va, written_bytes, page->file.ofs);

    // printf("File written: kva=%p, read_bytes=%d, ofs=%d\n", page->va, read_bytes, ofs);

    pml4_set_dirty(thread_current()->pml4, page->va,
                   false);  // 페이지가 더 이상 더럽지 않도록 설정
  }

  // printf("FILE Swapping out page: kva=%p, read_bytes=%d, ofs=%d\n", page->frame->kva, read_bytes, ofs); // debug by minjoon

  // 페이지 프레임 해제 및 페이지 테이블에서 제거
  pml4_clear_page(t->pml4, page->va);
  // palloc_free_page(page->frame->kva);

  // 페이지의 프레임 포인터를 NULL로 설정
  // page->frame->kva = NULL;
  page->frame = NULL;

  return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
  struct thread *t = thread_current();

  off_t read_bytes = file_page->read_bytes;
  off_t ofs = file_page->ofs;
  struct file *file = file_page->file;

  // 페이지가 더럽다면 파일에 변경 내용을 기록
  if (file != NULL && page->writable && pml4_is_dirty(t->pml4, page->va)) {
    off_t written_bytes = PGSIZE < read_bytes ? PGSIZE : read_bytes;
    off_t written = file_write_at(file, page->va, written_bytes, ofs);

    if (written != written_bytes) {
      printf("File write error in destroy: expected=%d, actual=%d\n",
             (int)written_bytes, (int)written);
    }

    pml4_set_dirty(t->pml4, page->va,
                   false);  // 페이지가 더 이상 더럽지 않도록 설정
  }

  // 페이지 테이블에서 제거
  pml4_clear_page(t->pml4, page->va);

  if (page->frame && page->frame->page == page) {
    lock_acquire(&file_swap_lock);
    free_frame(page->frame);
    lock_release(&file_swap_lock);
  }

  // 해시 테이블에서 제거
  hash_delete(&t->spt.spt_hash, &page->hash_elem);
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {
  off_t remain_size;
  off_t original_size = file_length(file);

  // 주소가 페이지 경계에 정렬되어 있는지 확인하고, 올바르지 않으면 NULL 반환
  if (original_size <= offset || (pg_round_down(addr) != addr) ||
      offset % PGSIZE != 0)
    return NULL;

  // 요청된 크기와 파일의 실제 크기 중 작은 값을 선택하여 읽을 크기로 설정
  uint32_t read_size =
      (original_size - offset) < length ? (original_size - offset) : length;
  remain_size = read_size;
  off_t ofs = offset;
  off_t read_bytes, zero_bytes;
  uint8_t *upage = addr;
  struct thread *t = thread_current();

  // 요청된 길이만큼 페이지를 생성하고 파일의 내용을 메모리로 로드
  while (0 < remain_size) {
    read_bytes = PAGE_SIZE <= remain_size ? PAGE_SIZE : remain_size;
    zero_bytes = PAGE_SIZE - read_bytes;

    // clang-format off
    struct page_info_transmitter *aux = malloc(sizeof(struct page_info_transmitter));
    if (!aux) PANIC("{do_mmap} malloc FAIL !!");
    // clang-format on

    // 페이지 정보 설정
    aux->file = file_reopen(file);
    aux->ofs = ofs;
    aux->read_bytes = read_bytes;
    aux->zero_bytes = zero_bytes;
    aux->start_addr = addr;
    aux->size = read_size;
    aux->owner = t;

    // 페이지 할당 및 초기화자를 이용하여 페이지 로드

    if (!vm_alloc_page_with_initializer(VM_FILE, upage, writable,
                                        lazy_load_segment, aux)) {
      free(aux);
      return NULL;
    }

    upage += PAGE_SIZE;
    ofs += read_bytes;
    remain_size -= read_bytes;
  }
  return addr;
}
/* Do the munmap */
void do_munmap(void *addr) {
  struct supplemental_page_table *spt = &thread_current()->spt;
  struct thread *t = thread_current();

  struct page *target_page = spt_find_page(spt, addr);
  if (target_page == NULL || VM_TYPE(target_page->type) != VM_FILE ||
      target_page->file.start_addr != addr)
    return;

  off_t remain_size = target_page->file.size;
  off_t ofs = target_page->file.ofs;
  uint8_t *upage = addr;

  // munmap을 통해 매핑된 페이지들을 해제하고, 변경된 페이지의 내용을 파일에 기록
  while (remain_size > 0) {
    target_page = spt_find_page(spt, upage);
    if (target_page == NULL) break;

    off_t read_bytes = target_page->file.read_bytes;
    // 페이지가 변경되었다면 파일에 기록
    if (pml4_is_dirty(t->pml4, upage)) {
      // lock_acquire(&filesys_lock); // ADD: filesys_lock at file system
      file_write_at(target_page->file.file, target_page->va, read_bytes, ofs);
      // lock_release(&filesys_lock); // ADD: filesys_lock at file system
      pml4_set_dirty(t->pml4, target_page->va, false);
    }

    // list_remove(&target_page->frame->frame_elem);

    // 페이지 제거 및 메모리 해제
    // spt_remove_page(&spt, &target_page);
    hash_delete(&spt->spt_hash, &target_page->hash_elem);
    /* ----------------- ⭐️⭐️⭐️ 추후 추가해야함 ----------------- */
    // spt_remove_page(&thread_current()->spt, target_page);
    // vm_dealloc_page(target_page);
    /* --------------------------------------------------------- */
    // vm_dealloc_page(target_page); // 호출자가 페이지 구조체를 free 해주기 때문에 free(page)는 필요 없음 by AruJoy
    // 페이지 테이블에서 유효비트(valid bit)만 0으로 변경(메모리에 적재되지 않음을 표시)
    pml4_clear_page(t->pml4, upage);

    remain_size -= PAGE_SIZE;
    ofs += PAGE_SIZE;
    upage += PAGE_SIZE;
  }
}
