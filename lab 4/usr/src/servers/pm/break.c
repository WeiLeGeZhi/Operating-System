/* The MINIX model of memory allocation reserves a fixed amount of memory for
 * the combined text, data, and stack segments.  The amount used for a child
 * process created by FORK is the same as the parent had.  If the child does
 * an EXEC later, the new size is taken from the header of the file EXEC'ed.
 *
 * The layout in memory consists of the text segment, followed by the data
 * segment, followed by a gap (unused memory), followed by the stack segment.
 * The data segment grows upward and the stack grows downward, so each can
 * take memory from the gap.  If they meet, the process must be killed.  The
 * procedures in this file deal with the growth of the data and stack segments.
 *
 * The entry points into this file are:
 *   do_brk:	  BRK/SBRK system calls to grow or shrink the data segment
 *   adjust:	  see if a proposed segment adjustment is allowed
 *   size_ok:	  see if the segment sizes are feasible (i86 only)
 */

#include "pm.h"
#include <signal.h>
#include "mproc.h"
#include "param.h"
#include <lib.h>
#define DATA_CHANGED       1	/* flag value when data segment size changed */
#define STACK_CHANGED      2	/* flag value when stack size changed */

/*===========================================================================*
 *				do_brk  				     *
 *===========================================================================*/
PUBLIC int do_brk()
{
/* Perform the brk(addr) system call.
 *
 * The call is complicated by the fact that on some machines (e.g., 8088),
 * the stack pointer can grow beyond the base of the stack segment without
 * anybody noticing it.
 * The parameter, 'addr' is the new virtual address in D space.
 */

  register struct mproc *rmp;
  int r;
  vir_bytes v, new_sp;
  vir_clicks new_clicks;

  rmp = mp;
  v = (vir_bytes) m_in.addr;
  new_clicks = (vir_clicks) ( ((long) v + CLICK_SIZE - 1) >> CLICK_SHIFT);
  if (new_clicks < rmp->mp_seg[D].mem_vir) {
	rmp->mp_reply.reply_ptr = (char *) -1;
	return(ENOMEM);
  }
  new_clicks -= rmp->mp_seg[D].mem_vir;
  if ((r=get_stack_ptr(who_e, &new_sp)) != OK) /* ask kernel for sp value */
  	panic(__FILE__,"couldn't get stack pointer", r);
  r = adjust(rmp, new_clicks, new_sp);
  rmp->mp_reply.reply_ptr = (r == OK ? m_in.addr : (char *) -1);
  return(r);			/* return new address or -1 */
}

/*===========================================================================*
 *				allocate_new_mem  				     *
 *===========================================================================*/
 PUBLIC int allocate_new_mem(rmp,data_clicks,delta,clicks)
