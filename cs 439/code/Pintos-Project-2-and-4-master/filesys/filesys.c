#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}


// Scott
/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{

  /* the directory must not be empty */
  if(strlen(name)==0)
  {
    return false;
  }

  struct dir* dir;
  bool isabsolute = *name == ROOT;
  /* if the path is absolute... */
  if(isabsolute)
  {
    /* start at the root */
    dir = dir_open_root();
  }
  else
  {
    /* else start a at a copy of the current working directory */
    dir = thread_current()->cwd == ROOT_CWD ? dir_open_root() : dir_reopen(thread_current()->cwd);
  }

  char* temp_path = strdup(name);

  struct inode* inode = NULL;
  char *token, *save_ptr, *last_token = "/";
  bool lookup_success = true;

  /*iterating over each token of the path */
  for (token = strtok_r (temp_path, "/", &save_ptr); token != NULL && lookup_success;
  token = strtok_r (NULL, "/", &save_ptr))
  {
    last_token = token;
    if(!dir_lookup(dir, token, &inode))
    {
      lookup_success = false;
    }

    else if(inode->data.isdir)
    {
      dir_close(dir);
      dir = dir_open(inode);
    }

    else
    {
      break;
    }
  }

  /* failed before path finished tokenizing
  clean up resources and return */
  if(token != NULL && !lookup_success)
  {
    dir_close(dir);
    free(temp_path); 
    return false;
  }

  /* if the file already exists, then fail */
  if(token == NULL && lookup_success)
  {
    dir_close(dir);
    free(temp_path);
    return false;
  }

  /* if it broke out of loop, tried to
  traverse through a file */
  if(token != NULL && lookup_success)
  {
    dir_close(dir);
    inode_close(inode);
    free(temp_path);
    return false;
  }

  block_sector_t inode_sector = 0;

  /* when the correct directory and final token are found
  add it to the directory structure */
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, FILE)
                  && dir_add (dir, last_token, inode_sector));
  
  /* if the add fails, release the sector */
  if (!success && inode_sector != 0){ 
    free_map_release (inode_sector, 1);
  }

  dir_close(dir);
  free(temp_path);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
// Scott
struct file *
filesys_open (const char *name)
{

  /* the directory must not be empty */
  if(strlen(name)==0)
  {
    return NULL;
  }

  struct dir* dir;
  bool isabsolute = *name == ROOT;
  /* if the directory is absolute.. */
  if(isabsolute)
  {
    /* start at root */
    dir = dir_open_root();
    /* .. and if the path is just the root */
    if(is_root(name))
    {
      /* return the root directory */
      return (struct file*)dir;
    }
  }

  else
  {
    /* else assign to the current working directory */
    dir = thread_current()->cwd == ROOT_CWD ? dir_open_root() : dir_reopen(thread_current()->cwd);
  }

  char* temp_path = strdup(name);

  struct inode* inode = NULL;
  char *token, *save_ptr, *last_token = "/";
  bool lookup_success = true;
  bool found_file = false;

  /*iterating over each token of the path */
  for (token = strtok_r (temp_path, "/", &save_ptr); token != NULL && lookup_success && !found_file;
  token = strtok_r (NULL, "/", &save_ptr))
  {
    last_token = token;

    if(!dir_lookup(dir, token, &inode))
    {
      lookup_success = false;
    }

    else if(inode->data.isdir)
    {
      dir_close(dir);
      dir = dir_open(inode);
    }
    else
    {
      found_file = true;
    }
  }

  /* didn't finish traversing the path, FAILURE */
  if(token != NULL || !lookup_success)
  {
    dir_close(dir);
    free(temp_path);
    if(found_file)
      inode_close(inode);

    return NULL;
  }

  struct file* file;

  /* if the file is a directory.. */
  if(inode->data.isdir)
  {
    /* return the directory */
    file = (struct file*)dir;
  }

  else
  {
    /* else open the file associated with 
    the final inode */
    dir_close(dir);
    file = file_open(inode);
  }

  free(temp_path);
  return file;
}

// Kevin
/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  /* the directory must not be empty */
  if(strlen(name)==0)
  {
    return NULL;
  }

  /* cannot remove ROOT */
  if(is_root(name))
  {
    return NULL;
  }

  struct dir* dir;
  bool isabsolute = *name == ROOT;
  if(isabsolute)
    dir = dir_open_root();
  else
    dir = thread_current()->cwd == ROOT_CWD ? dir_open_root() : dir_reopen(thread_current()->cwd);

  /* duplicate the path to a mutable path */
  char* temp_path = strdup(name);

  struct inode* inode = NULL;
  char *token, *save_ptr, *last_token = "/";
  bool lookup_success = true;
  bool found_file = false;

  /*iterating over each token of the path */
  for (token = strtok_r (temp_path, "/", &save_ptr); token != NULL && lookup_success && !found_file;
  token = strtok_r (NULL, "/", &save_ptr))
  {
    last_token = token;

    if(!dir_lookup(dir, token, &inode))
    {
      lookup_success = false;
    }

    else if(inode->data.isdir)
    {
      dir_close(dir);
      dir = dir_open(inode);
    }
    else
    {
      found_file = true;
    }
  }

  /* didn't finish traversing the path, FAILURE */
  if(token != NULL || !lookup_success)
  {
    dir_close(dir);
    free(temp_path);
    if(found_file)
      inode_close(inode);

    return false;
  }

  /* removing a directory ? */
  if(inode->data.isdir)
  {
    /* check if the directory is empty */
    if(dir_is_empty(dir) && inode->open_cnt < 2)
    {
      /* is the directory the current working directory ? */
      block_sector_t cwd_sector = thread_current()->cwd == ROOT_CWD ? ROOT_DIR_SECTOR : thread_current()->cwd->inode->sector;
      if(cwd_sector == dir->inode->sector)
      {
        /* fail if it is */
        dir_close(dir);
        free(temp_path);
        return false;
      }

      else
      {
        /* else lookup the directory's parent
        and remove the directory from it's parent's list of entries */
        struct inode* parent_node;
        bool success = dir_lookup(dir, "..", &parent_node);
        ASSERT(success);
        struct dir* parent = dir_open(parent_node);
        success = dir_remove(parent, last_token);
        dir_close(parent);
        ASSERT(success);
      }
    }

    else
    {
      /* if the directory is not empty, FAILURE */
      dir_close(dir);
      free(temp_path);
      return false;
    }
  }

  else
  {
    /* if file is to be removed,
    just remove it from the last open directory */
    dir_remove(dir, last_token);
  }

  dir_close(dir);
  free(temp_path);
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
