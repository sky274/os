/*
Seung Yoo
Scott Munro

jan/30/2015

finds the nth fibbonacci numbers using recursion and forking.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

const int MAX = 13;

static void doFib(int n, int doPrint);

/*
 * unix_error - unix-style error routine.
 */
inline static void 
unix_error(char *msg)
{
   fprintf(stdout, "%s: %s\n", msg, strerror(errno));
   exit(1);
}

/*
 * Wrapper function for fork to handle error checking
 * Originally from Byrant & O'Hallaron CSAPP pg.
 */
pid_t Fork(void)
{
	pid_t pid;
	if((pid = fork()) < 0)
		unix_error("Fork error");

	return pid;
}

/*
 * Handles error checking for waitpid and returns the child's
 * exit status.
 *
 * Essentially a wrapper function for waitpid
 */
int get_child_return(pid_t child_id, int* status, int options){
	if(waitpid(child_id, status, options) < 0)
	  unix_error("waitpid error");

   //retrieve the "return" value from the status of the child
   int child_ret = 0; 
   if(WIFEXITED(*status))
	  child_ret= WEXITSTATUS(*status); 
   
   // ERROR
   else{ 
	  printf("Child %d of process %d exited incorrectly.", child_id, getpid());
	  unix_error("WIFEXITED returned false");
   }

    return child_ret; 
}



int main(int argc, char **argv)
{
   int arg;
   //int print;

   if(argc != 2){
	  fprintf(stderr, "Usage: fib <num>\n");
	  exit(-1);
   }

	/*
   if(argc >= 3){
	  print = 1;
   }
	*/

   arg = atoi(argv[1]);
   if(arg < 0 || arg > MAX){
	  fprintf(stderr, "number must be between 0 and %d\n", MAX);
	  exit(-1);
   }

   doFib(arg, 1);

   return 0;
}

/* 
 * Recursively compute the specified number. If print is
 * true, print it. Otherwise, provide it to my parent process.
 *
 * NOTE: The solution must be recursive and it must fork
 * a new child for each call. Each process should call
 * doFib() exactly once.
 */
static void 
doFib(int n, int doPrint)
{
   // kevin
   // IDs of children to be obtained via fork
   pid_t child_1;
   pid_t child_2;

   // status's of children upon exiting
   int status_1;
   int status_2;

   /*
   BASE CASES
   if n = 0 then fibonnaci number is 0
   if n = 1 or 2 then fibonnaci number is 1

   if parent is called with base case value
   just print out base case
   */
   if (n==0){
	  if(doPrint){
		 printf("%d\n", n);
		 return;
	  }
	  exit(0);
   }

   if (n==1 || n==2){
	  if(doPrint){
		 printf("1\n");
		 return;
	  }
	  exit(1);
   }

   // create children and have child execute recursive call
   child_1 = Fork(); /* create first (n-1) child process */
   if (child_1 ==0)
	  doFib(n-1, 0);
   
   child_2 = Fork(); /* create second (n-2) child fib process */
   if (child_2 == 0)
	  doFib (n-2, 0);
   

   // Scott is Driving 
   //retrieve the "return" value from the status of the children
   int ret1 = get_child_return(child_1, &status_1, 0);  
   int ret2 = get_child_return(child_2, &status_2, 0);
   int returnvalue= ret1+ret2;

   // if the original parent, print final value
   if(doPrint)
	  printf("%d\n", returnvalue);
   
   // else return value back to parent  
   else
	  exit(returnvalue);
}



