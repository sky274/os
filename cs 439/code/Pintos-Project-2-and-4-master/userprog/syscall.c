#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"
#include "lib/kernel/console.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "devices/input.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "filesys/free-map.h"

#define MAX_SYSCALL_ARGS 3
#define FD_NOT_FOUND -1
#define NO_DIR_WRITE -1

// scott
static bool valid_pointer(void* pointer);
static struct file_elem* get_file(int fd);

// ~darrenvx
static void syscall_handler (struct intr_frame *f);
static void syscall_halt (void);
static void syscall_exit (int status);
static tid_t syscall_exec (const char *cmd_line);
static int syscall_wait(tid_t tid);
static int syscall_write(int fd, const void *buffer, unsigned size);
static bool syscall_create(const char* file, unsigned initial_size);
static int syscall_open(const char *file);
static bool syscall_remove (const char *file);
static int syscall_filesize (int fd);
static void syscall_close (int fd);
static int syscall_read(int fd, void *buffer, unsigned size);
static void syscall_seek(int fd, unsigned position);
static unsigned syscall_tell(int fd);
static bool syscall_chdir(char* path);
static bool syscall_mkdir(char* path);
static bool syscall_readdir(int fd, char* name);
static bool syscall_isdir(int fd);
static int syscall_inumber(int fd);

static struct lock filesys_lock;

static char prev[3] = {'.','.','\0'};  
static char curr[2] = {'.','\0'};



/* acquires the console lock from outside the syscall scope */
void acquire_filesys_lock()
{ 
  lock_acquire(&filesys_lock); 
}


void release_filesys_lock()
{ 
  lock_release(&filesys_lock); 
}
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

/* parses the interrupt frame and then throws thread into
the correct system call */
static void
syscall_handler (struct intr_frame *f) 
{
  // ~darrenvx
  // scott
  /* make sure esp is a valid pointer and then
   get the system call off of the stack */
  verify_pointer(f->esp);

  int* stackpointer = f-> esp;
  int syscall_number = *stackpointer;

  /* an array to store all of the systemcall arguments 
  the arguments will be casted appropriately before
  being passed to the syscall handlers, but because
  they are all the same size it is okay to have them
  casted as ints*/
  int syscall_args[MAX_SYSCALL_ARGS] = {-1, -1, -1};

  // scott
  /* switch on the system call number to get the appropriate arguments */
  switch(syscall_number)
  {
    /* zero argument system calls */
    case SYS_HALT:
      break;

    /* one argument system calls */
    case SYS_EXIT:
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_CLOSE:
    // kevin
    case SYS_CHDIR:
    case SYS_MKDIR:
    case SYS_ISDIR:
    case SYS_INUMBER:
      /* verify the stack pointer that points to the first arg */
      verify_pointer(++stackpointer);
      /* get the first argument off the stack */
      syscall_args[0] = *stackpointer;
      break;

    /* two argument system calls */
    case SYS_CREATE:
    case SYS_SEEK:
    case SYS_READDIR:
      /* verify the stack pointer that points to the first arg*/
      verify_pointer(++stackpointer);
      /* gument off the stack */
      syscall_args[0] = *stackpointer;
      verify_pointer(++stackpointer);
      syscall_args[1] = *stackpointer;
      break;

    /* three argument system calls */
    case SYS_READ:
    case SYS_WRITE:
      /* verify the stack pointer that points to the first arg*/
      verify_pointer(++stackpointer);
      /* get the first argument off the stack */
      syscall_args[0] = *stackpointer;
      /*  and repeat for rest of args*/
      verify_pointer(++stackpointer);
      syscall_args[1] = *stackpointer;
      verify_pointer(++stackpointer);
      syscall_args[2] = *stackpointer;
      break;  

      
    default:
      ASSERT(0&&"NOT A SYSTEM CALL");

  }

  //kevin
  /* switch on the syscall to dispatch the arguments and exec the system call */
  switch(syscall_number)
  {
  	case SYS_HALT:
    {
  		syscall_halt();
  	  break;
    }

  	case SYS_EXIT:
    {
      /* pass the status to the exit */
      int status = syscall_args[0];
  		syscall_exit(status);
  	  break;
    }

    case SYS_EXEC:
    {
      /* verify that the pointer to the command line string is valid
      and then begin exec */
      const char* cmd_line = (char*)syscall_args[0];
      verify_pointer((void*) cmd_line);
      f->eax = syscall_exec(cmd_line);
      break;
    }

    // ~darrenvx
    case SYS_WAIT:
    {
      tid_t tid = syscall_args[0];
      f->eax = syscall_wait(tid);
      break;
    }

    case SYS_WRITE:
    {
      //scott
      //~darrenvx
      int fd = syscall_args[0];
      void* buffer = (void*)syscall_args[1];
      /*verify that the buffer points to valid memory */
      verify_pointer(buffer);
      unsigned size = syscall_args[2];
      /* return */
      f->eax = syscall_write(fd, buffer, size);
      break;
    }

    case SYS_CREATE:
    {
      //scott
      /* get the filename off of the stack */
      char* filename = (char*)syscall_args[0];
      verify_pointer((void*)filename);
      /* get the unsigned size argument */
      unsigned size = (unsigned)syscall_args[1];
      f->eax = syscall_create(filename, size);
      break;
    }

    case SYS_REMOVE:
    {
      char* filename = (char*)syscall_args[0];
      verify_pointer((void*)filename);
      f->eax = syscall_remove(filename);
      break;
    }

    case SYS_OPEN:
    {
      char* filename = (char*)syscall_args[0];
      verify_pointer((void*)filename);
      f->eax = syscall_open(filename);
      break;
    }

    case SYS_FILESIZE:
    {
      int fd = syscall_args[0];
      f->eax = syscall_filesize(fd);
      break; 
    }

    case SYS_CLOSE:
    {
      int fd = syscall_args[0];
      syscall_close(fd);
      break;  
    }

    case SYS_READ:
    {
      int fd = syscall_args[0];
      void* buffer = (void*)syscall_args[1];
      /*verify that the buffer points to valid memory */
      verify_pointer(buffer);
      unsigned size = (unsigned)syscall_args[2];
      /* return */
      f->eax = syscall_read(fd, buffer, size);
      break;
    }

    case SYS_SEEK:
    {
      int fd = syscall_args[0];
      unsigned position = syscall_args[1];
      syscall_seek(fd, position);
      break;
    }

    case SYS_TELL:
    {
      int fd = syscall_args[0];
      f->eax = syscall_tell(fd);
      break;
    }

    // Kevin
    case SYS_CHDIR:
    {
      char* dir = (char*)syscall_args[0];
      f->eax = syscall_chdir(dir);
      break;
    }

    case SYS_MKDIR:
    {
      char* dir = (char*)syscall_args[0];
      f->eax = syscall_mkdir(dir);
      break;
    }

    case SYS_READDIR:
    {
      int fd = syscall_args[0];
      char* name = (char*)syscall_args[1];
      f->eax = syscall_readdir(fd, name);
      break;
    }

    case SYS_ISDIR:
    {
      int fd = syscall_args[0];
      f->eax = syscall_isdir(fd);
      break; 
    }

    case SYS_INUMBER:
    {
      int fd = syscall_args[0];
      f->eax = syscall_inumber(fd);
      break;  
    }

    default:
      ASSERT(0 && "syscall number not implemented");
  }
}

