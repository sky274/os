#ifndef SWAP_H
#define SWAP_H

#include "devices/block.h"
#include <kernel/bitmap.h>
#include "threads/vaddr.h"

#define SECTORS_IN_FRAME (PGSIZE/BLOCK_SECTOR_SIZE)

/* LIFECYCLE */
void swap_init(void);

/* SWAP TABLE MANIPULATION */
block_sector_t swap_find_slot(void);
void swap_writeto(void* frame, block_sector_t swap_index);
void swap_readfrom(void* frame, block_sector_t swap_index);
void swap_remove(block_sector_t);

#endif
