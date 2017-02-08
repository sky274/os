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
#include "devices/input.h"

#define MAX_SYSCALL_ARGS 3
#define FD_NOT_FOUND -1

// scott
static bool valid_pointer(void* pointer);
static struct file_elem* get_file(int fd);

// ~darrenvx
static void syscall_handler (struct intr_frame *f);
static void syscall_handler_halt (void);
static void syscall_handler_exit (int status);
static tid_t syscall_handler_exec (const char *cmd_line);
static int syscall_handler_wait(tid_t tid);
static int syscall_handler_write(int fd, const void *buffer, unsigned size);
static bool syscall_handler_create(const char* file, unsigned initial_size);
static int syscall_handler_open(const char *file);
static bool syscall_handler_remove (const char *file);
static int syscall_handler_filesize (int fd);
static void syscall_handler_close (int fd);
static int syscall_handler_read(int fd, void *buffer, unsigned size);
static void syscall_handler_seek(int fd, unsigned position);
static unsigned syscall_handler_tell(int fd);

static struct lock filesys_lock;

/* acquires the console lock from outside the syscall scope */
void 
acquire_filesys_lock()
{ 
  lock_acquire(&filesys_lock); 
}

bool 
has_filesys_lock()
{
  return filesys_lock.holder == thread_current();
}


void 
release_filesys_lock()
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
      /* verify the stack pointer that points to the first arg */
      verify_pointer(++stackpointer);
      /* get the first argument off the stack */
      syscall_args[0] = *stackpointer;
      break;

    /* two argument system calls */
    case SYS_CREATE:
    case SYS_SEEK:
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
  		syscall_handler_halt();
  	  break;
    }

  	case SYS_EXIT:
    {
      /* pass the status to the exit */
      int status = syscall_args[0];
  		syscall_handler_exit(status);
  	  break;
    }

    case SYS_EXEC:
    {
      /* verify that the pointer to the command line string is valid
      and then begin exec */
      const char* cmd_line = (char*)syscall_args[0];
      verify_pointer((void*) cmd_line);
      f->eax = syscall_handler_exec(cmd_line);
      break;
    }

    // ~darrenvx
    case SYS_WAIT:
    {
      tid_t tid = syscall_args[0];
      f->eax = syscall_handler_wait(tid);
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
      f->eax = syscall_handler_write(fd, buffer, size);
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
      f->eax = syscall_handler_create(filename, size);
      break;
    }

    case SYS_REMOVE:
    {
      char* filename = (char*)syscall_args[0];
      verify_pointer((void*)filename);
      f->eax = syscall_handler_remove(filename);
      break;
    }

    case SYS_OPEN:
    {
      char* filename = (char*)syscall_args[0];
      verify_pointer((void*)filename);
      f->eax = syscall_handler_open(filename);
      break;
    }

    case SYS_FILESIZE:
    {
      int fd = syscall_args[0];
      f->eax = syscall_handler_filesize(fd);
      break; 
    }

    case SYS_CLOSE:
    {
      int fd = syscall_args[0];
      syscall_handler_close(fd);
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
      f->eax = syscall_handler_read(fd, buffer, size);
      break;
    }

    case SYS_SEEK:
    {
      int fd = syscall_args[0];
      unsigned position = syscall_args[1];
      syscall_handler_seek(fd, position);
      break;
    }

    case SYS_TELL:
    {
      int fd = syscall_args[0];
      f->eax = syscall_handler_tell(fd);
      break;
    }

    default:
      ASSERT(0 && "syscall number not implemented");
  }
}

/* returns whether the pointer provided is a valid 
user address (below phys_base and not null*/
static bool valid_pointer(void* pointer){
  /* check that the pointer isn't null */
  if(pointer == NULL)
    return false;

  /* ...and that the pointer is a user virtual address... */
  if(!is_user_vaddr(pointer))
    return false;

  return true;
}

// kevin
/* used to safely kill processes 
outside of the syscall handler */
void 
safe_exit_process()
{
  syscall_handler_exit(-1);
}

/* verify that the user-provided pointer
is a valid user virtual memory address */
void verify_pointer(void *pointer){
  //scott
  /* pointer is invalid if...
  pointer given is null.. */
  if(!valid_pointer(pointer))
    syscall_handler_exit(-1);
}

// ~darrenvx
/* halt system call */
static void 
syscall_handler_halt(void)
{
	shutdown_power_off();
}

/* exit system call */
static void 
syscall_handler_exit(int status)
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

