#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/off_t.h"


/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

#define ROOT '/'
#define PATH_SEP '/'
/* tracing macros */
#define TRACE_FAIL 0			/* tracing a path failed */
#define TRACE_CREATE 1 			/* tracing a path succeeded except for last token
									signifies a create or mkdir call
									/a/b/c will leave dir b open with c available 
									to either create or mkdir */

#define TRACE_DIR_OPEN 2		/* path succeeded and last dir is open
									/a/b/c will have dir c open */

#define TRACE_FILE_OPEN	3		/* path succeeded and last dir open is the
									parent of end file
									/a/b/c has dir b open ready to open file c */

struct inode;

/* A directory. */
struct dir 
{
	struct inode *inode;                /* Backing store. */
	off_t pos;                          /* Current position. */
};

/* A single directory entry. */
struct dir_entry 
{
	block_sector_t inode_sector;        /* Sector number of header. */
	char name[NAME_MAX + 1];            /* Null terminated file name. */
	bool in_use;                        /* In use or free? */
};

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

/* path manipulation */
bool is_root(const char* path);
bool is_prev(const char* name);
bool is_curr(const char* name);
bool dir_is_empty(struct dir* dir_);
char* strdup(const char* src);


#endif /* filesys/directory.h */
