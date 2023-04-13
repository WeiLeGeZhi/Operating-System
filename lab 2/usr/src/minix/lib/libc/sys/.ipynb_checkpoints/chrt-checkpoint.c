#include <lib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

int chrt(long deadline)
{
	struct timeval tv;
	struct timezone tz;
	message m;
	memset(&m,0,sizeof(m));
	unsigned int us_ddl = (unsigned int)deadline;
	alarm(us_ddl);//set alarm
	if(deadline>0)//record present time and calculate deadline
	{
		gettimeofday(&tv,&tz);
		deadline = tv.tv_sec+deadline;
	}
	//save and send deadline to service layer
	m.m2_l1=deadline;
	return(_syscall(PM_PROC_NR,PM_CHRT,&m));
}
