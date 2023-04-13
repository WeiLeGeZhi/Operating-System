#include "kernel/system.h"
#include "kernel/vm.h"
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <minix/endpoint.h>
#include <minix/u64.h>

#if USE_CHRT

int do_chrt(struct proc *caller, message *m_ptr)
{
    struct proc *rp;
    long exp_time;
    exp_time = m_ptr->m2_l1;
    //find the address of process in kernal
    rp = proc_addr(m_ptr->m2_i1);
    //set the process's deadline
    rp->deadline = exp_time;
    return (OK);
}

#endif /* USE_CHRT */
