#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

/**
 * LAB1 - helper functions for sys_task_info
 */
int calculate_time(void) {
    struct proc *p = curr_proc();
    uint64 cycle = get_cycle();
	return (int)(((cycle - p->scheduledCycle)  * 1000 / CPU_FREQ));
}
int sys_getpid()
{
	return curr_proc()->pid;
}

// LAB2: implement sys_gettimeofday in pagetable. (VA to PA)
uint64 sys_gettimeofday(TimeVal *val, int _tz)
{
	// get current process
	struct proc *p = curr_proc();

	// get corresponding physical address, not safe to use virtual address passed in by process
	uint64 phys_addr = useraddr(p->pagetable, (uint64)val);

	// fetch corresponding val pointer from phys addr
	TimeVal *phys_val = (TimeVal *)phys_addr;

	// now we can finally do get time of day work
	uint64 cycle = get_cycle();
	phys_val->sec = cycle / CPU_FREQ;
	phys_val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	/* The code in `ch3` will leads to memory bugs*/
	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	return 0;
}

/*
 * LAB2: reimplement sys_task_info here
 */
int sys_task_info(TaskInfo *ti)
{
	// lab2 upgrade
	struct proc *p = curr_proc();
	uint64 phys_addr = useraddr(p->pagetable, (uint64)ti);
	TaskInfo *phys_ti = (TaskInfo *)phys_addr;

	// work
	phys_ti->time = calculate_time();    // update time
	phys_ti->status = p->taskInfo.status;    // update status
	// update syscall counts
	for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        phys_ti->syscall_times[i] = p->taskInfo.syscall_times[i];
    }
	return 0;
}

// TODO LAB2: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)

/*
 * LAB2: sys_mmap implementation
 * 
 * address must be page aligned otherwise report error
 * length can be rounded up to the nearest page
 * ignore parameters flag and fd for now
 * 
 * note - page recovery in case of allocation failure is not considered
 */
uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
	// error - address is not page-aligned
	if (start % PGSIZE != 0)
        return -1;

	// error - other bits of port must be 0
	if ((port & ~0x7) != 0)
        return -1;

	// error - port must specify some permissions (not be zero)
	if ((port & 0x7) == 0)
        return -1;

	// setup
	struct proc *p = curr_proc();    // get current process
	len = PGROUNDUP(len);            // round up length if needed

	// apply permissions based on port bits
	//    need to convert between port bit and PTE bit
	//    must start with PTE_U
	//    can use bitwise OR to add each permission
	int p_bits = PTE_U;                
    if (port & 0x1) p_bits = p_bits | PTE_R;    // read = bit 0 = 001
    if (port & 0x2) p_bits = p_bits | PTE_W;    // write = bit 1 = 010
    if (port & 0x4) p_bits = p_bits | PTE_X;    // execute = bit 2 = 100

	// iterate through pages we need to verify they are unmapped
	//    walkaddr returns 0 if page is not mapped
	//    so if we get a non-zero return value we know this page already has data and should error
	//    page_index is the virtual memory address
    for (uint64 page_index = start; page_index < start + len; page_index += PGSIZE) {
        if (walkaddr(p->pagetable, page_index) != 0)
			// error - a page we want is already mapped
            return -1;
    }

	// allocate memory
	//    kalloc can only allocate one page at a time
	//    therefore we must iterate across the pages like we did above
    for (uint64 page_index = start; page_index < start + len; page_index += PGSIZE) {
        char *mem = kalloc();
        if (mem == 0)
			// error - failed to allocate memory
            return -1;
        memset(mem, 0, PGSIZE);    // wipe old data
		// need to use mappages to create the actual page table entries
        if (mappages(p->pagetable, page_index, PGSIZE, (uint64)mem, p_bits) != 0)
			// error - failed to allocate memory
            return -1;
    }		

	return 0;
}

/*
 * LAB2: sys_munmap implementation
 * 
 * note - memory recovery and reclamation not considered
 */
uint64 sys_munmap(uint64 start, uint64 len)
{
	// error - address is not page-aligned
	if (start % PGSIZE != 0)
        return -1;
	
	// setup
	struct proc *p = curr_proc();    // get current process
	len = PGROUNDUP(len);            // round up length if needed

	// iterate through pages we need to verify they are all mapped
    for (uint64 page_index = start; page_index < start + len; page_index += PGSIZE) {
        if (walkaddr(p->pagetable, page_index) == 0)
			// error - a page we want to clear is already unmapped
            return -1;
    }

	// clear pages
	uvmunmap(p->pagetable, start, len/PGSIZE, 0);

	return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret = 0;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	
	// LAB1 - you may need to update syscall counter for task info here
	curr_proc()->taskInfo.syscall_times[id]++;
	
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_task_info:
		// LAB1 - you may need to add SYS_taskinfo case here
		sys_task_info((TaskInfo *)args[0]);
		break;
	case SYS_getpid:
		// LAB1 - needed to add this to get task_info to work
		ret = sys_getpid();
		break;
	case SYS_mmap:
		// LAB2
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		// LAB2
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