/* 
ensures that a pointer is valid

inputs:
pointer - the pointer to check for validity

returns:
true if pointer is valid i.e. is not null,
is a user virtual address, and is mapped
 */
static bool valid_pointer(void* pointer){
  if(pointer == NULL)
  {
    return false;
  }
  /* ...or if the pointer is not a user virtual address... */
  if(!is_user_vaddr(pointer))
  {
    return false;
  }

  /* ...of if the address is not mapped to a page */
  if(pagedir_get_page(thread_current()->pagedir, pointer) == NULL)
  {
    return false;
  }

  return true;
}

/* 
verify that the user-provided pointer
is a valid user virtual memory address,

exit if verification fails

inputs:
pointer - pointer to verify
*/
void verify_pointer(void *pointer){
  //scott
  /* pointer is invalid if...
  pointer given is null.. */
  if(!valid_pointer(pointer))
    syscall_exit(-1);
}

// ~darrenvx
/* halt system call */
static void 
syscall_halt(void)
{
	shutdown_power_off();
}

/* exit system call 

inputs:
status - the exit status to inform the parent of
*/
static void 
syscall_exit(int status)
{
  //kevin
  /* if a child's parent has died before it, then the pointer referencing that
  parent should be an invalid pointer */
  bool orphaned = valid_pointer(thread_current()->parent);
  if(!orphaned)
  {
    //scott
    struct child_elem* matching_elem = thread_current()->self;
    /* update the thread's status*/
    matching_elem->status = status;
    /* marks the child is dead by upping it's semaphore*/
    sema_up(&matching_elem->dead_sema);
  }

  printf("%s: exit(%d)\n", thread_current()->name, status);
  /* free the thread's resources*/
	thread_exit ();
}

