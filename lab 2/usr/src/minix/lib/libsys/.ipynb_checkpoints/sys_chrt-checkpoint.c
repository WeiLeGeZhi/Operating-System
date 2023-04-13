#include "syslib.h"
int sys_chrt(endpoint_t proc_ep,long deadline)
{
	int r;
	message m;
	//save process id and deadline into m
	m.m2_i1=proc_ep;
	m.m2_l1=deadline;
	//conduct kernal call
	r=_kernel_call(SYS_CHRT,&m);
	return r;
}
