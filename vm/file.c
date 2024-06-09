/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "string.h"
#include "threads/vaddr.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
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

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	// struct thread *t = thread_current();

	// if (pml4_is_dirty(t->pml4, page->va))
	// {
	// 	// 페이지가 더럽다면 파일에 변경 내용을 기록
	// 	file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
	// }
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
    struct file_page *file_page UNUSED = &page->file;
    struct thread *t = thread_current();

    // 페이지가 더럽다면 파일에 변경 내용을 기록
    if (pml4_is_dirty(t->pml4, page->va))
    {
        file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
        pml4_set_dirty(t->pml4, page->va, false);
    }

    // 페이지 테이블에서 제거
    pml4_clear_page(t->pml4, page->va);

    // 해시 테이블에서 제거
    hash_delete(&t->spt.spt_hash, &page->hash_elem);
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	off_t remain_size;
	off_t original_size = file_length(file);
	if	(original_size <= offset ||
		(pg_round_down(addr) != addr) ||
		offset % PGSIZE != 0) return NULL;
	uint32_t read_size = (original_size - offset) < length ? (original_size - offset) : length;
	remain_size = read_size;
	off_t ofs = offset;
	off_t read_bytes, zero_bytes;
	uint8_t *upage = addr;
	struct thread *t = thread_current();
	while (0 < remain_size)
	{
		read_bytes = PAGE_SIZE <= remain_size ? PAGE_SIZE : remain_size;
		zero_bytes = PAGE_SIZE - read_bytes;

		ASSERT((read_bytes + zero_bytes) % PAGE_SIZE == 0);
		void *aux = NULL;
		aux = malloc(sizeof(struct page_info_transmitter));

		if (aux == NULL)
			return NULL;

		((struct page_info_transmitter *)aux)->file = file;
		((struct page_info_transmitter *)aux)->ofs = ofs;
		((struct page_info_transmitter *)aux)->read_bytes = read_bytes;
		((struct page_info_transmitter *)aux)->zero_bytes = zero_bytes;
		((struct page_info_transmitter *)aux)->start_addr = addr;
		((struct page_info_transmitter *)aux)->size = read_size;
		((struct page_info_transmitter *)aux)->owner = t;

		if (!vm_alloc_page_with_initializer(VM_FILE|VM_MARKER_1, upage,
											writable, lazy_load_segment, aux))
		{
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
void do_munmap(void *addr)
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct thread *t = thread_current();

	struct page *target_page = spt_find_page(spt, addr);
	if (target_page == NULL ||
		VM_TYPE(target_page->type) != VM_FILE ||
		target_page->file.start_addr != addr)
		return;

	off_t remain_size = target_page->file.size;
	off_t ofs = target_page->file.ofs;
	uint8_t *upage = addr;

	while (remain_size > 0)
	{
		target_page = spt_find_page(spt, upage);
		if (target_page == NULL)
			break;

		off_t read_bytes = target_page->file.read_bytes;
		if (pml4_is_dirty(t->pml4, upage))
		{	
			file_write_at(target_page->file.file, target_page->va, read_bytes, ofs);
		}

		hash_delete(&spt->spt_hash, &target_page->hash_elem);
		vm_dealloc_page(target_page);
		remain_size -= PAGE_SIZE;
		ofs += PAGE_SIZE;
		upage += PAGE_SIZE;
	}
}