/* exec system call 

inputs:
cmd_line - the string command line to execute*/
static tid_t 
syscall_exec (const char *cmd_line)
{
  /* pass file_name to process_execute
  pid_t to tid_t is mapped 1 to 1, meaning that pid_t = tid_t
  returns the associated tid by setting the frame. 
  this will already be -1 if process fails to execute. */
  //scott
  return process_execute(cmd_line);
}

/* wait system call 

inputs: 
tid - the tid of the process to wait on*/
static int
syscall_wait(tid_t tid){
  //scott
  return process_wait(tid); 
}

/* write to a file

inputs:
fd - file descriptor of the file to write to
buffer - data to write to the file
size - length of data being written

returns:
number of bytes written to file
*/
static int
syscall_write(int fd, const void *buffer, unsigned size) 
{

  /* verify all of buffer where buffer points */
  const char* cbuf = (const char*)buffer;
  verify_pointer((void*)(cbuf+size));
  if(fd <= 0){
    /* if a thread tries to write to a negative fd or to stdin 
    that is a horrendous error. termiante immediately */
    syscall_exit(-1);
  }

  /* if fd is 1 then use putbuf. Otherwise, write to memory until EOF */
  if(fd == 1)
  {
    /* may need to split the buffer into smaller pieces if a test is failed */
    putbuf(buffer, (size_t)size);
    return size;
  }
  else
  {
    /* find the file associated with fd and ... */
    struct file_elem* file_fd = get_file(fd);
    if(file_fd != NULL)
    { 
      // Scott
      /* ensure that a directory is not being written to */
      if(file_fd->file_p->inode->data.isdir)
      {
        return NO_DIR_WRITE;
      }

      /* synchronization for write() */
      lock_acquire(&filesys_lock);
      /* write to the file */
      int bytes_written = file_write(file_fd->file_p, buffer, size);
      lock_release(&filesys_lock);
      return bytes_written;
    }

    /* if the fd not found return error */
    return FD_NOT_FOUND;
  }
}

/*
creates a file in the file system

inputs:
file - name of the file
initial size - the initial size of the file

returns true if the operation succeeds
*/
static bool
syscall_create(const char* file, unsigned initial_size)
{
  /* acquires the lock to ensure synchronization with the file system */
  lock_acquire(&filesys_lock);
  bool res = filesys_create(file, initial_size);
  lock_release(&filesys_lock);

  return res;
}


/*
opens a file in the file system
and appends the file to the threads list of open files

inputs:
file - name of the file to open
*/
static int 
syscall_open(const char *file)
{
  /* acquires the lock to ensure synchronization with the file system */
  int fd = -1;

  struct file_elem *new_file_elem = palloc_get_page(PAL_ZERO);
  /* if out of memory/ can't palloc then terminate */
  if(new_file_elem == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  struct file* file_p = filesys_open(file);
  lock_release(&filesys_lock);

  if(file_p != NULL)
  {
    /* initialize the new file element's values */
    new_file_elem->name = file;
    fd = thread_current()->fd_counter++;
    new_file_elem->fd = fd;
    new_file_elem->file_p = file_p;
    /* push new file element onto the file list */
    list_push_back(&thread_current()->filelist, &new_file_elem->elem);
  }
  return fd;
}

/*
removes the file from the file system

inputs:
file - name of file to remove
*/
static bool 
syscall_remove (const char *file)
{
  /* acquires the lock to ensure synchronization with the file system */
  lock_acquire(&filesys_lock);
  bool res = filesys_remove(file);
  lock_release(&filesys_lock);

  return res;
}

/*
returns the size of a file

inputs:
fd - file descriptor of the file

returns:
size of file fd
*/
static int 
syscall_filesize (int fd)
{
  /* tests for invalid (neg) file descriptors 
  and the stdin/out file descriptors */
  if(fd <= 1)
    syscall_exit(0);

  /* retrieve the file associated with fd.. */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    /* return it's file size */
    return file_length(file_fd->file_p);
  }

  /* fd not found, exit 0 */
  syscall_exit(-1);
  /* shut up the compiler */
  NOT_REACHED();
}

/*
close the file in the thread's file list denoted
by fd

inputs:
fd - descriptor for the file in the file list
*/
static void 
syscall_close (int fd)
{
/* tests for invalid (neg) file descriptors 
  and the stdin/out file descriptors */
  if(fd <= 1)
    syscall_exit(-1);

  /* retrieve the file associated with fd.. */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    /* remove the file from the list, close the file in the filesys ,
      and free the page associate with the file element */
    struct file* file = file_fd->file_p;
    list_remove(&file_fd->elem);
    if(file->inode->data.isdir)
      dir_close((struct dir*)file);
    else
      file_close(file);
    palloc_free_page(file_fd);
    return;
  }

  /* fd not found, exit 0 */
  syscall_exit(-1);
  NOT_REACHED();
}

