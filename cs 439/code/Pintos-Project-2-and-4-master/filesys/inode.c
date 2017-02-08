#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* zero array container */
static char ZERO[BLOCK_SECTOR_SIZE];

/* minimum of non-negatives a and b */
static inline size_t
min(size_t a, size_t b)
{
  return a > b ? b : a;
}

/* functions used for deallocation and allocation */
static bool inode_disk_allocate(size_t sectors, struct inode_disk* disk_inode);
static void inode_disk_destroy(struct inode_disk* inode);
static void deallocate_direct_pointers(size_t sectors, block_sector_t* sector_con, size_t con_size);
static void deallocate_first_level_indirection(size_t sectors, block_sector_t indir_fir_index);
static void deallocate_second_level_indirection(size_t sectors, block_sector_t indir_sec_index);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{

  // Scott
  int pos_sectors = pos / BLOCK_SECTOR_SIZE;
  block_sector_t result;
  /* if file fits into direct pointers */
  if(pos_sectors < LEVEL0)
  {
    result = inode->data.direct[pos_sectors];
  }
  /* if file exceeds direct pointers but fits into single indirection block */
  else if(pos_sectors < LEVEL0 + LEVEL1)
  {
    block_sector_t indirect_pointers[LEVEL1];
    block_read(fs_device, inode->data.first, indirect_pointers);
    result = indirect_pointers[pos_sectors - LEVEL0];
  }
  /* if file exceeds single indirection block then it must be in double indrection block */
  else
  {
    /* pos found from second indirect block using 2D array indexing math
       i.e. ROW_INDEX * #ofCOLS + COL_INDEX = INDEX into 2D array
       the row in this case is the single indirection block that contains the pointer
       and the col is the index into that indirection block that stores the sector we
       are searching for */

    /* read the pointers to sectors containing single indirect pointers */
    // Scott
    block_sector_t second_indirect_pointers[LEVEL1];
    block_read(fs_device, inode->data.second, second_indirect_pointers);

    block_sector_t first_indirect_pointers[LEVEL1];
    pos_sectors = (pos_sectors - (LEVEL0 + LEVEL1));
    /* row = the single indirect block that contains the direct pointer to pos */
    /* column = index of the direct pointer to pos */
    int column_index = pos_sectors % LEVEL1;
    int row_index = (pos_sectors - column_index) / LEVEL1;
    block_read(fs_device, second_indirect_pointers[row_index], first_indirect_pointers);

    result = first_indirect_pointers[column_index];
  }

  return result == 0 ? (block_sector_t)-1 : result;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, unsigned isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      // darren
      if((success = inode_disk_allocate(sectors, disk_inode)))
      {
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
        disk_inode->isdir = isdir;
        block_write (fs_device, sector, disk_inode);
      }
      else
      {
        // scott
        inode_disk_destroy(disk_inode);
      }
      
      free (disk_inode);
    }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. 

(Does it?  Check code.)


   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          block_write(fs_device, inode->sector, &inode->data);
          free_map_release (inode->sector, 1);
          /* free all of the sectors allocate to the inode */
          // kevin
          inode_disk_destroy(&inode->data);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* if the end of the write is past the current EOF.. */
  if(size+offset > inode->data.length)
  {
    /* extend the file */
    // Scott 
    inode_disk_allocate(bytes_to_sectors(size+offset-inode->data.length), &inode->data);
    inode->data.length = size+offset;
    block_write(fs_device, inode->sector, &inode->data);
  }


  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

