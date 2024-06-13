/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk; /* 스왑 디스크 */
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* 스왑 슬롯을 관리할 비트맵 */
static struct bitmap *swap_bitmap; /* 스왑 비트맵 */
static struct lock swap_lock;      /* 스왑 락 */

/** 1 slot에 1 page를 담을 수 있는 slot 개수 구하기
 * 		1 sector = 512bytes, 1 page = 4096bytes -> 1 slot = 8 sector */
#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

/* Initialize the data for anonymous pages */ /* 스왑 디스크를 초기화합니다. */
void vm_anon_init(void) {
  /* TODO: Set up the swap_disk. */
  /* 스왑 디스크 초기화 */

  swap_disk = disk_get(1, 1); /* 디스크 1의 장치 1을 스왑 디스크로 설정 */
  if (swap_disk == NULL) {
    PANIC("No swap disk found!");
  }

  int swap_pages = disk_size(swap_disk) / SECTORS_PER_PAGE;
  swap_bitmap = bitmap_create(swap_pages);
  if (swap_bitmap == NULL) {
    PANIC("Swap bitmap creation failed!");
  }
  bitmap_set_all(swap_bitmap, 0);
  lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;
  page->type = page->uninit.type;

  struct page_info_transmitter *aux = page->uninit.aux;
  struct anon_page *anon_page = &page->anon;

  memset(anon_page, 0, sizeof(*anon_page));

  anon_page->swap_slot = -1;  // 스왑 슬롯을 유효하지 않은 값으로 초기화
  return true;
}

/* Swap in the page by read contents from the swap disk. */
/* 익명 페이지를 스왑 디스크에서 스왑 인합니다. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
  size_t swap_index = anon_page->swap_slot;

  if (swap_index == -1) return false;

  lock_acquire(&swap_lock);

  if (bitmap_test(swap_bitmap, swap_index) == false) {
    lock_release(&swap_lock);
    return false;
  }

  // printf("ANON Swapping in page: kva=%p, swap_slot=%zu\n\n", kva, swap_index); // debug by minjoon

  /* 스왑 슬롯에서 페이지를 읽습니다. */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    disk_read(swap_disk, swap_index * SECTORS_PER_PAGE + i,
              (void *)(uint8_t *)kva + i * DISK_SECTOR_SIZE);
  }

  // printf("---------------------------------\n");// debug by jageon
  // bitmap_dump(swap_bitmap);// debug by jageon
  bitmap_flip(swap_bitmap, swap_index);
  // printf("---------------------------------\n");// debug by jageon
  // bitmap_dump(swap_bitmap);// debug by jageon

  lock_release(&swap_lock);
  return true;
}

/* Swap out the page by writing contents to the swap disk. */
/* 익명 페이지를 스왑 디스크로 스왑 아웃합니다. */
static bool anon_swap_out(struct page *page) {
  struct anon_page *anon_page = &page->anon;

  lock_acquire(&swap_lock);

  /* 빈 스왑 슬롯을 찾습니다. */
  size_t swap_slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
  if (swap_slot == BITMAP_ERROR) {
    lock_release(&swap_lock);
    PANIC("Swap space full!");
  }

  // printf("ANON Swapping out page: kva=%p, swap_slot=%zu\n", page->frame->kva, swap_slot); // debug by minjoon

  /* 페이지를 스왑 슬롯에 씁니다. */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    disk_write(swap_disk, swap_slot * SECTORS_PER_PAGE + i,
               (uint8_t *)page->frame->kva + i * DISK_SECTOR_SIZE);
  }

  /* 페이지 구조체에 스왑 슬롯을 저장합니다. */
  anon_page->swap_slot = swap_slot;

  bitmap_set(swap_bitmap, swap_slot, true);

  /* 페이지의 프레임을 지웁니다. */
  pml4_clear_page(thread_current()->pml4, page->va);
  // page->frame = NULL;

  lock_release(&swap_lock);

  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *anon_page = &page->anon;
  // lock_acquire(&swap_lock);
  //⭐️⭐️⭐️ 이놈 풀면 arg none 부터 다터짐
  // bitmap_set(swap_bitmap, anon_page->swap_slot, true);
  // lock_release(&swap_lock);
  if (page->frame && page->frame->page == page) {
    free_frame(page->frame);
  }
  pml4_clear_page(thread_current()->pml4, page->va);
}
