/* 
 * tsh - A tiny shell program with job control
 * 
 * WEN, Zhaohe 10205501432
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <curses.h>
#include <limits.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <assert.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/type.h>
#include <minix/endpoint.h>
#include <minix/const.h>
#include <minix/u64.h>
#include <paths.h>
#include <minix/procfs.h>
#include <sys/stat.h>  
#include <sys/types.h> 
#include <sys/wait.h>  
#include <sys/times.h>
#include <sys/time.h>
#include <sys/select.h>

/* mytop */
#define TIMECYCLEKEY 't'
#define ORDERKEY 'o'

#define ORDER_CPU	0
#define ORDER_MEMORY	1
#define ORDER_HIGHEST	ORDER_MEMORY

typedef int endpoint_t;
typedef uint64_t u64_t;
typedef long unsigned int vir_bytes;

int order = ORDER_CPU;

u32_t system_hz;

#define MAX_NR_TASKS 1023
#define SELF    ((endpoint_t) 0x8ace)
#define _MAX_MAGIC_PROC (SELF)
#define _ENDPOINT_GENERATION_SIZE (MAX_NR_TASKS+_MAX_MAGIC_PROC+1)
#define _ENDPOINT_P(e) \
((((e)+MAX_NR_TASKS) % _ENDPOINT_GENERATION_SIZE) - MAX_NR_TASKS)
#define  SLOT_NR(e) (_ENDPOINT_P(e) + 5)
#define _PATH_PROC /proc
#define CPUTIME(m, i) (m & (1 << (i)))  

#define  TC_BUFFER  1024        /* Size of termcap(3) buffer    */
#define  TC_STRINGS  200        /* Enough room for cm,cl,so,se  */

char *Tclr_all;

int blockedverbose = 0;

#define USED 0x1
#define IS_TASK 0x2
#define IS_SYSTEM 0x4
#define BLOCKED 0x8
#define PSINFO_VERSION 0

#define STATE_RUN 'R'
const char *cputimenames[] = {"user", "ipc", "kernelcall"};
#define CPUTIMENAMES ((sizeof(cputimenames)) / (sizeof(cputimenames[0])))                                  
int n_his = 0;
char *path = NULL;
unsigned int nr_procs, nr_tasks;
int slot = -1;
int nr_total;

struct proc
{
    int p_flags;
    endpoint_t p_endpoint;           
    pid_t p_pid;                     
    u64_t p_cpucycles[CPUTIMENAMES]; 
	int p_priority;                  
    endpoint_t p_blocked;            
    time_t p_user_time;              
    vir_bytes p_memory;              
    uid_t p_effuid;                  
    int p_nice;                      
    char p_name[PROC_NAME_LEN + 1];  
};

struct proc *proc = NULL, *prev_proc = NULL;

struct tp
{
    struct proc *p;
    u64_t ticks;
};

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
char commandHistory[1000][MAXLINE];/* storing command history */
int commandHistoryNumber = 0;/* how many records are stored in commandHistory */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

