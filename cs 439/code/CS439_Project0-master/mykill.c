#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>


int main(int argc, char **argv)
{
	//code provided in fib.c(error checking).
	if(argc < 2){
		fprintf(stderr, "Usage: mykill <num>\n");
		exit(-1);
	}

	// use kill system call to kill the process
	int process_to_kill = atoi(argv[1]);
	kill(process_to_kill , SIGUSR1);

	return 0;
}
