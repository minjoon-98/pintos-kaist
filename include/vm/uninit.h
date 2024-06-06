#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"
#include "filesys/off_t.h"

struct page;
enum vm_type;

typedef bool vm_initializer(struct page *, void *aux);

struct uninit_aux
{
	struct file *file;
	off_t ofs;
	size_t read_bytes;
	size_t zero_bytes;

	// bool writable;
};

/* Uninitlialized page. The type for implementing the
 * "Lazy loading". */
struct uninit_page
{
	/* Initiate the contets of the page */
	vm_initializer *init;
	enum vm_type type;
	void *aux;
	/* Initiate the struct page and maps the pa to the va */
	bool (*page_initializer)(struct page *, enum vm_type, void *kva);
};

void uninit_new(struct page *page, void *va, vm_initializer *init,
				enum vm_type type, void *aux,
				bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
