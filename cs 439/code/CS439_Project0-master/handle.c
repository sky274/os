#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "util.h"


/*
 * First, print out the process ID of this process.
 *
 * Then, set up the signal handler so that ^C causes
 * the program to print "Nice try.\n" and continue looping.
 *
 * Finally, loop forever, printing "Still here\n" once every
 * second.
 */

// Handler code originally from the Project 0 assignment instructions
void handler(int sig){
	ssize_t bytes; 
	const int STDOUT = 1; 
	bytes = write(STDOUT, "Nice try.\n", 10); 
	if(bytes != 10)
		exit(-999);
}

// Handler for SIGUSR1. 
void user_handler(int sig){
	ssize_t bytes; 
	const int STDOUT = 1; 
	bytes = write(STDOUT, "exiting\n", 9); 
	if(bytes != 9)
		exit(-999);
	else{
		exit(1);
	}
}

int main(int argc, char **argv)
{
	struct timespec requested, remaining; /* declare and init timespec */
	requested.tv_sec= 1;
	
	pid_t current_pid= getpid();		/* output process's pid */
	printf("%d\n", current_pid);		/* w/ pid user can externally kill */
  
	Signal(SIGINT, handler);			/* install ctrl-c interrupt handler*/
	Signal(SIGUSR1, user_handler);		/*install sigusr1 interrupt handler */ 
	
	while(1){

		// if SIGINT interrupts sleep, continue for remaining time
  		if (nanosleep(&requested, &remaining) < 0 && errno== EINTR){
  			requested.tv_sec= remaining.tv_sec;
  			requested.tv_nsec= remaining.tv_nsec;
  		}
		
		// if sleep finishes in it's entirety reset time to 1 second
  		else{
  			requested.tv_sec = 1; 
  			requested.tv_nsec= 0; 		 
		}
	
		printf("Still here\n");			/* taunt the user */
	}

	return 0;
}