void parse_file(pid_t pid); 
void parse_dir(void);
int print_memory(void);
u64_t cputicks(struct proc *p1, struct proc *p2, int timemode);
void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode);
void get_procs();
void getkinfo();

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

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
	if (commandHistoryNumber>=1000)
	{
		for (int i=0;i<999;i++)
		{
			strcpy(commandHistory[i],commandHistory[i+1]);
		}
		commandHistoryNumber=999;
	}
	strcpy(commandHistory[commandHistoryNumber],cmdline);
	commandHistoryNumber++;
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
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int is_pipe=0;
    int bg;
    int state;
    pid_t pid;
    sigset_t mask_all,mask_one,prev;
    strcpy(buf,cmdline);
    bg = parseline(buf,argv);
    if(argv[0]==NULL)
    {
	    return;
    }
    state = bg ? BG : FG;
    sigfillset(&mask_all);
    sigemptyset(&mask_one);
    sigaddset(&mask_one,SIGCHLD);
    if(!builtin_cmd(argv))
    {
	    sigprocmask(SIG_BLOCK,&mask_one,&prev);
	    if((pid = fork())==0)
	    {
		    sigprocmask(SIG_SETMASK,&prev,NULL);
		    if(setpgid(0,0)<0)
		    {
			    unix_error("Setpid error");
			    exit(0);
		    }
		    for (int i=0;argv[i]!=NULL;i++)
		    {
			    if (!strcmp(argv[i],">"))
			    {
				    if(argv[i+1]==NULL)
				    {
					    perror("No output file!");
					    exit(1);
				    }
				    else
				    {
					    argv[i]=NULL;
					    int fd = creat(argv[i+1],0666);
					    dup2(fd,1);
					    close(fd);
					    break;
				    }
			    }
		    }
		    for (int i=0;argv[i]!=NULL;i++)
		    {
			    if (!strcmp(argv[i],">>"))
			    {
				    if(argv[i+1]==NULL)
				    {
					    perror("No output file!");
					    exit(1);
				    }
				    else
				    {
					    argv[i]=NULL;
					    int fd = open(argv[i+1],O_RDWR|O_APPEND,0666);
					    dup2(fd,1);
					    close(fd);
					    break;
				    }
			    }
		    }
		    for (int i=0;argv[i]!=NULL;i++)
                    {
                            if (!strcmp(argv[i],"<"))
                            {
                                    if(argv[i+1]==NULL)
                                    {
                                            perror("No output file!");
                                            exit(1);
                                    }
                                    else
                                    {
                                            argv[i]=NULL;
                                            int fd = open(argv[i+1],O_RDONLY);
                                            dup2(fd,0);
                                            close(fd);
                                            break;
                                    }
                            }
                    }
		    for (int i=0;argv[i]!=NULL;i++)
		    {
			    char *program1[MAXARGS];
			    char *program2[MAXARGS];
			    for (int ii=0;ii<MAXARGS;ii++)
			    {
				    program1[ii]=NULL;
				    program2[ii]=NULL;
			    }
			    if (!strcmp(argv[i],"|"))
			    {
				is_pipe++;
				for (int ii=0;ii<i;ii++)
				{
					program1[ii]=argv[ii];
				}
				for (int ii=i+1;argv[ii]!=NULL;ii++)
				{
					program2[ii-i-1]=argv[ii];
				}
				int fd[2];
				int pip = pipe(&fd[0]);
				pid_t pid2;
				if((pid2=fork())==0)
				{
					close(fd[0]);
					dup2(fd[1],1);
					execvp(program1[0],program1);
				}
				else
				{
					wait(NULL);
				}
				close(fd[1]);
				dup2(fd[0],0);
				execvp(program2[0],program2);
				break;
			    }
		    }
		    if(!is_pipe)
		    {
		    	if(execvp(argv[0],argv)<0)
		    	{
			    printf("%s: Command not found\n", argv[0]);
			    exit(0);
		    	}
		    }
	    }
	    else
	    {
		    if(state==BG)
		    {
			    sigprocmask(SIG_BLOCK,&mask_all,NULL);
			    addjob(jobs, pid, state, cmdline);
			    sigprocmask(SIG_SETMASK,&mask_one,NULL);
			    printf("[%d] (%d) %s",pid2jid(pid), pid, cmdline);
		    }
		    else
		    {
			    sigprocmask(SIG_BLOCK,&mask_all,NULL);
                            addjob(jobs, pid, state, cmdline);
                            sigprocmask(SIG_SETMASK,&mask_one,NULL);
			    waitfg(pid);
		    }
		    sigprocmask(SIG_SETMASK, &prev, NULL); 
	    }
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/*
 * parse_file - Read the information of every pid and judge whether they can be used
 */
void parse_file(pid_t pid)
{
    char path[PATH_MAX], name[256], type, state;
    int version, endpt, effuid; 
    unsigned long cycles_hi, cycles_lo; 
    FILE *fp;
    struct proc *p;
    int i;
    
    sprintf(path, "/proc/%d/psinfo", pid);
	//open /proc/pid/psinfo
    if ((fp = fopen(path, "r")) == NULL)
    {
        return;
    }
	//read version
    if (fscanf(fp, "%d", &version) != 1)
    {
        fclose(fp);
        return;
    }
	//check version fault
    if (version != PSINFO_VERSION)
    {
        fputs("procfs version mismatch!\n", stderr);
        exit(1);
    }
	//read type and endpoint
    if (fscanf(fp, " %c %d", &type, &endpt) != 2)
    {
        fclose(fp);
        return;
    }

    slot++; 
	//check whether the value of endpoint is reasonable
    if (slot < 0 || slot >= nr_total)
    {
        fprintf(stderr, "mytop:unreasonable endpoint number %d\n", endpt);
        fclose(fp);
        return;
    }
	//get the address of process in sequence
    p = &proc[slot]; 
	//mark task or sequence process
    if (type == TYPE_TASK)
    {
        p->p_flags |= IS_TASK; 
    }
    else if (type == TYPE_SYSTEM)
    {
        p->p_flags |= IS_SYSTEM; 
    }
    //save endpoint and pid
    p->p_endpoint = endpt;
    p->p_pid = pid;
    //read name, state,etc.
    if (fscanf(fp, " %255s %c %d %d %lu %*u %lu %lu",
               name, &state, &p->p_blocked, &p->p_priority,
               &p->p_user_time, &cycles_hi, &cycles_lo) != 7)
    {
        fclose(fp);
        return;
    }
    //save name
    strncpy(p->p_name, name, sizeof(p->p_name) - 1);
    p->p_name[sizeof(p->p_name) - 1] = 0;
	//mark block
    if (state != STATE_RUN)
    {
        p->p_flags |= BLOCKED; 
    }

    //save CPU cycles
    p->p_cpucycles[0] = make64(cycles_lo, cycles_hi);
    p->p_flags |= USED; 

    fclose(fp);
}

/*
 * parse_dir - analize all the pid-s and call parse_file for each of them 
 */
void parse_dir(void)
{
    DIR *p_dir;
    struct dirent *p_ent;
    pid_t pid;
    char *end;
    // open /proc
    if ((p_dir = opendir("/proc/")) == NULL) 
	{
        perror("opendir on /proc");
        exit(1);
    }
    //analyze all pid
    for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir))
    {
        pid = strtol(p_ent->d_name, &end, 10);
        if (!end[0] && pid != 0)
        {
            //call parse_file for each pid
			parse_file(pid);
        }
    }
    closedir(p_dir);
}

