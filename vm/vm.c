/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "include/userprog/process.h"

struct list frame_table;
struct list_elem* ft_elem;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init (&frame_table);
	ft_elem = list_begin (&frame_table);
	lock_init (&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		struct page* new_page = (struct page*)malloc (sizeof (struct page));

		if (new_page == NULL) {
			goto err;
		}

		switch (VM_TYPE (type)) {
			case VM_ANON:
				uninit_new (new_page, pg_round_down (upage), init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new (new_page, pg_round_down (upage), init, type, aux, file_backed_initializer);
				break;
			default:
				NOT_REACHED ();
				break;

		}
		new_page->writable = writable;
		return spt_insert_page (spt, new_page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page *elem_page;
	elem_page->va = pg_round_down (va);
	struct hash_elem *e = hash_find (&spt->spt_hash, &elem_page->hash_elem);

	if (e != NULL) {
		page = hash_entry (e, struct page, hash_elem);
	}

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	struct hash_elem *e = hash_insert (&spt->spt_hash, &page->hash_elem);

	if (e == NULL) {
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete (&spt->spt_hash, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	struct thread *curr = thread_current ();
	struct list_elem *e = ft_elem;

	lock_acquire (&frame_lock);
	for (ft_elem = e; ft_elem != list_end (&frame_table); ft_elem = list_next (ft_elem)) {
		victim = list_entry (ft_elem, struct frame, frame_elem);

		if (pml4_is_accessed (curr->pml4, victim->page->va)) {
			pml4_set_accessed (curr->pml4, victim->page->va, 0);
		}
		else {
			return victim;
		}
	}

	for (ft_elem = list_begin (&frame_table); ft_elem != e; ft_elem = list_next (ft_elem)) {
		victim = list_entry (ft_elem, struct frame, frame_elem);
		if (pml4_is_accessed (curr->pml4, victim->page->va)) {
			pml4_set_accessed (curr->pml4, victim->page->va, 0);
		}
		else {
			return victim;
		}
	}
	lock_release (&frame_lock);

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (victim->page != NULL) {
		swap_out (victim->page);
	}

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	frame = (struct frame *)malloc (sizeof (struct frame));
	frame->kva = palloc_get_page (PAL_USER);

	if (frame->kva == NULL) {
		frame = vm_evict_frame ();
	}

	list_push_back (&frame_table, &frame->frame_elem);
	frame->page = NULL;

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void *rounded_addr = pg_round_down (addr);

	if (vm_alloc_page (VM_ANON | VM_MARKER_0, rounded_addr, true)) {
		if (vm_claim_page (rounded_addr)) {
			thread_current ()->stack_bottom -= PGSIZE;
		}
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
	/* Extra! */
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	
	/* There is no address */
	if (addr == NULL) {
		return false;
	}

	/* The address is kernel page */
	if (is_kernel_vaddr (addr)) {
		return false;
	}

	/* Is this a physical page? */
	if (!not_present) {
		return false;
	}
	else {
		page = spt_find_page (spt, addr);

		if (page == NULL) {
			void *rsp_stack;

			if (is_kernel_vaddr (f->rsp)) {
				rsp_stack = thread_current ()->rsp_stack;
			}
			else {
				rsp_stack = f->rsp;
			}

			/* Stack growth */
			if (addr < thread_current ()->stack_bottom && addr <= USER_STACK && addr >= rsp_stack - 8 && addr >= USER_STACK - (1 << 20)) {
				void *growth_addr = thread_current ()->stack_bottom - PGSIZE;
				vm_stack_growth (growth_addr);
				page = spt_find_page (spt, growth_addr);
			}
			else {
				return false;
			}
		}

		if (write && !page->writable) {
			return false;
		}

		return vm_do_claim_page (page);
	}
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page (&thread_current ()->spt, va);

	if (page == NULL) {
		return false;
	}
	else {
		return vm_do_claim_page (page);
	}
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page (thread_current ()->pml4, page->va, frame->kva, page->writable)) {
		return false;
	}

	return swap_in (page, frame->kva);
}

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
    const struct page *p = hash_entry (p_, struct page, hash_elem);

    return hash_bytes (&p->va, sizeof p->va);
}

bool
sort_by_hash_priority (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
	const struct page *hash_a = hash_entry (a, struct page, hash_elem);
    const struct page *hash_b = hash_entry (b, struct page, hash_elem);

    return (hash_a->va < hash_b->va);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->spt_hash, page_hash, sort_by_hash_priority, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
    hash_first (&i, &src->spt_hash);
	int succ = false;

    while (hash_next (&i)) {
		struct page *src_page = hash_entry (hash_cur (&i), struct page, hash_elem);

		succ = vm_alloc_page_with_initializer (src_page->uninit.type, src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux);
		struct page *dst_page = spt_find_page (dst, src_page->va);

		if (src_page->frame != NULL) {
			succ = vm_do_claim_page (dst_page);
			memcpy (dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		}
	}

	return succ;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy (&spt->spt_hash, hash_kill);
}

/* Remove hash page */
void
hash_kill (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry (e, struct page, hash_elem);
	vm_dealloc_page (page);
}