register struct mproc *rmp;
phys_clicks data_clicks;
long delta;
phys_clicks clicks;
{
    register struct mem_map *mem_sp,*mem_dp,*mem_cp;
    int change = 0,d,s,r,ft;
    phys_bytes databytes,stackbytes,new_address_data_byte,old_address_data_byte,new_address_stack_byte,old_address_stack_byte;
    phys_clicks cur_data_clicks,new_address_data,old_address_data,old_clicks,new_clicks,old_address_stack,new_address_stack;
    /*set the size of new memory space and save the size of old memory space*/
    new_clicks = data_clicks+mem_sp->mem_len+delta;
    old_clicks = clicks;
    /*set pointer to data segment map & stack segment map*/ 
    mem_sp=&rmp->mp_seg[S];
    mem_dp=&rmp->mp_seg[D];
	/*allocate a new space of new_clicks for rmp*/
    if((new_address_data=alloc_mem(new_clicks)) == NO_MEM)
	{
      return(ENOMEM);
    }
	/*save the size of old stack and data*/
    databytes=(phys_bytes)mem_dp->mem_len << CLICK_SHIFT;
    stackbytes=(phys_bytes)mem_sp->mem_len << CLICK_SHIFT;
    /*work out the address of the old and new data and stack segment*/
    old_address_data=mem_dp->mem_phys;
    new_address_stack=new_address_data + new_clicks - mem_sp->mem_len;
    old_address_stack=mem_sp->mem_phys;
    /*change click into byte*/
    new_address_data_byte=(phys_bytes)new_address_data << CLICK_SHIFT;
    new_address_stack_byte=(phys_bytes)new_address_stack << CLICK_SHIFT;
    old_address_data_byte=(phys_bytes)old_address_data << CLICK_SHIFT;
    old_address_stack_byte=(phys_bytes)old_address_stack << CLICK_SHIFT;
	/*fill the new memory space with 0*/
    sys_memset(0, new_address_data_byte, (new_clicks << CLICK_SHIFT));
    /*copy the data and stack segment to the new memory space*/
    d = sys_abscopy(old_address_data_byte,new_address_data_byte,databytes);
    if (d < 0)
    {
    	panic(__FILE__, " can't copy data segment in alloc_new_mem", d);
	}
    s = sys_abscopy(old_address_stack_byte,new_address_stack_byte,stackbytes);
    if (s < 0)
    {
    	panic(__FILE__, " can't copy stack segment in alloc_new_mem", s);
	}
    /*change the physical address of data and stack segment and the virtue address of the stack segment*/
    rmp->mp_seg[D].mem_phys = new_address_data;
    rmp->mp_seg[S].mem_phys = new_address_stack;
    rmp->mp_seg[S].mem_vir = rmp->mp_seg[D].mem_vir+new_clicks -mem_sp->mem_len;
	/*save current size of data segment*/
    cur_data_clicks= mem_dp->mem_len;
	/*adjust the size of data segment*/
    if(data_clicks != mem_dp->mem_len)
	{
      mem_dp->mem_len = data_clicks;
      change |= DATA_CHANGED;
    }
	/*adjust the size of stack segment*/
    if(delta > 0)
	{
      mem_sp->mem_vir -= delta;
      mem_sp->mem_phys -= delta;
      mem_sp->mem_len += delta;
      change |= STACK_CHANGED;
    }
	/*judge whether the new memory size fit in the address space*/
    ft = (rmp->mp_flags & SEPARATE);
#if (CHIP == INTEL && _WORD_SIZE == 2)
    r = size_ok(ft, rmp->mp_seg[T].mem_len,rmp->mp_seg[D].mem_len,rmp->mp_seg[S].mem_len,rmp->mp_seg[D].mem_vir, rmp->mp_seg[S].mem_vir);
#else
    r = (rmp->mp_seg[D].mem_vir + rmp->mp_seg[D].mem_len > rmp->mp_seg[S].mem_vir) ? ENOMEM : OK;
#endif
	/*if the new memory size fit in the new address space,free the old memory space*/
  	if (r == OK) 
  	{
		int r2;
		if (change && (r2=sys_newmap(rmp->mp_endpoint, rmp->mp_seg)) != OK)
		{
			panic(__FILE__,"couldn't sys_newmap in adjust", r2);
		}
    	free_mem(old_address_data, old_clicks);
	  	return(OK);
  	}
  	/*if the new memory size don't fit in the new address space, restore the size and address of the old space*/
	else
	{
		if (change & DATA_CHANGED)
		{
			mem_dp->mem_len = cur_data_clicks;
		} 
	  	if (change & STACK_CHANGED) 
		{
			mem_sp->mem_vir += delta;
			mem_sp->mem_phys += delta;
			mem_sp->mem_len -= delta;
	  	}
	}
  	return(ENOMEM);
}

/*===========================================================================*
 *				adjust  				     *
 *===========================================================================*/
