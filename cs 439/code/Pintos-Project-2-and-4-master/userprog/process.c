#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"

/* constants defined for checking that the stack page does not overflow */
#define ARGS_PAGE_OVERFLOW -1
#define ARGS_PAGE_SIZE 4096

static thread_func start_process NO_RETURN;
static int parse_args(char * file_name);
static bool load (const char *cmdline, void (**eip) (void), void **esp, int num_args);
static void clear_childlist(void);
static void clear_filelist(void);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0); 
  if (fn_copy == NULL)
    return TID_ERROR;

  strlcpy (fn_copy, file_name, PGSIZE);

  /* get a copy of the filename to parse prior to calling 
  thread_create so that the thread will have the correct name 
  and the fn copy will be unadulterated */
  char *fn_copy2;
  fn_copy2 = palloc_get_page(0);
  if(fn_copy2 == NULL)
    return TID_ERROR;
  strlcpy (fn_copy2, file_name, PGSIZE);


  /* parse thread_name so that it is only the first 
  argument (i.e. the filename w/o args) */
  char* save_ptr;
  char* thread_name = strtok_r(fn_copy2, " ", &save_ptr);
     
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (thread_name, PRI_DEFAULT, start_process, fn_copy);

  /* ensure that the thread didn't fail in thread create or else
  nothing will be able to up the load sema */
  if(tid != TID_ERROR){
    /* synchronize exec by waiting until created thread 
    calls sema_up at the end of process start */
    sema_down(&thread_current()->load_sema);

    /* find current thread's child element from it's parent's childlist*/
    struct list* childlist = &thread_current()->childlist;
    /* the list should not be empty at this point because exec is syncronized
    to not return control until process exec finishes. There should be at least
    one child in the list (the one that was just created with thread_create) */
    ASSERT(!list_empty(childlist));
    struct list_elem* e;
    struct child_elem* matching_elem = NULL;
    for (e = list_begin(childlist); e != list_end(childlist); e = list_next(e)){
      matching_elem = list_entry(e, struct child_elem, elem);
      if(matching_elem->tid == tid)
        break;
    }

    if(!matching_elem->loaded)
      tid = TID_ERROR;
  }
  
  /* free the extra filename copy after the thread is created */
  palloc_free_page(fn_copy2);
    
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  int num_args = parse_args(file_name);
  /* parse args returned ARGS_PAGE_OVERFLOW then the load fails
  else retrieve the load success from the load function */
  success = num_args == ARGS_PAGE_OVERFLOW ? false : load (file_name, &if_.eip, &if_.esp, num_args);
  thread_current()->self->loaded = success;
  sema_up(&thread_current()->parent->load_sema);

  /* If load failed, quit. */
  palloc_free_page (file_name_);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.
*/
int
process_wait (tid_t child_tid) 
{
  // kevin
  // scott
  /* find the child element associated with child_tid*/
  struct list* childlist = &thread_current()->childlist;
  struct list_elem* e;
  struct child_elem* matching_elem;
  bool child_found = false;
  for (e = list_begin(childlist); e != list_end(childlist); e = list_next(e)){
    matching_elem = list_entry(e, struct child_elem, elem);
    if(matching_elem->tid == child_tid){
      child_found = true;
      break;
    }
  }

  /* if the tid was not associated with a child of this 
  thread then return -1 */
  if(!child_found)
    return -1;

  /* wait for the child to exit/die */
  sema_down(&matching_elem->dead_sema);
  /* retrieve it's status */
  int status = matching_elem->status;
  /* remove the child from the child list */
  list_remove(&matching_elem->elem);
  /* free the removed child element */
  palloc_free_page(matching_elem);
  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  //scott
  clear_childlist();
  clear_filelist();

  /* close the current working directory
  to prevent memory leaks */
  if(cur->cwd != ROOT_CWD) dir_close(cur->cwd);

  /* close this file's executable so that it can be modified again */
  acquire_filesys_lock();
  file_close(cur->exec);
  release_filesys_lock();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;

  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* 
when a thread closes it removes all of the entries in the child list
and frees all of the memory to  avoid leaks 
*/
static void
clear_childlist(){
  struct thread *cur = thread_current ();

  /* handle a parent thread dying while childlist isn't empty 
  making those child threads orphans */
  struct list* childlist = &cur->childlist;
  if(!list_empty(childlist))
  {
    /* iterate over entire list.. */
    struct list_elem* e = list_begin(childlist);
    while(e != list_end(childlist)){
      /* keep a temporary copy of the next element because we would lose
      it's reference we when remove the current element */
      struct list_elem* e_next = list_next(e);
      /* remove the current element from the child list */
      list_remove(e);
      /* get the child_elem associated with the element that was dynamically 
      allocated when the child was created */
      struct child_elem* child_e = list_entry(e, struct child_elem, elem);
      /* free the child_elem to prevent memory leaks */
      palloc_free_page(child_e);
      e = e_next;
    }
  }
}

/*
empties out the file list and closes all of the remaining
files to prevent leaks
*/
static void
clear_filelist(){
  struct thread *cur = thread_current ();

  /* frees the memory for the file list */
  struct list* filelist = &cur->filelist;
  if(!list_empty(filelist))
  {
    /* iterate over entire list.. */
    struct list_elem* e = list_begin(filelist);
    while(e != list_end(filelist)){
      /* keep a temporary copy of the next element because we would lose
      it's reference we when remove the current element */
      struct list_elem* e_next = list_next(e);
      /* remove the current element from the child list */
      list_remove(e);
      /* get the child_elem associated with the element that was dynamically 
      allocated when the child was created */
      struct file_elem* file_e = list_entry(e, struct file_elem, elem);
      /* free the child_elem to prevent memory leaks */
      struct file* file = file_e->file_p;
      if(file->inode->data.isdir){
        dir_close((struct dir*)file);
      }
      else{
        file_close(file);
      }
      palloc_free_page(file_e);
      e = e_next;
    }
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const char * file_name, int num_args);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* helper functions for setting up the stack*/
static void push_args(void **esp, const char * file_name, int num_args);
static char ** push_arg_addr(char * end_of_string, char ** addr_esp, int num_args);


/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp, int num_args) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();



  /* Open executable file. */
  acquire_filesys_lock();
  file = filesys_open (file_name); 
  release_filesys_lock();
  
  /* give this thread a reference to it's own executable */
  t->exec = file;

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  
  /* deny write access to this executable */
  file_deny_write(file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;

      acquire_filesys_lock();
      file_seek (file, file_ofs);
      int bytesread = file_read (file, &phdr, sizeof phdr);
      release_filesys_lock();

      if (bytesread != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, file_name ,num_args))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  acquire_filesys_lock();
  file_seek (file, ofs);
  release_filesys_lock();
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      acquire_filesys_lock();
      int bytesread = file_read (file, kpage, page_read_bytes);
      release_filesys_lock();
      if (bytesread != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const char * file_name, int num_args) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
      {
        *esp = PHYS_BASE;
        push_args(esp, file_name, num_args);
      }

      else
        palloc_free_page (kpage);
    }
  return success;
}

