/**
PAGESUP.C
AUTHORS: SCOTT MUNRO, KEVIN YOO, DARREN SADR

04/08/2015

This file contains code for creating and maintaining the each thread's local 
supplementary page table.
This supplementary page table is used to assist the frame table in eviction
and swapping as well as the kernel in determining valid user addresses 
that aren't in memory
**/

#include "pagesup.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include "swap.h"

/* mutator functions for the hash table */
static unsigned hash_func(const struct hash_elem *e, void *aux);
static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void pagesup_destructor(struct hash_elem* e, void* aux UNUSED);

/* clears all of this pagetable's pages from the swap space */
static void clear_swap(void); 

/* initalize the supplementary page table */
void 
pagesup_init()
{
	hash_init(&thread_current()->pagesup_table, hash_func, less_func, NULL);
}


/* 
finds a pages entry in the supplemental page table 

inputs:
t - the thread whose supplementary page table should be searched through
upage - the user page of the supplementary page table entry being searched for

returns:
a pointer to the entry in the supplementary page table searched for or NULL if 
not found
*/
struct pagesup_entry* 
pagesup_find(struct thread* t, void* upage)
{
  /* code developed using hash table example in
  section A.8.5 of the Reference Guide */
  struct pagesup_entry p;
  struct hash_elem* e;
  //darren
  p.upage = pg_round_down(upage);

  e = hash_find(&t->pagesup_table, &p.h_elem);
  return e != NULL ? hash_entry(e, struct pagesup_entry, h_elem) : NULL;
}

/*
adds a new entry associated with the current thread
to the supplemental page table

inputs:
struct file* src - the source file to assign to a new entry 
off_t start - the offset to assign to a new entry
size_t length - the length to assign to a new entry
void* upage  - the user page to assign to a new entry
bool writable - the writable flag to assign to a new entry

returns
returns true if the add was successful
*/
bool
pagesup_add(struct file* src, off_t start, size_t length, void* upage, bool writable)
{
	/* create and initialize a new pagesup_entry */
	struct pagesup_entry* new_entry = (struct pagesup_entry*)calloc(sizeof(struct pagesup_entry), 1);
	new_entry->src = src;
	new_entry->start = start;
	new_entry->length = length;
	new_entry->upage = pg_round_down(upage);
	new_entry->writable = writable;
	new_entry->swap_index = -1;


	/* add the entry to the hash table */
	struct hash_elem* result = hash_insert (&thread_current()->pagesup_table, &new_entry->h_elem);
	return result == NULL;
}

/*
removes an entry associated with the current thread
from the supplemental page table

inputs:
upage - the userpage to be removed from the supplemental page table

returns
true if the entry was found and removed
false otherwise
*/
bool
pagesup_remove(void* upage)
{
	/* get the entry associated with the upage */
	struct pagesup_entry* current_entry = pagesup_find(thread_current(), upage);
	if(current_entry != NULL)
	{
		/* remove the entry */
		struct hash_elem* result = hash_delete (&thread_current()->pagesup_table, &current_entry->h_elem);

		/* free the data associated with the entry */
		free(current_entry);

		return result != NULL;
	}

	return false;
}

/* 
does the supplementary page table
contain a mapping for the upage 

inputs:
upage - the userpage to search for in the supplemental page table

returns:
true if the entry was found
*/
bool
pagesup_contains(void* upage)
{
	return pagesup_find(thread_current(), upage) != NULL;
}

/* 
used when a process exits to the supplementary page
table's resources and the clear out the swap space 
*/
void
pagesup_destroy()
{
	//scott
	clear_swap();
	hash_destroy(&thread_current()->pagesup_table, pagesup_destructor);
}


/* 
code developed using hash table example in the Reference Guide 
hash function used to place new entries in the underlying hash table

inputs:
e - the hash_elem to hash

returns:
result of the hash
*/
static unsigned
hash_func(const struct hash_elem *e, void *aux UNUSED)
{
	struct pagesup_entry* current_entry = hash_entry(e, struct pagesup_entry, h_elem);
	unsigned pagenum = pg_no(current_entry->upage);
	unsigned result = hash_int(pagenum);
	return result;
}

/* 
code developed using hash table example in the Reference Guide 
compares two elements in the hash table

inputs:
a - element a to compare in the less function
b - element b to compare to in the less function

returns:
true if a should be ordered first in the list
false otherwise

*/
static bool
less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
	bool order = false;
	struct pagesup_entry* entry_a = hash_entry(a, struct pagesup_entry, h_elem);
	struct pagesup_entry* entry_b = hash_entry(b, struct pagesup_entry, h_elem);

	/* if they are from the same thread, sort by the address of the upage */
	order = pg_no(entry_a->upage) < pg_no(entry_b->upage);

	return order;
}

/* 
code developed using hash table example in the Reference Guide 
destructor function used to clean up page table resources

inputs:
e - the hash element to be freed

*/
static void
pagesup_destructor(struct hash_elem* e, void* aux UNUSED)
{
	struct pagesup_entry* entry = hash_entry(e, struct pagesup_entry, h_elem);
	free(entry);
}

//scott 
/* 
a function designed to clear out the swap space of any pages marked
in the supplemental page table as swapped out
*/
static void
clear_swap()
{
	/*built from the example code in the pintos documentation 
	https://www.cs.utexas.edu/users/ans/classes/cs439/projects/pintos/WWW/pintos_6.html#SEC131*/
	/* iterate over all the pages of the current thread and check to see if they're in swap, if so remove slot from swap table */
	struct hash* h = &thread_current()->pagesup_table;
	struct hash_iterator i;
	hash_first(&i, h);
	while(hash_next(&i))
	{
		struct pagesup_entry *page = hash_entry(hash_cur(&i), struct pagesup_entry, h_elem);
		if(page->swap_index != -1)
			swap_remove(page->swap_index);
	}
}