/* exec system call */
static tid_t 
syscall_handler_exec (const char *cmd_line)
{
  /* pass file_name to process_execute
  pid_t to tid_t is mapped 1 to 1, meaning that pid_t = tid_t
  returns the associated tid by setting the frame. 
  this will already be -1 if process fails to execute. */
  //scott
  return process_execute(cmd_line);
}

/* wait system call */
static int
syscall_handler_wait(tid_t tid){
  //scott
  return process_wait(tid); 
}

static int
syscall_handler_write(int fd, const void *buffer, unsigned size) 
{

  if(fd <= 0){
    /* if a thread tries to write to a negative fd or to stdin 
    that is a horrendous error. termiante immediately */
    syscall_handler_exit(-1);
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
      /* synchronization for write() */
      lock_acquire(&filesys_lock);
      /* write to the file */
      int bytes_written = file_write(file_fd->file_pointer, buffer, size);
      lock_release(&filesys_lock);
      return bytes_written;
    }

    /* if the fd not found return error */
    return FD_NOT_FOUND;
  }
}

static bool
syscall_handler_create(const char* file, unsigned initial_size)
{
  /* acquires the lock to ensure synchronization with the file system */
  lock_acquire(&filesys_lock);
  bool res = filesys_create(file, initial_size);
  lock_release(&filesys_lock);

  return res;
}

static int 
syscall_handler_open(const char *file)
{
  /* acquires the lock to ensure synchronization with the file system */
  int fd = -1;

  lock_acquire(&filesys_lock);
  struct file* file_pointer = filesys_open(file);
  lock_release(&filesys_lock);

  if(file_pointer != NULL)
  {
    struct file_elem *new_file_elem = palloc_get_page(PAL_ZERO);
    /* if out of memory/ can't palloc then terminate */
    if(new_file_elem == NULL)
    	return -1;
	
    /* initialize the new file element's values */
    new_file_elem->name = file;
    fd = thread_current()->fd_counter++;
    new_file_elem->fd = fd;
    new_file_elem->file_pointer = file_pointer;
    /* push new file element onto the file list */
    list_push_back(&thread_current()->filelist, &new_file_elem->elem);
  }
  
  return fd;
}

static bool 
syscall_handler_remove (const char *file)
{
  /* acquires the lock to ensure synchronization with the file system */
  lock_acquire(&filesys_lock);
  bool res = filesys_remove(file);
  lock_release(&filesys_lock);

  return res;
}

static int 
syscall_handler_filesize (int fd)
{
  /* tests for invalid (neg) file descriptors 
  and the stdin/out file descriptors */
  if(fd <= 1)
    syscall_handler_exit(0);

  /* retrieve the file associated with fd.. */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    /* return it's file size */
    return file_length(file_fd->file_pointer);
  }

  /* fd not found, exit 0 */
  syscall_handler_exit(-1);
  /* shut up the compiler */
  NOT_REACHED();
}

static void 
syscall_handler_close (int fd)
{
/* tests for invalid (neg) file descriptors 
  and the stdin/out file descriptors */
  if(fd <= 1)
    syscall_handler_exit(-1);

  /* retrieve the file associated with fd.. */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    /* remove the file from the list, close the file in the filesys ,
      and free the page associate with the file element */
    list_remove(&file_fd->elem);
    file_close(file_fd->file_pointer);
    palloc_free_page(file_fd);
    return;
  }

  /* fd not found, exit 0 */
  syscall_handler_exit(-1);
  NOT_REACHED();
}


static int 
syscall_handler_read(int fd, void *buffer, unsigned size)
{
  /* verify the entire buffer */
  /* if fd is STDOUT or negative, terminate */
  if(fd == 1 || fd < 0)
  {
    syscall_handler_exit(-1);
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
    int bytes_read = file_read(file_fd->file_pointer, buffer, size);
    lock_release(&filesys_lock);
    return bytes_read;
  }

  /* if the file wasn't found.. */
  return FD_NOT_FOUND;
}

static void
syscall_handler_seek(int fd, unsigned position)
{
  /* retrieve the file and seek */
  struct file_elem* file_fd = get_file(fd);
  if(file_fd != NULL)
  {
    lock_acquire(&filesys_lock);
    /* seek to the correct position */
    file_seek(file_fd->file_pointer, position);
    lock_release(&filesys_lock);
  }
}

static unsigned
syscall_handler_tell(int fd)
{
  /* retrieve the file */
  struct file_elem* file_fd = get_file(fd);

  /* if the file was found.. */
  if(file_fd != NULL)
  {
    lock_acquire(&filesys_lock);
    /* get the position of the next byte to be read */
    int pos = file_tell(file_fd->file_pointer);
    lock_release(&filesys_lock);
    return pos;
  }

  return FD_NOT_FOUND;
}

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