// darren
/* allocates "sectors" sectors for "disk_inode" */
static bool
inode_disk_allocate(size_t sectors, struct inode_disk* disk_inode)
{
  /* whether the allocation succeeded */
  bool success = true;

  /* count indicates the Nth sector of disk_inode */
  int count = bytes_to_sectors(disk_inode->length);
  ASSERT(count != -1);
  /* eof_pos indicates where we end up after the allocation */
  int eof_pos = count + sectors;

  /* the array of first level indirected pointers */
  // kevin
  block_sector_t* first = calloc(LEVEL1, sizeof (block_sector_t));
  if(first == NULL)
  {
    return false;
  }

  /* the array of first level indirection blocks inside the second level indirection block */
  block_sector_t* second_first = calloc(LEVEL1, sizeof (block_sector_t));
  if(second_first == NULL)
  {
    free(first);
    return false;
  }

  /* the array of second level indirected pointers */
  block_sector_t* second_second = calloc(LEVEL1, sizeof (block_sector_t));
  if(second_second == NULL)
  {
    free(first);
    free(second_first);
    return false;
  }

  /* go from the current sector to where we end up */
  while(count < eof_pos)
  {
    /* if we're in LEVEL0: using direct pointers */
    if(count < LEVEL0)
    {
      
      /* allocate a new sector with 0 data */
      if(!free_map_allocate(1, &disk_inode->direct[count]))
      {
        success = false;
        break;
      }

      block_write(fs_device, disk_inode->direct[count], ZERO);
    }

    // darren
    /* if we're in LEVEL1: using first level indirected pointers */
    else if(count < LEVEL0 + LEVEL1)
    {
      /* if the first level indirection block has not been allocated then allocate it */
      if(disk_inode->first == BLOCK_NULL)
      {
        if(!free_map_allocate(1, &disk_inode->first))
        {
          success = false;
          break;
        }
        block_write(fs_device, disk_inode->first, ZERO);
      }
      
      /* if we haven't read the array of first level indirected pointers yet, then read it */
      if(!first[0])
      {
        block_read(fs_device, disk_inode->first, first);
      }

      /* allocate a new sector with 0 data */
      if(!free_map_allocate(1, &first[count - LEVEL0]))
      {
        success = false;
        break;
      }

      block_write(fs_device, first[count - LEVEL0], ZERO);

      /* if this is the last sector being allocated, then write it to disk */
      if(count == eof_pos - 1 || count == LEVEL0 + LEVEL1 - 1)
      {
        block_write(fs_device, disk_inode->first, first);
      }
    }

    // darren
    /* if we're in LEVEL2: using second level indirected pointers */
    else
    {
      /* SANITY NOTE: Second level indirection block = array of first level indirection blocks */
      /* SANITY NOTE: First level indirection block = array of direct pointers */

      /* if the second level indirection block has not been allocated then allocate it */
      if(disk_inode->second == BLOCK_NULL)
      {
        if(!free_map_allocate(1, &disk_inode->second))
        {
          success = false;
          break;
        }

        block_write(fs_device, disk_inode->second, ZERO);
      }

      /* if the array of first level indirection blocks has not been read yet then read it */
      if(!second_first[0])
      {
        block_read(fs_device, disk_inode->second, second_first);
      }

      /* index of the first level indirection block inside the second level indirection block */
      int first_index = (count - LEVEL0 - LEVEL1) / LEVEL1; 

      /* if the first level indirection block inside the second level indirection has not been allocated then allocate it */
      if(second_first[first_index] == BLOCK_NULL)
      {
        if(!free_map_allocate(1, &second_first[first_index]))
        {
          success = false;
          break;
        }

        block_write(fs_device, second_first[first_index], ZERO);
      }

      /* if the array of second level indirected pointers has not been read yet then read it */
      if(!second_second[0])
      {
        block_read(fs_device, second_first[first_index], second_second);
      }

      /* index of the direct pointer inside the first level indirection block */
      int direct_index = (count - LEVEL0 - LEVEL1) % LEVEL1;

      /* allocate a new sector */
      if(!free_map_allocate(1, &second_second[direct_index]))
      {
        success = false;
        break;
      }

      block_write(fs_device, second_second[direct_index], ZERO);

      /* if this is the last direct pointer in the first level indirection block then write the first level indirection block to the disk */
      if(second_second[LEVEL1 - 1] != BLOCK_NULL)
      {
        block_write(fs_device, second_first[first_index], second_second);
      }

      /* if this is the sector we're allocating then write the second level indirection block to the disk */
      if(count == eof_pos - 1 || count == LEVEL0 + LEVEL1 + LEVEL2 - 1)
      {
        // Scott
        block_write(fs_device, second_first[first_index], second_second);
        block_write(fs_device, disk_inode->second, second_first);
      }

    }
    count++;
  }

  // Scott 
  free(first);
  free(second_first);
  free(second_second);

  return success;
}