/* this function pushes the arguments defined in file_name onto the stack */
static void 
push_args(void **esp, const char * file_name, int num_args)
{
  int i;
  char * string_esp = (char *) (*esp);
  const char * current_argument = file_name;

  /* 
  PUSH STRINGS ONTO STACK
  */
  for(i = 0; i < num_args; i++)
  {
    /* move esp the length of the string+1 (for null terminating) */
    int string_length = strlen(current_argument) + 1;
    string_esp -= (string_length);
    /* copy string j onto the stack(memcpy) */
    memcpy(string_esp, current_argument, sizeof(char) *(string_length));

    /* advances to the next argument */
    current_argument += string_length;
    
    // scott
    /* account for extra spaces */
    while(*current_argument == ' ')
      current_argument++;
  }

  /*
  ALIGN STACK
  */
  char * end_of_string = string_esp; 
  /* putting alignment onto the stack */ 
  int alignment = ((int) string_esp) % (sizeof(char *));
  string_esp -= alignment;
  /* update original esp register */
  *esp = string_esp;

  /*
  ADD NULL TERMINATION FOR ARGV
  */
  /* adding the null termination for argv */
  char ** addr_esp = (char **) (*esp); 
  addr_esp --;
  *addr_esp = (char *)NULL;

  /*
  PUSH ADDRESS OF STRINGS ONTO STACK
  */
  /* pushes the address of the string arguments into addr_esp 
  and updates the stack pointer*/
  addr_esp = push_arg_addr(end_of_string, addr_esp, num_args);
  /* finally push the address of argv onto the stack */
  addr_esp --;
  /* addr_esp + 1 used because addr_esp was previously decremented 
  and we want to add the previous address */
  *addr_esp = (char*)(addr_esp + 1);
  /* update the original stack pointer */
  *esp = addr_esp;

  /* push argc onto the stack (num_args) */
  int* int_esp = (int*)(*esp);
  int_esp--;
  *int_esp = num_args;
  *esp = int_esp;

  /* push return address (fake placeholder) onto stack to keep 
  consistent stack structure */
  int ** pointer_esp = (int **)(*esp);
  pointer_esp --;
  *pointer_esp = NULL;
  *esp = pointer_esp;

}

static char **
push_arg_addr(char * end_of_string, char ** addr_esp, int num_args)
{ 
  /* this jumps over the first null terminating character*/
  int i;
  for(i = 0; i < num_args; i++)
  {
    /* allocate space on the stack to put the next addresss */
    addr_esp --; 
    /* put the address of the string argument on the stack */
    *addr_esp = end_of_string;
    /* get the size of the string argument and advance to the
    next string argument */
    end_of_string += strlen(end_of_string) + 1;
  }
  /* returns addr_esp so that it is updated outside of this scope */
  return addr_esp;
}

/* this function parses the commandline file_name for the executable and it's approperiate arguments */
static int
parse_args(char * file_name)
{
  /* checks what the eventual size of the stack will be
  throws error if necessary */
  int stacksize = 0;

  /* parsing the filename for the commandline and it's arguments */
  /* code taken from strtok_r function example in string.c */
  char *token, *save_ptr;
  int num_args = 0;
   for (token = strtok_r (file_name, " ", &save_ptr); token != NULL;
        token = strtok_r (NULL, " ", &save_ptr))
   {
    /* string parsed through calls to strok_r */
    /* num_args incremented everytime strtok parses a new token */
    num_args++;
    
    /* keep track of the eventual stack size */
    stacksize+=strlen(token)+1;
   }

   /* add the alignment to the stacksize check */
   stacksize += (sizeof(char*) - stacksize % sizeof(char*));

   /* account for the size of the argv pointers that will be pushed onto 
   the stack. extra arg for the null terminator argument */
   stacksize += sizeof(char*)*(num_args+1);

   /* account for argc and the return address being pushed onto the stack */
   stacksize += sizeof(int) + sizeof(void*);

   /* if the size of all of the arguments is larger than the alloted
   page size then return an error value for num_args */
   if(stacksize >= ARGS_PAGE_SIZE)
    num_args = ARGS_PAGE_OVERFLOW;

   return num_args;
}


/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