/*
 * print_memory - Print memory status 
 */
int print_memory(void)
{
	FILE *fp;
	unsigned int pagesize;
	unsigned long total, free, largest, cached;
	//open /proc/meminfo
	if ((fp = fopen("/proc/meminfo", "r")) == NULL)
		return 0;
	//get memory status
	if (fscanf(fp, "%u %lu %lu %lu %lu", &pagesize, &total, &free,
			&largest, &cached) != 5) {
		fclose(fp);
		return 0;
	}

	fclose(fp);
	//print memory status
	printf("main memory: %ldK total, %ldK free, %ldK cached\n",
		(pagesize * total)/1024, (pagesize * free)/1024,(pagesize * cached)/1024);

	return 1;
}

/*
 * cputicks - caculate ticks of all processes
 */
u64_t cputicks(struct proc *p1, struct proc *p2, int timemode)
{
    int i;
    u64_t t = 0;
    //caculate ticks for each process and compare it with prev_proc
    for (i = 0; i < CPUTIMENAMES; i++)
    {
        if (!CPUTIME(timemode, i))
        {
            continue;
        }
        if (p1->p_endpoint == p2->p_endpoint)
        {
            t = t + p2->p_cpucycles[i] - p1->p_cpucycles[i];
        }
        else
        {
            t = t + p2->p_cpucycles[i];
        }
    }
    return t;
}