/*
read from a file in the file sys into a buffer

inputs:
fd - file descriptor of file to be read
buffer - buffer to read into
*/
static int 
syscall_read(int fd, void *buffer, unsigned size)
{
  /* verify the entire buffer */
  const char* cbuf = (const char*)buffer;
  verify_pointer((void*)(cbuf+size));

  /* if fd is STDOUT or negative, terminate */
  if(fd == 1 || fd < 0)
  {
    syscall_exit(-1);
  }
  else if(fd == 0)
  {
    /* loop counter */
    unsigned x;
    uint8_t *int_buffer = (uint8_t *)buffer;
    /* read character from console "size" times, put each character into given buffer */
    for(x = 0; x < size; x++, int_buffer++) 
    {
     *int_buffer = input_getc();
    }
    ASSERT(x == size && "");
    return size;
  }

  /* search for the file associated with fd.. */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    lock_acquire(&filesys_lock);
    /* read character from console "size" times, put each character into given buffer */
    int bytes_read = file_read(file_fd->file_p, buffer, size);
    lock_release(&filesys_lock);
    return bytes_read;
  }

  /* if the file wasn't found.. */
  return FD_NOT_FOUND;
}

/*
move the file to the offset denoted by position 

inputs:
fd - file to move
position - offset to move the file to
*/
static void
syscall_seek(int fd, unsigned position)
{
  /* retrieve the file and seek */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    lock_acquire(&filesys_lock);
    /* seek to the correct position */
    file_seek(file_fd->file_p, position);
    lock_release(&filesys_lock);
  }
}

/*  tell the caller where the file 
is currently offsetted to */
static unsigned
syscall_tell(int fd)
{
  /* retrieve the file */
  struct file_elem* file_fd = get_file(fd);

  /* if the file was found.. */
  if(file_fd != NULL)
  {
    lock_acquire(&filesys_lock);
    /* get the position of the next byte to be read */
    int pos = file_tell(file_fd->file_p);
    lock_release(&filesys_lock);
    return pos;
  }

  return FD_NOT_FOUND;
}

// Darren
/*
Changes the current working directory to the directory specified by the path

inputs
path - a directory path i.e. /a/b/c/

returns
whether the directory was changed or not
*/
static bool 
syscall_chdir(char* path)
{
  /* the directory must not be empty */
  if(strlen(path)==0)
  {
    return false;
  }

  struct dir* dir;
  bool isabsolute = *path == ROOT;
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

  char* temp_path = strdup(path);

  struct inode* inode = NULL;
  char *token, *save_ptr;
  bool lookup_success = true;

  /* traverse each of the tokens in the path */
  /* code taken from strtok_r example in string.c comments above strtok_r*/
  // Scott
  for (token = strtok_r (temp_path, "/", &save_ptr); token != NULL && lookup_success;
  token = strtok_r (NULL, "/", &save_ptr))
  {
    if(!dir_lookup(dir, token, &inode) || !inode->data.isdir)
    {
      /* lookup failure */
      lookup_success = false;
    }

    else if(inode->data.isdir)
    {
      /* open the next directory to traverse */
      dir_close(dir);
      dir = dir_open(inode);
    }
  }

  free(temp_path);
  /* if the lookup failed return false */
  if(!lookup_success) 
    return false;

  /* free the old current working directory */
  if(thread_current()->cwd != ROOT_CWD)
    dir_close(thread_current()->cwd);

  /* assign the new cwd */
  thread_current()->cwd = dir;
  return true;

}

