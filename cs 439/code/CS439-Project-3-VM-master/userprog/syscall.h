#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/kernel/list.h"

void acquire_filesys_lock(void);
bool has_filesys_lock(void);
void release_filesys_lock(void);

struct file_elem
{
  const char* name; 		 // file name
  int fd; 					 // file descriptor
  struct file* file_pointer; // file pointer
  struct list_elem elem; 	 // list elem
};

void syscall_init (void);
void verify_pointer(void *pointer);
void safe_exit_process(void);


#endif /* userprog/syscall.h */
