/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

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

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	if (page == NULL) {
		return false;
	}

	struct data_for_lazy_load *aux = (struct data_for_lazy_load *)page->uninit.aux;

	struct file *file = aux->file;
	off_t offset = aux->offset;
	size_t page_read_bytes = aux->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	file_seek (file, offset);
	
	if ((int)file_read (file, kva, page_read_bytes) != (int)page_read_bytes) {
		return false;
	}

	memset (kva + page_read_bytes, 0, page_zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if (page == NULL) {
		return false;
	}

	struct data_for_lazy_load *aux = (struct data_for_lazy_load *)page->uninit.aux;

	if (pml4_is_dirty (thread_current ()->pml4, page->va)) {
		file_write_at (aux->file, page->va, aux->page_read_bytes, aux->offset);
		pml4_set_dirty (thread_current ()->pml4, page->va, 0);
	}

	pml4_clear_page (thread_current ()->pml4, page->va);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if (page->frame != NULL) {
		free (page->frame);
	}
}

static bool
mmap_lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct data_for_lazy_load *data_for_lazy_load = (struct data_for_lazy_load *)aux;
	file_seek (data_for_lazy_load->file, data_for_lazy_load->offset);
	size_t page_read_bytes = ((struct data_for_lazy_load *)aux)->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	if(file_read (data_for_lazy_load->file, page->frame->kva, page_read_bytes) != (int)page_read_bytes) {
		palloc_free_page (page->frame->kva);
		return false;
	}

	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}

/* Do the mmap (similar structure with load_segment) */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	file = file_reopen (file);
	void *mapped_addr = addr;
	uint32_t read_bytes = file_length (file);
	uint32_t zero_bytes = (length - read_bytes);

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (addr) == 0);
	ASSERT (offset % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct data_for_lazy_load *aux = (struct data_for_lazy_load *)malloc (sizeof (struct data_for_lazy_load));
		aux->file = file;
		aux->offset = offset;
		aux->page_read_bytes = page_read_bytes;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, 
				writable, mmap_lazy_load_segment, aux)) {
			file_close (file);
			return NULL;
		}

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return mapped_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *curr = thread_current ();
	struct page *page;

	while ((page != NULL)) {
		if (pml4_is_dirty (curr->pml4, addr)) {
			lock_acquire (&filesys_lock);
			struct data_for_lazy_load *aux = page->uninit.aux;
			file_write_at (aux->file, addr, aux->page_read_bytes, aux->offset);
			pml4_set_dirty (thread_current ()->pml4, page->va, false);
			lock_release (&filesys_lock);
		}

		pml4_clear_page (thread_current()->pml4, addr);

		addr += PGSIZE;
		page = spt_find_page (&curr->spt, addr);
	}

}