// Scott
/* create a new directory */
static bool
syscall_mkdir(char* path)
{
  /* the directory must not be empty */
  if(strlen(path)==0)
  {
    return false;
  }

  struct dir* dir;
  bool isabsolute = *path == ROOT;
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

  char* temp_path = strdup(path);

  struct inode* inode = NULL;
  char *token, *save_ptr, *last_token = "/";
  bool lookup_success = true;

  /* traverse each of the tokens in the path */
  /* code taken from strtok_r example in string.c comments above strtok_r*/
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
      /* open the next directory to traverse */
      dir_close(dir);
      dir = dir_open(inode);
    }

    else
    {
      break;
    }
  }

  // Scott
  /* failed before path finished tokenizing
  clean up resources and return */
  if(token != NULL && !lookup_success)
  {
    dir_close(dir);
    free(temp_path); 
    return false;
  }

  /* if the directory already exists, then fail */
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


  /* if it failed on the last token then mkdir
  is good to go ==> token == NULL && !lookup_success*/
  bool success = true;
  
  /* keep track of the parent sector's directory */
  block_sector_t parent_sector = dir->inode->sector;

  /* create the directory.. */
  /* get a sector */
  block_sector_t new_dir_sector;
  if(!free_map_allocate(1, &new_dir_sector))
  {
    printf("FALSE - allocation failed\n");
    success = false;
  }

  /* create an empty directory */
  else if(!dir_create(new_dir_sector, 2))
  {
    /* error clean up */
    printf("FALSE - create  failed\n");
    free_map_release(new_dir_sector, 1);
    success = false;
  }

  else
  {
    /* get the inode and dir of the newly created dir */
    struct inode* new_dir_node = inode_open(new_dir_sector);
    struct dir* new_dir = dir_open(new_dir_node);

    /* add the new empty directory to the hierarchy 
    i.e. for mkdir a/b/c add's dir c to dir b*/
    if(!dir_add(dir, last_token, new_dir_sector))
    {
      /* error clean up*/ 
      inode_close(new_dir_node);
      free_map_release(new_dir_sector, 1); 
      printf("FALSE - add failed \n");
      success = false;
    }

    /* add . and .. to the new directory */
    else if(!dir_add(new_dir, curr, new_dir_sector))
    {
      /* error clean up */
      dir_remove(dir, last_token);
      inode_close(new_dir_node);
      free_map_release(new_dir_sector, 1); 
      printf("FALSE - add . failed\n");
      success = false;
    }

    else if(!dir_add(new_dir, prev, parent_sector))
    {
      /* error clean up */
      dir_remove(dir, last_token);
      inode_close(new_dir_node);
      free_map_release(new_dir_sector, 1); 
      printf("FALSE - add .. failed\n");
      success = false;
    }
    /* succeeds to create node */
    else
    {
      dir_close(new_dir);
    }
  }
  
  free(temp_path);
  dir_close(dir);

  return success;
}

/*
Reads a directory entry from file descriptor fd, which must represent a directory. 
If successful, stores the null-terminated file name in name, which must have room for READDIR_MAX_LEN + 1 bytes, and returns true.
 If no entries are left in the directory, returns false.
. and .. should not be returned by readdir.
*/
// Kevin
static bool 
syscall_readdir(int fd, char* name)
{
  /* get the directory from the filelst */
  struct file_elem* file_e = get_file(fd);
  struct dir* dir = (struct dir*)file_e->file_p;
   /* if the file-fd is not a directory return false */
  if(!dir->inode->data.isdir)
    return false;

  ASSERT(dir != NULL);

  bool read_success;
  bool success = false;

  /* loop through each entry in the dir, putting the entry's value in name */
  for(read_success = dir_readdir(dir, name); read_success; read_success = dir_readdir(dir, name)){
    /* . and .. should not be returned */
    if(!is_prev(name) && !is_curr(name)){
      success = true;
      break;
    }
  }
  
  return success;
}

/*
Check if the file identified by the fd
is a directory or not 
inputs:
fd - file to check
returns:
whether the file is a directory
*/
// kevin
static bool 
syscall_isdir(int fd)
{
  struct file_elem* current_file = get_file(fd);
  return current_file != NULL ? (bool)current_file->file_p->inode->data.isdir : false;
}

/*
Get the inode number of the file described by fd
inputs:
fd - id of the file
returns:
the inode of the file
*/
// kevin
static int
syscall_inumber(int fd)
{
  struct file_elem* current_file = get_file(fd);
  return current_file != NULL ? (int)current_file->file_p->inode->sector : FD_NOT_FOUND;
}

/* 
get the file element associated with a file descriptor

inputs:
fd - file descriptor to search for in the thread's list 
of open files

returns:
a pointer to the file elem if found, NULL otherwise
*/
static struct file_elem* 
get_file(int fd)
{
  /* get current thread's file list */
  struct list* filelist = &thread_current()->filelist;
  /* loop through the file list */
  struct list_elem* e = list_begin(filelist);
  while(e != list_end(filelist))
  {
    struct file_elem* file_e = list_entry(e, struct file_elem, elem);
    /* check to see if this is the file descriptor we're looking for, then read */
    if(file_e->fd == fd)
    {
      return file_e;
    }
    e = list_next(e);
  }

  return NULL;
}
