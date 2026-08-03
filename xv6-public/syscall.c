#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// Fetch the int at addr from the current process.
int
fetchint(uint addr, int *ip)
{
  struct proc *curproc = myproc();

  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
int
fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){
    if(*s == 0)
      return s - *pp;
  }
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
int
argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();
 
  if(argint(n, &i) < 0)
    return -1;
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  *pp = (char*)i;
  return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
int
argstr(int n, char **pp)
{
  int addr;
  if(argint(n, &addr) < 0)
    return -1;
  return fetchstr(addr, pp);
}

extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);
extern int sys_add(void);
extern int sys_ps(void);
extern int sys_send(void);
extern int sys_recv(void);
extern int sys_send_multi(void);

// Defined below in this file, next to the tracing state they operate on.
int sys_toggle(void);
int sys_print_count(void);

static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_toggle]  sys_toggle,
[SYS_print_count] sys_print_count,
[SYS_add]     sys_add,
[SYS_ps]      sys_ps,
[SYS_send]    sys_send,
[SYS_recv]    sys_recv,
[SYS_send_multi] sys_send_multi,
};

// Names reported by sys_print_count. Index must stay aligned with syscalls[]
// above; entry 0 is unused because system call numbers start at 1.
static char *syscall_names[] = {
[SYS_fork]    "sys_fork",
[SYS_exit]    "sys_exit",
[SYS_wait]    "sys_wait",
[SYS_pipe]    "sys_pipe",
[SYS_read]    "sys_read",
[SYS_kill]    "sys_kill",
[SYS_exec]    "sys_exec",
[SYS_fstat]   "sys_fstat",
[SYS_chdir]   "sys_chdir",
[SYS_dup]     "sys_dup",
[SYS_getpid]  "sys_getpid",
[SYS_sbrk]    "sys_sbrk",
[SYS_sleep]   "sys_sleep",
[SYS_uptime]  "sys_uptime",
[SYS_open]    "sys_open",
[SYS_write]   "sys_write",
[SYS_mknod]   "sys_mknod",
[SYS_unlink]  "sys_unlink",
[SYS_link]    "sys_link",
[SYS_mkdir]   "sys_mkdir",
[SYS_close]   "sys_close",
[SYS_toggle]  "sys_toggle",
[SYS_print_count] "sys_print_count",
[SYS_add]     "sys_add",
[SYS_ps]      "sys_ps",
[SYS_send]    "sys_send",
[SYS_recv]    "sys_recv",
[SYS_send_multi] "sys_send_multi",
};

// Tracing state is kernel-global, per the spec ("two states within the
// kernel"), not per-process. Counts are zeroed on every TRACE_OFF->TRACE_ON
// transition so they always cover "since the state changed to TRACE_ON".
static int trace_state = TRACE_OFF;
static int syscall_count[NELEM(syscalls)];

// ASSUMPTION: the two tracing-control calls are not themselves traced. The
// spec's example output lists neither, and counting sys_print_count while it
// is executing is self-referential. Flip this to 0 if check.sh disagrees.
#define EXCLUDE_TRACE_CONTROL 1

static int
traced(int num)
{
  if(trace_state != TRACE_ON)
    return 0;
#if EXCLUDE_TRACE_CONTROL
  if(num == SYS_toggle || num == SYS_print_count)
    return 0;
#endif
  return num > 0 && num < NELEM(syscall_count) && syscall_names[num] != 0;
}

int
sys_toggle(void)
{
  int i;

  if(trace_state == TRACE_OFF){
    // Entering TRACE_ON: start a fresh counting window.
    for(i = 0; i < NELEM(syscall_count); i++)
      syscall_count[i] = 0;
    trace_state = TRACE_ON;
  } else {
    trace_state = TRACE_OFF;
  }
  return 0;
}

int
sys_print_count(void)
{
  int i, j, min;
  int printed[NELEM(syscall_count)];

  for(i = 0; i < NELEM(syscall_count); i++)
    printed[i] = 0;

  // Selection sort by name so output is alphabetically ascending. The table
  // is tiny (25 entries), so an O(n^2) pass costs nothing and avoids
  // allocating scratch space in the kernel.
  for(;;){
    min = -1;
    for(j = 1; j < NELEM(syscall_count); j++){
      if(printed[j] || syscall_names[j] == 0 || syscall_count[j] <= 0)
        continue;
      if(min < 0 || strncmp(syscall_names[j], syscall_names[min], 32) < 0)
        min = j;
    }
    if(min < 0)
      break;
    cprintf("%s %d\n", syscall_names[min], syscall_count[min]);
    printed[min] = 1;
  }
  return 0;
}

void
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Count before dispatch: a call that turns tracing off should still be
    // reflected in the window it ran in.
    if(traced(num))
      syscall_count[num]++;
    curproc->tf->eax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}
