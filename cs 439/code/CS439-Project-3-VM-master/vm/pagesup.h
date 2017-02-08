#ifndef PAGESUP_H_
#define PAGESUP_H_

#include "threads/thread.h"
#include "filesys/off_t.h"
#include <hash.h>

struct pagesup_entry
{
	struct file* src;			/* source file for this page;
									stack pages src == NULL */
	off_t start;				/* where the data for source file begins */
	size_t length;				/* length of data to read in from source */
	void* upage;				/* used to identify the entry and to map 
									the virtual address to physical memory*/
	bool writable;				/* is this page writable to? */
	struct hash_elem h_elem;	/* used for hashing */

	int swap_index;				/* index of the swap slot */
	bool dirty;
};

/* LIFECYCLE */
void pagesup_init(void);
void pagesup_destroy(void);

/* PAGETABLE MANIPULATION */
struct pagesup_entry* pagesup_find(struct thread* t, void* upage);
bool pagesup_add(struct file* src, off_t start, size_t length, void* upage, bool writable);
bool pagesup_remove(void* upage);
bool pagesup_contains(void* upage);

#endif
