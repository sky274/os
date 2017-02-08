/**
FRAME.C
AUTHORS: SCOTT MUNRO, KEVIN YOO, DARREN SADR

04/05/2015

This file contains code for creating and maintaining the kernel's frame table.
This frame table is used to manage which user virtual pages are being store
physically in memory
**/


#include "threads/loader.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include <stdio.h>
#include <bitmap.h>
#include "frame.h"
#include "pagesup.h"
#include "swap.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"

static void set_frame(size_t, void*);
static void remove_frame(size_t index);
static size_t find_free_frame(void);
static size_t clock_evict_frame(void);
static bool find_eviction_loc(struct thread* owner, void* uaddr);

static struct bitmap* frame_map;		/* bitmap used to track free frames */
static size_t table_size;				/* size of the frame table */
static struct frame_entry* frame_table;	/* the table itself, an array of frame entries*/

/* lock for the frame table to ensure synchronized allocation */
static struct lock frame_table_lock;

/* clock hand used for the clock eviction algorithm
SEE clock_evict_frame(...) */
static size_t clockhand;

//scott
/* the frame_lock functions used to
manipulate the frame_lock from outside
of frame.c */
void 
acquire_frame_lock()
{ 
  lock_acquire(&frame_table_lock); 
}

bool 
has_frame_lock()
{
  return frame_table_lock.holder == thread_current();
}

void 
release_frame_lock()
{ 
  lock_release(&frame_table_lock); 
}

// scott
// kevin
// darren
/* 
initialize the frame table 
For each user page in memory there will be
one entry in the frame table
*/
void
frame_init(void)
{
	/* create a frame container/table */
	/* half of the initial ram pages are used for user memory */
	table_size = palloc_number_userpages();

	/* allocate the frame table */
	frame_table = (struct frame_entry*)malloc(sizeof(struct frame_entry) * table_size);

	/* initalize the bitmap used to track which frames are free */
	frame_map = bitmap_create(table_size);

	/* map each frame of memory to a slot in our frame table */
	struct frame_entry* table_iterator = frame_table;
	size_t i = 0;
	for(; i < table_size; i++){
		
		uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
		ASSERT(kpage != NULL);
		/* set each entry in the table to a mapped kpage with
		empty data for everything else */
		table_iterator->phys_addr = kpage;
		table_iterator->user_addr = NULL;
		table_iterator->owner = NULL;
		table_iterator->pin = false;
		/* go to the next entry in the frame table */
		table_iterator++;
	}

	/* give the frame table a lock for synchronization */
	lock_init(&frame_table_lock);

	/* initialize the clock hand */
	clockhand = 0;
}

// kevin
/*
add a page of data to the frame table
from the swap space or disk 

inputs:
upage - the virtual page to assign to a frame in physical memory

return:
returns the frame entry given to the process
*/
struct frame_entry*  
get_frame(void* upage)
{
	/* put our user page in the frame table... */
	/* get a free frame */
	size_t frame_index = find_free_frame();

	/* set the frame with the new frame's metadata */
	set_frame(frame_index, upage);
	struct frame_entry* new_entry = &frame_table[frame_index];

	return new_entry;
}

/*
remove all of the frames associated with a thread
this can be used so that when a thread dies, none
of its old entries in the frame table will linger
*/
// scott
void
clear_frames()
{
	size_t frame_index = 0;
	struct thread* current = thread_current();

	/* use the lock to synchronize removing frames */
	lock_acquire(&frame_table_lock);
	for(; frame_index < table_size; frame_index++)
	{
		/*if the current frame is used
		and is owned by this thread, remove it */
		if(bitmap_test(frame_map, frame_index) && frame_table[frame_index].owner == current)
		{
			remove_frame(frame_index);
		}
	}
	lock_release(&frame_table_lock);
}

/* 
set the frame at index
used to update the frame table 

inputs: 
index - the index of the frame to set
upage - the user virtual address being assigned

*/
static void
set_frame(size_t index, void* upage)
{
	struct frame_entry* frame = &frame_table[index];
	frame->user_addr = upage;
	frame->owner = thread_current();
}

/*
externally synchronized --
removes a frame from the frame table
resets all of its values 

inputs:
index - the index of the frame to remove
*/
static void
remove_frame(size_t index)
{
	/* the meta data of the page being cleared from the pagedir */
	struct thread* thread_clear = frame_table[index].owner;
	void* upage_clear = frame_table[index].user_addr; 
	/* clear the page out of page directory */
	pagedir_clear_page(thread_clear->pagedir, upage_clear); 
	/* clear the uaddr in the frame */
	set_frame(index, NULL);
	/* clear the frame's owner */
	frame_table[index].owner = NULL;
	/* update our free map */ 
	bitmap_reset(frame_map, index);
}