PUBLIC int adjust(rmp, data_clicks, sp)
register struct mproc *rmp;	/* whose memory is being adjusted? */
vir_clicks data_clicks;		/* how big is data segment to become? */
vir_bytes sp;			/* new value of sp */
{
/* See if data and stack segments can coexist, adjusting them if need be.
 * Memory is never allocated or freed.  Instead it is added or removed from the
 * gap between data segment and stack segment.  If the gap size becomes
 * negative, the adjustment of data or stack fails and ENOMEM is returned.
 */

  register struct mem_map *mem_sp, *mem_dp;
  vir_clicks sp_click, gap_base, lower, old_clicks;
  int changed, r, ft,res;
  long base_of_stack, delta;	/* longs avoid certain problems */

  mem_dp = &rmp->mp_seg[D];	/* pointer to data segment map */
  mem_sp = &rmp->mp_seg[S];	/* pointer to stack segment map */
  changed = 0;			/* set when either segment changed */

  if (mem_sp->mem_len == 0) return(OK);	/* don't bother init */

  /* See if stack size has gone negative (i.e., sp too close to 0xFFFF...) */
  base_of_stack = (long) mem_sp->mem_vir + (long) mem_sp->mem_len;
  sp_click = sp >> CLICK_SHIFT;	/* click containing sp */
  if (sp_click >= base_of_stack) return(ENOMEM);	/* sp too high */

  /* Compute size of gap between stack and data segments. */
  delta = (long) mem_sp->mem_vir - (long) sp_click;
  lower = (delta > 0 ? sp_click : mem_sp->mem_vir);

  /* Add a safety margin for future stack growth. Impossible to do right. */
#define SAFETY_BYTES  (384 * sizeof(char *))
#define SAFETY_CLICKS ((SAFETY_BYTES + CLICK_SIZE - 1) / CLICK_SIZE)
  gap_base = mem_dp->mem_vir + data_clicks + SAFETY_CLICKS;
  if (lower < gap_base) /*return(ENOMEM);*/	/* data and stack collided */
  {
  	res=allocate_new_mem(rmp,data_clicks,delta,(phys_clicks)(rmp->mp_seg[S].mem_vir - rmp->mp_seg[D].mem_vir + rmp->mp_seg[S].mem_len));
  	return res;
  }

  /* Update data length (but not data orgin) on behalf of brk() system call. */
  old_clicks = mem_dp->mem_len;
  if (data_clicks != mem_dp->mem_len) {
	mem_dp->mem_len = data_clicks;
	changed |= DATA_CHANGED;
  }

  /* Update stack length and origin due to change in stack pointer. */
  if (delta > 0) {
	mem_sp->mem_vir -= delta;
	mem_sp->mem_phys -= delta;
	mem_sp->mem_len += delta;
	changed |= STACK_CHANGED;
  }

  /* Do the new data and stack segment sizes fit in the address space? */
  ft = (rmp->mp_flags & SEPARATE);
#if (CHIP == INTEL && _WORD_SIZE == 2)
  r = size_ok(ft, rmp->mp_seg[T].mem_len, rmp->mp_seg[D].mem_len, 
       rmp->mp_seg[S].mem_len, rmp->mp_seg[D].mem_vir, rmp->mp_seg[S].mem_vir);
#else
  r = (rmp->mp_seg[D].mem_vir + rmp->mp_seg[D].mem_len > 
          rmp->mp_seg[S].mem_vir) ? ENOMEM : OK;
#endif
  if (r == OK) {
	int r2;
	if (changed && (r2=sys_newmap(rmp->mp_endpoint, rmp->mp_seg)) != OK)
  		panic(__FILE__,"couldn't sys_newmap in adjust", r2);
	return(OK);
  }

  /* New sizes don't fit or require too many page/segment registers. Restore.*/
  if (changed & DATA_CHANGED) mem_dp->mem_len = old_clicks;
  if (changed & STACK_CHANGED) {
	mem_sp->mem_vir += delta;
	mem_sp->mem_phys += delta;
	mem_sp->mem_len -= delta;
  }
  return(ENOMEM);
}

#if (CHIP == INTEL && _WORD_SIZE == 2)
/*===========================================================================*
 *				size_ok  				     *
 *===========================================================================*/
PUBLIC int size_ok(file_type, tc, dc, sc, dvir, s_vir)
int file_type;			/* SEPARATE or 0 */
vir_clicks tc;			/* text size in clicks */
vir_clicks dc;			/* data size in clicks */
vir_clicks sc;			/* stack size in clicks */
vir_clicks dvir;		/* virtual address for start of data seg */
vir_clicks s_vir;		/* virtual address for start of stack seg */
{
/* Check to see if the sizes are feasible and enough segmentation registers
 * exist.  On a machine with eight 8K pages, text, data, stack sizes of
 * (32K, 16K, 16K) will fit, but (33K, 17K, 13K) will not, even though the
 * former is bigger (64K) than the latter (63K).  Even on the 8088 this test
 * is needed, since the data and stack may not exceed 4096 clicks.
 * Note this is not used for 32-bit Intel Minix, the test is done in-line.
 */

  int pt, pd, ps;		/* segment sizes in pages */

  pt = ( (tc << CLICK_SHIFT) + PAGE_SIZE - 1)/PAGE_SIZE;
  pd = ( (dc << CLICK_SHIFT) + PAGE_SIZE - 1)/PAGE_SIZE;
  ps = ( (sc << CLICK_SHIFT) + PAGE_SIZE - 1)/PAGE_SIZE;

  if (file_type == SEPARATE) {
	if (pt > MAX_PAGES || pd + ps > MAX_PAGES) return(ENOMEM);
  } else {
	if (pt + pd + ps > MAX_PAGES) return(ENOMEM);
  }

  if (dvir + dc > s_vir) return(ENOMEM);

  return(OK);
}
#endif

