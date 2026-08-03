#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Assignment 1, Part 2: return the sum of two integer arguments.
int
sys_add(void)
{
  int a, b;

  if(argint(0, &a) < 0 || argint(1, &b) < 0)
    return -1;
  return a + b;
}

// Assignment 1, Part 2: print the currently active processes.
int
sys_ps(void)
{
  return ps();
}

// Assignment 1, Part 3: unicast send. sender_pid is redundant (the kernel
// knows myproc()), but the spec fixes the signature, so it is accepted and
// passed through.
int
sys_send(void)
{
  int sender_pid, rec_pid;
  char *msg;

  if(argint(0, &sender_pid) < 0 || argint(1, &rec_pid) < 0 ||
     argptr(2, &msg, MSGSIZE) < 0)
    return -1;
  return send(sender_pid, rec_pid, msg);
}

// Assignment 1, Part 3: blocking unicast receive.
int
sys_recv(void)
{
  char *msg;

  if(argptr(0, &msg, MSGSIZE) < 0)
    return -1;
  return recv(msg);
}

// Assignment 1, Part 3: multicast. Delivers the same MSGSIZE-byte message to
// every pid in rec_pids[].
//
// The spec describes doing this with a software interrupt (sys_msg_mcast)
// that runs a user-space signal handler. xv6 has no signal mechanism, and the
// assignment's own closing note permits building multicast on sys_recv
// instead, which is the route taken here: each receiver simply finds the
// message in its mailbox on its next recv().
//
// rec_pids[] carries no length in the given signature, so it is walked with
// fetchint() (which validates each word individually) until a non-positive
// entry or MAXRECV, whichever comes first. Callers must terminate the array.
int
sys_send_multi(void)
{
  int sender_pid, i, pid, n, failed;
  int pids[MAXRECV];
  uint addr;
  char *msg;

  if(argint(0, &sender_pid) < 0 || argint(1, (int*)&addr) < 0 ||
     argptr(2, &msg, MSGSIZE) < 0)
    return -1;

  n = 0;
  for(i = 0; i < MAXRECV; i++){
    if(fetchint(addr + 4 * i, &pid) < 0)
      break;
    if(pid <= 0)
      break;
    pids[n++] = pid;
  }
  if(n == 0)
    return -1;

  // Deliver to every receiver; report failure if any single delivery failed,
  // but still attempt the rest so one dead receiver cannot strand the others.
  failed = 0;
  for(i = 0; i < n; i++)
    if(send(sender_pid, pids[i], msg) < 0)
      failed = 1;

  return failed ? -1 : 0;
}