/*
 * print_procs - print CPU status
 */
void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode)
{
    int p, nprocs;
    u64_t systemticks = 0;
    u64_t userticks = 0;
    u64_t total_ticks = 0;
    u64_t idleticks = 0;
    u64_t kernelticks = 0;
    int blockedseen = 0;
    
    static struct tp *tick_procs = NULL;
    if (tick_procs == NULL)
    {
        //allocate memory for tick_procs
		tick_procs = malloc(nr_total * sizeof(tick_procs[0]));
        if (tick_procs == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(1);
        }
    }
    for (p = nprocs = 0; p < nr_total; p++)
    {
        u64_t uticks;
        if (!(proc2[p].p_flags & USED))
        { 
            continue;
        }
        tick_procs[nprocs].p = proc2 + p;
        tick_procs[nprocs].ticks = cputicks(&proc1[p], &proc2[p], cputimemode);
        //update uticks
        uticks = cputicks(&proc1[p], &proc2[p], 1);
        //work out total_ticks
        total_ticks = total_ticks + uticks;
        //classify all ticks into idletick, kernal tick system tick and usertick
        if (p - NR_TASKS == IDLE)
        {
            idleticks = uticks;
            continue;
        }
        if (p - NR_TASKS == KERNEL)
        {
            kernelticks = uticks;
        }
        if (!(proc2[p].p_flags & IS_TASK))
        {
            if (proc2[p].p_flags & IS_SYSTEM)
            {
                systemticks = systemticks + tick_procs[nprocs].ticks;
            }
            else
            {
                userticks = userticks + tick_procs[nprocs].ticks;
            }
        }
        nprocs++;
    }
    if (total_ticks == 0)
    {
        return;
    }
    //print CPU status
    printf("CPU states: %6.2f%% user, ", 100.0 * userticks / total_ticks);
    printf("%6.2f%% system, ", 100.0 * systemticks / total_ticks);
    printf("%6.2f%% kernel, ", 100.0 * kernelticks / total_ticks);
    printf("%6.2f%% idle\n", 100.0 * idleticks / total_ticks);
}

/*
 * get_procs - get all the processes, put them in structure sequence proc[], and call parse_dir to analyze them
 */
void get_procs()
{
    struct proc *p;
    int i;
    //exchange prev_proc and proc
    p = prev_proc;
    prev_proc = proc;
    proc = p;

    if (proc == NULL)
    {
        //allocate memory for process structures
        proc = malloc(nr_total * sizeof(proc[0])); 
        if (proc == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(0);
        }
    }

    for (i = 0; i < nr_total; i++)
    {
        proc[i].p_flags = 0; 
    }
	//call parse_dir for each process
    parse_dir();
}

/*
 * getkinfo - work out the total number of processes
 */
