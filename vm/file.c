/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "filesys/file.h"
#include "userprog/process.h"
#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {

}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	page->type = VM_FILE;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	off_t remain_size;
	remain_size = (file_length(file) - offset) < length ? (file_length(file) - offset) : length;
	off_t ofs = offset;
	off_t read_bytes, zero_bytes;
	uint8_t *upage = addr;

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

		if (!vm_alloc_page_with_initializer(VM_FILE, upage,
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
void
do_munmap (void *addr) {
}
