/*
 * msh - A mini shell program with job control
 *
 * <Put your name and login ID here>
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
#include "util.h"
#include "jobs.h"

/* Scott and Kevin defined consts */
#define OUTPUT_SIZE 50
#define NO_CMD 0
#define QUIT_CMD 1
#define JOBS_CMD 2
#define BG_CMD 3
#define FG_CMD 4
#define NULL_CMD 5

/* Global variables */
int verbose = 0;                    /* if true, print additional output */

extern char **environ;              /* defined in libc */
static char prompt[] = "msh> ";    /* command line prompt (DO NOT CHANGE) */
static struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

pid_t Fork();                                       /*wrapper for Fork*/
pid_t Waitpid(pid_t pid, int* status, int options); /*wrapper for waitpid*/
int argumentChecker(char **argv);                   /*check arguments for bgfg cmd*/


/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
void usage(void);
void sigquit_handler(int sig);





/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
        break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
        break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
        break;
    default:
            usage();
    }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

    /* Read command line */
    if (emit_prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
        app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
        fflush(stdout);
        exit(0);
    }

    /* Evaluate the command line */
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/
void eval(char *cmdline)
{
    // kevin driving
    /* store the command arguments */
    char *arguments[MAXARGS];   
    /* interpret user input */
    int background = parseline(cmdline, arguments); 
    pid_t child_id;
    /* is the command built in? */
    int builtin = builtin_cmd(arguments);   

    if(builtin == QUIT_CMD)
        exit(0);

    else if(builtin == JOBS_CMD)
        listjobs(jobs);

    else if (builtin == BG_CMD || builtin == FG_CMD)
        do_bgfg(arguments);

	else if(builtin == NULL_CMD) /* no commnd given */
		return;

    else{
        /* using a signal mask, block the SIGCHLD signal to handle race conditions */
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        /* create context for command to run in */
        child_id = Fork(); 
        if(child_id == 0){
            /* unblock the child in case it forks */
            sigprocmask(SIG_UNBLOCK, &mask, NULL); 
            /* give it a unique group id to protect the shell from SIGINT and SIGTSTP signals*/ 
            setpgid(0,0);   
            /* from bryant and o'hallaron book page 735.
             execute the command in the new context */
            if(execve(arguments[0],arguments, environ) < 0){    
                // command does not exist
                printf("%s: Command not found\n", arguments[0]);
                exit(0);
            }

        }

        // Scott Driving
        /* initialize the state of the new job */
        int state= background ? BG:FG;
        /* add the new job */                          
        addjob(jobs, child_id, state, cmdline);
        /* unblock the signal so that the shell can receive sigchld */
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        /* foreground processes wait*/
        if(!background)                                         
            waitfg(child_id);
        /* background processes print out info */
        else{                                                   
            printf("[%d] (%d) %s",
                pid2jid(jobs, child_id), child_id, cmdline);
        }
    }
    return;
}


/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 * Return 1 if a builtin command was executed; return 0
 * if the argument passed in is *not* a builtin command.
 */
