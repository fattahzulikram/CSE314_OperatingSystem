#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "pstat.h"

int sys_fork(void)
{
  return fork();
}

int sys_exit(void)
{
  exit();
  return 0; // not reached
}

int sys_wait(void)
{
  return wait();
}

int sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int sys_getpid(void)
{
  return myproc()->pid;
}

int sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

int sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
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
int sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//Sets the number of tickets in the calling process
int sys_settickets(void)
{
  int ticketNumber;

  //Error
  if (argint(0, &ticketNumber) < 0)
  {
    return -1;
  }

  // User cannot reduce ticket to 0 or set tickets less than 0
  if(ticketNumber <= 0){
    return -1;
  }
  
  // Checking done, call the process
  return setticketsutil(myproc(), ticketNumber);
}

// Returns information about processes
int sys_getpinfo(void)
{
  struct pstat *pstatStructure;
  //bad pointer
  if(argptr(0, (void *)&pstatStructure, sizeof(*pstatStructure)) < 0){
    return -1;
  }
  //NULL pointer
  if (!pstatStructure)
  {
    return -1;
  }

  // For iterating through all processes
  struct proc *process;
  int Counter = 0;

  // Lock the ptable first
  acquire(&ptable.lock);
  for (process = ptable.proc; process < &ptable.proc[NPROC]; process++)
  {
    if (process->state != UNUSED)
    {
      pstatStructure->pid[Counter] = process->pid;
      pstatStructure->inuse[Counter] = process->inuse;
      pstatStructure->tickets[Counter] = process->procTickets;
      pstatStructure->ticks[Counter] = process->ticks;
      Counter++;
    }
  }
  // Work done, release the lock
  release(&ptable.lock);

  return 0;
}