void getkinfo()
{
    FILE *fp;
    if ((fp = fopen("/proc/kinfo", "r")) == NULL)
    {
        fprintf(stderr, "opening /proc/kinfo failed\n");
        exit(1);
    }
	//read the number of processes and tasks
    if (fscanf(fp, "%u %u", &nr_procs, &nr_tasks) != 2)
    {
        fprintf(stderr, "reading from /proc/kinfo failed");
        exit(1);
    }

    fclose(fp);

    //work out nr_total
    nr_total = (int)(nr_procs + nr_tasks);
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    if(!strcmp(argv[0],"exit"))
    {
	    exit(0);
    }
    else if(!strcmp(argv[0],"jobs"))
    {
	    listjobs(jobs);
	    return 1;
    }
    else if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg"))
    {
	    do_bgfg(argv);
	    return 1;
    }
    else if (!strcmp(argv[0],"&"))
    {
	    return 1;
    }
    else if(!strcmp(argv[0],"history"))
    {
	    int commandToPrint = atoi(argv[1]);
	    if (commandToPrint<=0)
	    {
		    printf("No command history record will be printed!\n");
		    return 1;
	    }
	    else
	    {
		    for(int i=0;i<(commandToPrint<commandHistoryNumber?commandToPrint:commandHistoryNumber);i++)
		    {
			    printf("%s",commandHistory[i]);
		    }
		    return 1;
	    }
    }
    else if (!strcmp(argv[0],"cd"))
    {
	    int result = chdir(argv[1]);
	    if(result!=0)
	    {
		    printf("%s : No such directory!\n",argv[1]);
	    }
	    return 1;
    }
    else if(!strcmp(argv[0],"mytop"))
    {
	    print_memory();
	    getkinfo();
    	get_procs();
    	if(prev_proc==NULL)
    	{
    		get_procs();	
		}
    	print_procs(prev_proc,proc,1);
	    return 1;
    }
    else
    {
	    return 0;
    }
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    struct job_t *job = NULL;
    int id;
    int state;
    if(!strcmp(argv[0],"bg"))
    {
            state = BG;
    }
    else
    {
            state = FG;
    }
    if (argv[1]==NULL)
    {
	    printf("%s command requires PID or %%jobid argument\n", argv[0]);
	    return;
    }
    if (sscanf(argv[1],"%%%d",&id)>0)
    {
	    job = getjobjid(jobs,id);
	    if(job == NULL)
	    {
		    printf("%%%d: No such job\n", id);
		    return;
	    }
    }
    else if (sscanf(argv[1],"%d",&id)>0)
    {
	    job = getjobpid(jobs,id);
	    if(job == NULL)
	    {
		    printf("(%d): No such process\n", id);
	    }
    }
    else
    {
	    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
	    return;
    }
    if (state==BG)
    {
	    kill(-(job->pid),SIGCONT);
	    job->state = state;
	    printf("[%d] (%d) %s",job->jid, job->pid, job->cmdline);
    }
    else
    {
	    kill(-(job->pid),SIGCONT);
            job->state = state;
	    waitfg(job->pid);
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    sigset_t mask_none,prev;
    sigemptyset(&mask_none);
    while(1)
    {
	    sigprocmask(SIG_SETMASK,&mask_none,&prev);
	    if(fgpid(jobs)==0)
	    {
		    break;
	    }
	    sleep(1);
	    sigprocmask(SIG_SETMASK,&prev,NULL);
    }
    return;
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
    int olderrno = errno;
    int status;
    pid_t pid;
    struct job_t *job;
    sigset_t mask_all,prev;
    sigfillset(&mask_all);
     while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
     {
	     if(WIFEXITED(status))
	     {
		     sigprocmask(SIG_SETMASK,&mask_all,&prev);
		     deletejob(jobs,pid);
		     sigprocmask(SIG_SETMASK,&prev,NULL);
	     }
	     else if(WIFSIGNALED(status))
	     {
		     sigprocmask(SIG_SETMASK,&mask_all,&prev);
		     printf ("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
		     deletejob(jobs,pid);
		     sigprocmask(SIG_SETMASK,&prev,NULL);
	     }
	     else if(WIFSTOPPED(status))
	     {
		     sigprocmask(SIG_SETMASK,&mask_all,&prev);
		     printf ("Job [%d] (%d) stoped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
		     job = getjobpid(jobs,pid);
		     job->state = ST;
		     sigprocmask(SIG_SETMASK,&prev,NULL);
	     }
     }
     errno = olderrno;
     return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    int olderrno = errno;
    pid_t pid;
    if((pid = fgpid(jobs))!=0)
    {
	    kill(-pid,SIGINT);
    }
    errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    int olderrno = errno;
    pid_t pid;
    if((pid = fgpid(jobs))!=0)
    {
            kill(-pid,SIGSTOP);
    }
    errno = olderrno;
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


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
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}


