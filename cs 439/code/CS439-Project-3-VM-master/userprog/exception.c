#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/pagesup.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "threads/vaddr.h"
#include "vm/swap.h"

/* defines the limit of how far the stack can grow */
#define STACK_LOWER_BOUND (((uint8_t *) PHYS_BASE) - 8388608)

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
static bool install_page (void *upage, void *kpage, bool writable);


/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{

  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;
  
  // scott
  /* if the user program faulted on a bad pointer (null or unmapped)
  terminate the program */
  if(user)
    verify_pointer(fault_addr);

  /* ensure that user programs are not trying to 
  write to unwritable pages */
  if(write && !not_present)
  {
    struct pagesup_entry* page = pagesup_find(thread_current(), fault_addr);
    /* if the page is not writeable, exit */
    if(!page->writable)
      safe_exit_process();
  }

  // darren
  /* lazy loading and stack growth */
  if(not_present)
  {
    struct pagesup_entry* page;
    struct frame_entry* new_frame;
    uint8_t* kpage;

    /* get the page from the page table */
    if((page = pagesup_find(thread_current(), fault_addr)) == NULL)
    {
      /* 
      criteria that a faulting address must meet for it to be allocated
      another page:
        * The faulting address MUST be a stack address
          * either the faulting address is within 32 bytes of the stack pointer (push)
          * or the faulting address is above the stack pointer (alreadyallocated)
        * the faulting address must be within the stack page limit (within bounds)
          * faulting address must be between phys_base and phys_base - 8MB
      */

      /* keep track of the old end of stack, it may be updated */
      void* old_eos = thread_current()->end_of_stack; 
      bool pushcheck = ((char*)fault_addr)+32 >= (char*)f->esp;
      bool alreadyallocated = f->esp < fault_addr || old_eos < fault_addr;
      bool withinbounds = (uint8_t*)fault_addr > STACK_LOWER_BOUND;

      if((pushcheck || alreadyallocated) && withinbounds)
      {
        /* allocate a page of memory */
        pagesup_add(NULL, 0, PGSIZE, fault_addr, true);
        page = pagesup_find(thread_current(), fault_addr);
        /* sanity check, just added the page should still be there */
        ASSERT( page != NULL);
        /* update the end of stack for this thread */
        thread_current()->end_of_stack = old_eos > page->upage ? page->upage : old_eos;
      }

      /* if the faulting address did not have an entry in the pagetable
      then it was an invalid access, kill the process */
      else
      {
        safe_exit_process();
      }
      
    }

    /* get a physical frame in memory to place the page */
    uint8_t* upage = page->upage;
    if((new_frame = get_frame(upage)) == NULL)
    {
      /* if a frame could be allocated, crash */
      safe_exit_process();
    }

    kpage = new_frame->phys_addr;

    /* check if frame is in swap space */
    if(page->swap_index != -1)
    {
      // scott
      /* write frame back to memory from swap space */
      swap_readfrom(kpage, page->swap_index);
      /* remove the slot from swap table */
      swap_remove(page->swap_index);
      /* indicate that the frame is no longer in swap space */
      page->swap_index = -1;

      /* inform the pagedirectory of the mapping */
      if(!install_page(upage, kpage, page->writable))
      {
        ASSERT(0 && "code loading failed install page");
      }

      /* unpin the new frame */
      new_frame->pin = false;

      /* restore the dirty bit to it's previous state */
      pagedir_set_dirty(thread_current()->pagedir, upage, page->dirty);
      return;
    }

    /* Is the loading page a code segment or a stack page ?*/
    /* stack page */
    // kevin
    else if(page->src == NULL)
    {
      /* allocate the stack page */
      /* install the stackpage */
      bool success = install_page(upage, kpage, true);
      /* unpin the new frame */
      new_frame->pin = false;
      ASSERT(success);
      return;
    }
    
    /* executable/code page or other */
    else
    {
      /* page info */
      struct file* src = page->src;
      off_t start = page->start;
      size_t length = page->length;
      size_t zero_length = PGSIZE - length;
      bool writable = page->writable;

      /* read the page data from disk into physical memory */
      /* must check if the thread already has the filesystem lock */
      bool has_lock = has_filesys_lock();
      if(!has_lock)
        acquire_filesys_lock();

      /* move the ofs in the file to the start of where we want to read */
      file_seek(src, start);
      /* read into our frame */
      int bytesread = file_read(src, kpage, length);

      /* if it didn't have the lock before the fault release it */
      if(!has_lock)
        release_filesys_lock();

      /* ensure the read was successful */
      if(bytesread != (int)length)
      {
        /* incomplete read, kill process */
        kpage = NULL;
        safe_exit_process();
      }

      /* write 0 to the rest of the page (or the whole page if demand zero) */
      memset(kpage+length, 0, zero_length);

      /* add the page to the process's address space */
      if(!install_page(upage, kpage, writable))
      {
        ASSERT(0 && "code loading failed install page");
      }

      /* unpin the new frame */
      new_frame->pin = false;

      return;
    }
  }
  
  /* if this call is reached then, fault was unresolved,
  kill process */
  safe_exit_process();
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

