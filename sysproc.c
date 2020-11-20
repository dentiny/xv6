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
  return fork(0);
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

// Print process status on the console.
int sys_cps(void) {
  return cps();
}

int sys_ccreate(void) {
  char *cont_name = 0;
  if (argstr(0, &cont_name) < 0) {
    return -1;
  }
  return ccreate(cont_name);
} 

int sys_cfork(void) {
  int cid = -1;
  if (argint(0, &cid) < 0) {
    return -1;
  }
  return cfork(cid);
}

int sys_cgetrootdir(void) {
  char *rootdir = 0;
  if (argstr(0, &rootdir) < 0) {
    return -1;
  }
  return cgetrootdir(rootdir);
}

int sys_getcontrootdir(void) {
  char *cont_name = 0;
  char *rootdir = 0;
  if (argstr(0, &cont_name) < 0 || argstr(1, &rootdir) < 0) {
    return -1;
  }
  return getcontrootdir(cont_name, rootdir);
}

int sys_cpause(void) {
  char *cont_name = 0;
  if (argstr(0, &cont_name) < 0) {
    return -1;
  }
  return cpause(cont_name);
}

int sys_cstart(void) {
  char *cont_name = 0;
  if (argstr(0, &cont_name) < 0) {
    return -1;
  }
  return cstart(cont_name);
}

int sys_cstop(void) {
  char *cont_name = 0;
  if (argstr(0, &cont_name) < 0) {
    return -1;
  }
  return cstop(cont_name);
}

int sys_cresume(void) {
  char *cont_name = 0;
  if (argstr(0, &cont_name) < 0) {
    return -1;
  }
  return cresume(cont_name);
}