int builtin_cmd(char **argv)
{

	if(!argv[0]){
		return NULL_CMD;
	}

    // kevin driving
    if(strcmp("quit", argv[0]) == 0)
        return QUIT_CMD;

    else if(strcmp("jobs", argv[0])== 0)
        return JOBS_CMD;

    else if(strcmp("bg", argv[0])== 0)
        return BG_CMD;

    else if(strcmp("fg", argv[0])== 0)
        return FG_CMD;

    else
        return NO_CMD;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 * args:
 *      argv = null terminated array of arguments
                should look like bg %1 or fg 5533
 */
void do_bgfg(char **argv)
{
    // Scott
    // error checking arguments
    if(!argumentChecker(argv))
        return;

    int background = strcmp("fg", argv[0]);
    pid_t pid;
    struct job_t* current_job;

    // JID arg
    if(*(argv[1]) == '%'){
        // get jid and then advance it past the '%'
        char* jid = argv[1];
        jid++;

        // get current job, if it doesn't exist return
        if(!(current_job = getjobjid(jobs, atoi(jid)))){
            printf("%s: No such job\n", argv[1]);
            return;
        }

        pid = current_job->pid;
    }

    // Kevin
    // PID arg
    else{
        // interpret pid from argument
        pid = atoi(argv[1]);

        // get current job, if it doesn't exist return
        if(!(current_job = getjobpid(jobs, pid))){
            printf("(pid): No such process\n");
            return;
        }
    }

    /*
     * send a CONT signal to the process group defined by the child's pid
     * adjust state to either FG or BG
     */
    kill(-1*pid, SIGCONT);
    if(background){
        current_job->state = BG;
        printf("[%d] (%d) %s", pid2jid(jobs, pid), pid, current_job->cmdline);
    }

    else{
        current_job->state = FG;
        waitfg(pid);
    }
}

/*
 * argumentChecker - checks the arguments given to bgfg to ensure that the
 *                  format of the arguments is fg/bg %jid/pid
 * args:
 *      argv = the null terminated array of arguments to be checked
 */
int argumentChecker(char **argv){
    //check size of arguments
    int argc = 0;
    while(*(argv+argc)){ argc++; } /* iterate through args till end is reached */
    if(argc < 2){
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return 0;
    }

    // Kevin
    // check second argument to ensure it is in pid or jid format
    char *arg1_pid = argv[1];
    char *arg1_jid = argv[1];
    arg1_jid++; /* advance jid past % */

    if(!atoi(arg1_pid) && !atoi(arg1_jid)){
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return 0;
    }

    return 1;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 * args:
 *      pid = the pid of the foreground process
 */
void waitfg(pid_t pid)
{
    // while there are foreground jobs sleep until interrupted
    while(fgpid(jobs)) /* fgpid returns 0 on failure to find fg job */
        sleep(1);
}


/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
    // Scott
    int status;                     /* status of the reaped child */
    pid_t pid;                      /* pid of the reaped child */
    char buffer[OUTPUT_SIZE];       /* buffer for handler response to shell */
    const int STDOUT = 1;

    /*
     * wait for a process that has either terminated or stopped
     * WNOHANG used so that when the waitset has only running
     * processes, the while loop exits
     */
    while((pid = Waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0) {

        struct job_t* reaped_child= getjobpid(jobs, pid);

        // if child exited normally
        if(WIFEXITED(status)) {
            deletejob(jobs, pid);
        }

        // Scott
        // if child terminated with signal
        else if(WIFSIGNALED(status)){
            /* response message */
            sprintf(buffer, "Job [%d] (%d) terminated by signal %d\n",
                reaped_child-> jid, reaped_child -> pid, WTERMSIG(status)); 

            /* get size of the string for the write command*/
            int size = strlen(buffer);
            /* safely write the string to the console */                  
            int bytes = write(STDOUT, buffer, size);    
            /* check to make sure the write finished correctly */
            if(bytes != size)                           
                exit(-999);
            /* delete terminated job */
            deletejob(jobs, pid);                       
        }

        // Kevin
        // if child stopped with signal
        else if (WIFSTOPPED(status)){
            /* response message */
            sprintf(buffer, "Job [%d] (%d) stopped by signal %d\n",
                reaped_child-> jid, reaped_child -> pid,WSTOPSIG(status)); 
             /* get size of the string for the write cmd */
            int size = strlen(buffer);
            /* safely write the string to the console */                 
            int bytes = write(STDOUT, buffer, size);
            /* change the job's state to ST */    
            reaped_child -> state =  ST;                

            /* error checking: make sure write finished */
            if(bytes != size)                           
                exit(-999);
        }
    }
}




/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    // Kevin
    pid_t pid = fgpid(jobs);     /* get the foreground job */
    if(pid){                     /* ensure there is a foreground process */
        kill(-1*pid,SIGINT);    /* kill everything in the foreground job's process group */
    }
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    // Kevin
    pid_t pid = fgpid(jobs);     /* get the foreground job */
    if(pid){                     /* ensure there is a foreground process */
        kill(-1*pid,SIGTSTP);   /* stop everything in the forground job's process group */
    }
}

/*********************
 * End signal handlers
 *********************/



/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    ssize_t bytes;
    const int STDOUT = 1;
    bytes = write(STDOUT, "Terminating after receipt of SIGQUIT signal\n", 45);
    if(bytes != 45)
       exit(-999);
    exit(1);
}

/* taken from Bryant & O'Hallaron CSAPP p. 718 */
pid_t Fork()
{
    pid_t pid;
    if((pid = fork()) < 0)
        unix_error("Fork error");

    return pid;
}

/*
* waitpid wrapper to clean up code
*/
pid_t Waitpid(pid_t child_id, int* child_status, int options)
{
    pid_t return_id = waitpid(child_id, child_status, options);

    if(return_id < 0 && errno != ECHILD){
        unix_error("waitpid error");
    }

    return return_id;
}