/*
iterate through the frame table to find
the first free frame, if no frames are free
evict a frame and use that frame 

returns:
returns the index of the free frame
*/
static size_t
find_free_frame(void)
{
	lock_acquire(&frame_table_lock);
	/* find the first free frame*/
	size_t free_frame_index = bitmap_scan (frame_map, 0, 1, false);
	if(free_frame_index != BITMAP_ERROR)
	{
		bitmap_mark(frame_map, free_frame_index);
		frame_table[free_frame_index].pin = true;
	}
	/* update the frame map */
	lock_release(&frame_table_lock);


	if(free_frame_index == BITMAP_ERROR)
	{
		/*if there are no free frames, evict a frame and return it's index */
		free_frame_index = clock_evict_frame();
	} 


	return free_frame_index;
}

/* 
Implementation of the clock eviction algorithm
used to evict a frame from the frame table 

returns:
the index of the evicted frame/ newly claimed frame
*/
static size_t
clock_evict_frame()
{

	/* get the frame to evict */
	/* synchronize the frame eviction */
	lock_acquire(&frame_table_lock);
	struct frame_entry* evicted_entry = NULL;
	size_t evicted_index = -1;
	// scott
	while(evicted_entry == NULL)
	{
		struct frame_entry* current_entry = &frame_table[clockhand];
		uint32_t* pagedir = current_entry->owner->pagedir;
		void* vaddr = current_entry->user_addr;

		/* if the reference bit is set... */
		if(current_entry->pin == false)
		{
			if(pagedir_is_accessed(pagedir, vaddr))
			{
				/* reset the reference bit */
				pagedir_set_accessed(pagedir, vaddr, false);
			}
			/* else found victim page */
			else
			{
				/* mark victim page */
				evicted_entry = current_entry;
				evicted_index = clockhand;
			}
		}

		clockhand = (clockhand+1)%table_size;
	}
	/* claim the information of the frame to be evicted */
	struct thread* evicted_thread = evicted_entry->owner;
	void* evicted_paddr = evicted_entry->phys_addr;
	void* evicted_uaddr = evicted_entry->user_addr;
	/* evict the frame */
	remove_frame(evicted_index);
	bitmap_mark(frame_map, evicted_index);
	/* pin the frame */
	evicted_entry->pin = true;
	/* release the lock */
	lock_release(&frame_table_lock);
	
	//scott
	/* check if the thread needs to be swapped out.. */
	if(find_eviction_loc(evicted_thread, evicted_uaddr))
	{
		/* find free swap slot */
		block_sector_t swap_index = swap_find_slot();
		/* get the pagesup entry associated with the evicted frame */
		struct pagesup_entry* page = pagesup_find(evicted_thread, evicted_uaddr);
		/* swap index indicates where this frame is stored in swap space */
		page->swap_index = swap_index;
		// kevin
		/* keep track of the dirty bit */
		page->dirty = pagedir_is_dirty(evicted_thread->pagedir, evicted_uaddr);
		/* sanity check to ensure that the virutal addresses of the frame table and the pagetable are consistent */
		ASSERT(page->upage == evicted_uaddr);
		/* write frame to swap */
		swap_writeto(evicted_paddr, swap_index);
	}

	return evicted_index;
}

/* 
returns true if the frame should be evicted to swap
returns false if the frame should just be cleared out of memory
and not written back to disk 

inputs:
owner - the thread that owns the frame
uaddr - the virtual user address of the frame
*/
static bool
find_eviction_loc(struct thread* owner, void* uaddr)
{
	/* get the information on the page in physical memory */
	struct pagesup_entry* page = pagesup_find(owner, uaddr);
	ASSERT(page != NULL);

	/* is the page a stack page? */
	if(page->src == NULL)
		return true;

	/* is the page read-only? */
	if(!page->writable)
		return false;

	/* is the page dirty? */
	if(pagedir_is_dirty(owner->pagedir, uaddr))
		return true;

	ASSERT(0 && "reached end of eviction location func, should not happen");
	return true;
}

/* dump the frame table for debugging */
void
dump_frame_table(void)
{
	bitmap_dump (frame_map); 
}