// Scott 
/*
frees all of the sectors allocated to an inode

inputs:
inode - the inode on disk who's sectors need to be freed
*/
static void
inode_disk_destroy(struct inode_disk* inode)
{

  size_t sectors_to_free = bytes_to_sectors(inode->length);
  /* deallocate the direct pointers */
  size_t direct_sectors_to_free = min(sectors_to_free, LEVEL0);
  deallocate_direct_pointers(direct_sectors_to_free, inode->direct, LEVEL0);
  sectors_to_free -= direct_sectors_to_free;
  /* deallocate the first level indirection pointers */
  if(sectors_to_free > 0)
  {
    size_t indir_1_sectors_to_free = min(sectors_to_free, LEVEL1);
    deallocate_first_level_indirection(indir_1_sectors_to_free, inode->first);
    /* null out the old first_indirection sector index so it can't be used */
    inode->first = 0;
    sectors_to_free -= indir_1_sectors_to_free;

    /* deallocate the second level indirection pointers */
    if(sectors_to_free > 0)
    {
      size_t indir_2_sectors_to_free = min(sectors_to_free, LEVEL2);
      deallocate_second_level_indirection(indir_2_sectors_to_free, inode->second);
      sectors_to_free -= indir_2_sectors_to_free;
    }
  }

  ASSERT(sectors_to_free == 0);
}

// Scott
/*
deallocates a series of direct pointers for an inode struct's
sector container

inputs:
sectors - number of sectors to deallocate
sector_con - container for the sectors (could be direct[]
in the inode struct, or an indirection block)
con_size - size of the container (varies between indirection blocks and direct blocks)

*/
static void
deallocate_direct_pointers(size_t sectors, block_sector_t* sector_con, size_t con_size)
{
  /* ensure we never try to deallocate more than the
  maximum amount of sector indexes that will fit
  within one sector (i.e. 128 sector indexes)*/
  ASSERT(con_size <= LEVEL1);

  /* deallocate the direct pointers */
  size_t i=0;
  for(; i < con_size && i < sectors; i++)
  {
    /* dealllocate a sector */
    block_sector_t sector_to_release = *(sector_con+i);
    /*sanity check: ensure that the sector to free is valid */
    ASSERT(sector_to_release != 0);
    free_map_release(sector_to_release, 1);
    /* null out old entry */
    *(sector_con+i) = 0;
  }
}

// Scott 
/*
deallocates a first level indirection block and all of the data
pointers within

inputs:
sectors - the number of sectors to deallocate
indir_fir_index - the index of the first level indirection block on
*/

static void
deallocate_first_level_indirection(size_t sectors, block_sector_t indir_fir_index)
{

  /* sanity check to ensure the correct number of sectors is being asked for */
  ASSERT(sectors > 0 && sectors <= LEVEL1);

  /* allocate space (on the stack) to
  bring in the indirection block from disk */
  block_sector_t direct_block_deallocate[LEVEL1];
  /* read the indirection block into memory */
  block_read(fs_device, indir_fir_index, direct_block_deallocate);
  /* deallocate the sectors allocated inside of the indirection block */
  deallocate_direct_pointers(sectors, direct_block_deallocate, LEVEL1);
  /* deallocate the sector used for storing the indirection block itself */
  free_map_release(indir_fir_index, 1);

}

// Scott
/*
deallocates a second level indirection block and all of the data it references

inputs:
sectors - number of sectors to allocate to the second level indirection block
indir_sec_index - the sector index of the second level indirection block
*/
static void
deallocate_second_level_indirection(size_t sectors, block_sector_t indir_sec_index)
{
  ASSERT(sectors > 0 && sectors <= LEVEL2);
  block_sector_t sectors_left = sectors;

  /* allocate space on the stack to store the second level indirection block */
  block_sector_t sec_indirect_block[LEVEL1];
  block_sector_t* sec_indirect = &sec_indirect_block[0];
  /* read the second level block in from disk */
  block_read(fs_device, indir_sec_index, sec_indirect_block);

  /* free each of the first level indirection blocks within the
  second level indirection block */
  while(sectors_left > 0)
  {
    /* free the first LEVEL1 sectors or the remaining sectors left
    if sectors left is less than LEVEL1 */
    size_t sectors_to_free = min(sectors_left, LEVEL1);
    block_sector_t first_level_indir_sector = *sec_indirect;
    deallocate_first_level_indirection(sectors_to_free, first_level_indir_sector);
    /* decrement the number of sectors left to deallocate */
    sectors_left -= sectors_to_free;
    /* move to the next first level block entry in
    the second level indirection block */
    sec_indirect++;
  }

  /* free the sector storing the second level
  indirection block itself */
  free_map_release(indir_sec_index, 1);
}
