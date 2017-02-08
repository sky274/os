/**
SWAP.C
AUTHORS: SCOTT MUNRO, KEVIN YOO, DARREN SADR

04/13/2015

This file contains code for creating and maintaining 
the global swap space and swap table.

**/

#include "swap.h"
#include "threads/synch.h"

static struct bitmap * swap_table;  /* our swap table */
static int swap_table_size; 		/* size of the swap table */
static struct block *swap_block; 	/* swap block object */
static struct lock swap_lock;

//scott
/* initialize the swap table */
void 
swap_init()
{
	/* retrieve the swap block struct */
	swap_block = block_get_role(BLOCK_SWAP);
	/* get the number of sectors */
	swap_table_size = block_size(swap_block) / SECTORS_IN_FRAME;
	/* use that to initialize our swap table bitmap */
	swap_table = bitmap_create(swap_table_size);
	lock_init(&swap_lock);

}

/* 
looks into swap table and finds the first available swap slot
returns the sector index of that slot 

returns:
the sector index of an empty position in swap space
*/
block_sector_t 
swap_find_slot(void)
{
	// kevin 
	lock_acquire(&swap_lock);
	size_t free_slot_index = bitmap_scan(swap_table, 0, 1, false);
	/* if swap space fills up crash the kernel */
	ASSERT(free_slot_index != BITMAP_ERROR);
	/* mark the allocated space in swap */
	bitmap_mark(swap_table, free_slot_index);
	/* return the sector index in swap */
	lock_release(&swap_lock);
	return (block_sector_t)free_slot_index*SECTORS_IN_FRAME;
}

// Darren
/* 
writes a frame from physical memory 
into the swap space 

inputs:
frame - the frame to be written out to swap
swap_index - the sector index in swap space where the write should occur
*/
void 
swap_writeto(void* frame, block_sector_t swap_index)
{
	char* temp_frame = (char*)frame;
	size_t i = 0;
	for(; i < SECTORS_IN_FRAME; i++)
	{
		block_write(swap_block, swap_index+i, temp_frame);
		temp_frame += BLOCK_SECTOR_SIZE;
	}
}

// Darren
/* 
reads a frame from the swap space into
physical memory 

inputs:
frame - the frame to be read from swap
swap_index - the sector index in swap space where the read should occur
*/
void 
swap_readfrom(void* frame, block_sector_t swap_index)
{
	char* temp_frame = (char*)frame;
	size_t i = 0;
	for(; i < SECTORS_IN_FRAME; i++)
	{
		block_read(swap_block, swap_index+i, temp_frame);
		temp_frame += BLOCK_SECTOR_SIZE;
	}
}

// Kevin
/* 
remove an entry form the swap table
inputs:
swap_index - the index to remove
*/
void 
swap_remove(block_sector_t swap_index)
{
	bitmap_reset(swap_table, swap_index/SECTORS_IN_FRAME);
}
