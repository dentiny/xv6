/*
 * Add-on feature: namespace
 * We wrap container struct outside of process, so that when we do part of the
 * job in the unit of container:
 * (1) cinit(): initialize the lock inside of the ctable.
 * (2) userinit(): used to initialize the first process, we turn to initialize
 * the first container first.
 * (3) fork(): it originally copy the status of the currently running process,
 * with new possible cfork() syscall, it allows to assign a new process the 
 * passed-in container, and use its rootdir.
 * (4) scheduler(): it originally did scheduling in the unit of processes, now
 * it turns to the unit of container. 
 * (5) wakeup1(): loop over every container and every process, check whether
 * the process is sleeping on the identified channel.
 * (6) wait(): when stop a container, kernel transfer all processes underneath
 * to root container and initproc. wait() loops over every container and every
 * process, initialize every zombie child process: initialize all data member,
 * deallocate allocate resource, set its status from ZOMBIE to UNUSED. After
 * that, set its container's status from CSTOPPING to CUNUSED.
 * 
 * Container user-interface:
 * (1) cont create <cont_name>: create a container, allocate resource and set
 * its status to CREADY.
 * (2) cont start <cont_name> prog [arg..]: start a container, set it CRUNNING
 * and the only running container, and execute the program.
 * (3) cont pause <cont_name>: set its status to CPAUSED, won't be scheduled
 * until resumed.
 * (4) cont resume <cont_name>: resume a container back to CRUNNABLE.
 * (5) cont stop <cont_name>: stop the container and let initproc to adopt and
 * exit all processes inside.
 * Note: cont start and cont resume enforces the caller's working directory
 * within the scope of container's root directory.
 */

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NCONT][NPROC];
} ptable;

struct 
{
  struct spinlock lock;
  struct container cont[NCONT];
} ctable;

static struct proc *initproc;
static struct container *curcont = 0;

// Container-related variables.
int nextcid = 1;

// Process-related variables.
int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
cinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&ctable.lock, "ctable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// Get current running container.
struct container*
mycont(void) {
  return curcont == 0 ? initproc->cont : curcont;
}

//PAGEBREAK: 32
// Look in the parent container's process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize state required to 
// run in the kernel. Otherwise return 0.
static struct proc*
allocproc(struct container *cont)
{
  struct proc *p;
  char *sp;
  struct proc *ptab;

  // Check container status.
  if (cont->state != CREADY && cont->state != CRUNNABLE && cont->state != CRUNNING) {
    return 0;
  }

  acquire(&ptable.lock);

  ptab = cont->ptable;

  for(p = ptab; p < &ptab[NPROC]; ++p) {
    if(p->state == UNUSED) {
      goto found;
    }
  }

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  p->cont = cont;
  return p;
}

//PAGEBREAK: 32
// Set up first user process within the root container.
struct proc*
initprocess(struct container *cont)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  if ((p = allocproc(cont)) == 0) {
    panic("Fail to allocate process\n");
  }
  
  // cont->initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = idup(cont->rootdir);
  p->cont = cont;

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);

  return p;  
}

// Look-up for CUNUSED container in ctable, and set its status to CEMBRYO.
static struct container*
alloccont(void) {
  struct container *cont = 0;
  acquire(&ctable.lock);
  for (cont = ctable.cont; cont < &ctable.cont[NCONT]; ++cont) {
    if (cont->state == CUNUSED) {
      goto found;
    }
  }
  release(&ctable.lock);
  return 0;

found:
  cont->state = CEMBRYO;
  cont->cid = nextcid++;
  release(&ctable.lock);
  return cont;
}

// Initialize first root container.
struct container*
initcontainer(void) {
  struct container *cont = 0;
  struct inode *rootdir = 0;

  if ((cont = alloccont()) == 0) {
    panic("Cannot allocate the initial container.\n");
  }

  if ((rootdir = namei("/")) == 0) {
    panic("Cannot set '/' as root container's rootdir.\n");
  }

  acquire(&ctable.lock);
  cont->rootdir = idup(rootdir);
  cont->state = CRUNNABLE;
  cont->nextproc = 0;
  memset(cont->rootpath, '\0', 200);
  cont->rootpath[0] = '/';
  safestrcpy(cont->name, "root container", sizeof(cont->name));

  // Initialize ptable inside each container.
  for (int ii = 0; ii < NCONT; ++ii) {
    ctable.cont[ii].ptable = ptable.proc[ii];
  }

  release(&ctable.lock);
  return cont;
}

// Initialize the first container and process at the launch period.
void
userinit(void) {
  struct container* rootcont = 0;
  rootcont = initcontainer();
  initproc = initprocess(rootcont);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(struct container *parentcont)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct proc *parent;
  struct container *cont;

  // If container assigned, use that container and initproc.
  // Otherwise, use current container(after cont start) if possible.
  if (parentcont != 0) {
    cont = parentcont;
    parent = initproc;
  } else if (curcont != 0) {
    cont = curcont;
    parent = initproc;
  } else {
    cont = curproc->cont;
    parent = curproc;
  }

  // Allocate process.
  if((np = allocproc(cont)) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = parent;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  struct proc *ptab;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  ptab = curproc->cont->ptable;

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to initproc of the running container.
  for (p = ptab; p < &ptab[NPROC]; ++p) {
    if (p->parent == curproc) {
      p->parent = initproc;
      if (p->state == ZOMBIE) {
        wakeup1(p->parent);
      }
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids = 0;
  int pid, ii;
  struct proc *curproc = myproc();
  struct container *cont;
  struct proc *ptab;

  acquire(&ptable.lock);
  for(;;){
    // Scan through tables looking for exited children.
    havekids = 0;
    for (ii = 0; ii < NCONT; ++ii) {
      cont = &ctable.cont[ii];
      if (cont->state == CUNUSED) {
        continue;
      }
      ptab = cont->ptable;
      for(p = ptab; p < &ptab[NPROC]; p++) {
        if(p->parent != curproc)
          continue;
        havekids = 1;
        if(p->state == ZOMBIE){
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          p->state = UNUSED;
          release(&ptable.lock);
          return pid;
        }
      }

      if (cont->state == CSTOPPING) {
        cont->state = CUNUSED;
      }
      
    } // iterate all ptables

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }  // infinite for loop
  
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

// The scheduler does scheduling in the unit of container. There're two 
// possibilities:
// (1) The container is CSTOPPING. Get processes inside of it and marked as
// killed. If the process is SLEEPING, wake them up.
// (2) The container is CRUNNABLE. Get a RUNNABLE process inside, and execute.
void
scheduler(void)
{
  struct proc *p;
  struct container *cont;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over container table looking for process to run.
    acquire(&ptable.lock);
    for(int ii = 0; ii < NCONT; ++ii) {
      // Check container status: for stopping container, kill processes inside.
      // For runnable container, schedule processes inside to run.
      // Note: if container status is allowed to be CRUNNING here, no second
      // container will be scheduled to run.
      cont = &ctable.cont[ii];
      if (cont->state != CRUNNABLE && cont->state != CRUNNING) {
        continue;
      }

      // Loop over the processes of ptable looking for runnable process. And 
      // the process should be RUNNABLE.
      p = &cont->ptable[(cont->nextproc++) % NPROC];
      if (p->state != RUNNABLE) {
        continue;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      cont->state = CRUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      if (cont->state != CSTOPPING && cont->state != CPAUSED) {
        cont->state = CRUNNABLE;
      }
      
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// Iterate all containers, wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  struct container *cont;
  int ii, jj;

  for (ii = 0; ii < NCONT; ++ii) {
    cont = &ctable.cont[ii];
    for (jj = 0; jj < NPROC; ++jj) {
      p = &cont->ptable[jj];
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill process could be called in either within kill() or cstop(), in both
// cases it should be guarentee the lock has been held.
int
kill1(int pid) {
  struct proc *p;
  struct proc *ptab;

  ptab = myproc()->cont->ptable;

  for (p = ptab; p < &ptab[NPROC]; ++p) {
    if (p->pid == pid) {
      p->killed = 1;
      // Wake up process if necessary.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
      }
      return 0;
    }
  }
  return -1;
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  acquire(&ptable.lock);
  int ret = kill1(pid);
  release(&ptable.lock);
  return ret;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *cstates[] = {
  [CUNUSED]    "unused  ",
  [CEMBRYO]    "embryo  ",
  [CREADY]     "ready   ",
  [CRUNNABLE]  "runnable",
  [CRUNNING]   "running ",
  [CPAUSED]    "paused  ",
  [CSTOPPING]  "stopping" 
  };
  static char *pstates[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };

  struct container *cont;
  struct proc *p;
  int ii, jj;

  acquire(&ptable.lock);
  for (ii = 0; ii < NCONT; ++ii) {
    cont = &ctable.cont[ii];
    if (cont->state == CUNUSED) {
      continue;
    }
    cprintf("\nContainer %d : %s %s\n", cont->cid, cont->name, cstates[cont->state]);

    for (jj = 0; jj < NPROC; ++jj) {
      p = &cont->ptable[jj];
      if (p->state == UNUSED) {
        continue;
      }
      cprintf("\t%s \t %d \t %s \t\n", p->name, p->pid, pstates[p->state]);
    }
  }
  release(&ptable.lock);
}

// Used for syscall SYS_cps. Display all containers and processes information
// on the screen. For processes within container, fake initproc if not root
// container, and give out fake PID.
int cps(void) {
  static char *cstates[] = {
  [CUNUSED]    "unused  ",
  [CEMBRYO]    "embryo  ",
  [CREADY]     "ready   ",
  [CRUNNABLE]  "runnable",
  [CRUNNING]   "running ",
  [CPAUSED]    "paused  ",
  [CSTOPPING]  "stopping" 
  };
  static char *pstates[] = {
  [UNUSED]    "unused  ",
  [EMBRYO]    "embryo  ",
  [SLEEPING]  "sleep   ",
  [RUNNABLE]  "runnable",
  [RUNNING]   "running ",
  [ZOMBIE]    "zombie  "
  };

  struct container *cont;
  struct proc *p;
  int ii, jj;
  acquire(&ptable.lock);
  for (ii = 0; ii < NCONT; ++ii) {
    cont = &ctable.cont[ii];
    if (cont->state == CUNUSED) {
      continue;
    }
    cprintf("\nContainer %d : %s %s, root path = %s\n", 
      cont->cid, cont->name, cstates[cont->state], cont->rootpath);
    cprintf("Process \tPID \t Real PID \t Status \t Container\n");

    // Fake initproc for every non-root container.
    int is_root_cont = strncmp(cont->name, "root container", 14) == 0;
    if (!is_root_cont) {
      cprintf("%s \t\t %d \t %d \t\t %s \t %s\n", "init", 1, 1, "sleep   ", cont->name);
    }

    int id = 2; // PID within container
    for (jj = 0; jj < NPROC; ++jj) {
      p = &cont->ptable[jj];
      if (p->state == UNUSED) {
        continue;
      }
      int id_in_cont = is_root_cont ? p->pid : id++;
      cprintf("%s \t\t %d \t %d \t\t %s \t %s\n", p->name, id_in_cont, p->pid, pstates[p->state], p->cont->name);
    }
  }
  release(&ptable.lock);
  return 0;
}

// For user-level getting and changing working directory(pwd and cd), container
// should achieve file system seperation.
// The util get the running container's root directory and fill into buffer. 
// It's guarenteed buffer has the capacity to fill in the rootdir.
void get_cont_rootdir(char *buffer) {
  acquire(&ctable.lock);
  struct container *cont = mycpu()->proc->cont;
  safestrcpy(buffer, cont->rootpath, sizeof(cont->rootpath));
  release(&ctable.lock);
}

static struct container*
get_container_by_name(char *cont_name) {
  acquire(&ctable.lock);
  for (int ii = 0; ii < NCONT; ++ii) {
    struct container cont = ctable.cont[ii];
    if (cont.state != CUNUSED && strncmp(cont.name, cont_name, strlen(cont_name)) == 0) {
      release(&ctable.lock);
      return &ctable.cont[ii];
    }
  }
  release(&ctable.lock);
  return 0;
}

static struct container*
get_container_by_cid(int cid) {
  acquire(&ctable.lock);
  for (int ii = 0; ii < NCONT; ++ii) {
    struct container cont = ctable.cont[ii];
    if (cont.state != CUNUSED && cont.cid == cid) {
      release(&ctable.lock);
      return &ctable.cont[ii];
    }
  }
  release(&ctable.lock);
  return 0;
}

// Extract container name from the full path.
static int extract_container_name(char *fpath, char *cont_name) {
  int len = 0; // length of subdirectory
  int idx = 0; // index of last slash
  for (char *ptr = fpath; *ptr != '\0'; ++ptr) {
    if (*ptr == '/') {
      len = 0;
      idx = ptr - fpath;
    } else {
      ++len;
    }
  }
  if (len > 15) {
    cprintf("Container name should be within 15 characters\n");
    return -1;
  }
  memmove(cont_name, fpath + idx + 1, len); // skip the slash
  cont_name[len] = '\0';
  return 0;
}

// User-space caller should be responsible for the sufficiency and 
// initialization of memory passed in.
int
cgetrootdir(char *rootdir) {
  acquire(&ctable.lock);
  struct container *cont = curcont == 0 ? initproc->cont : curcont;
  strncpy(rootdir, cont->rootpath, 200);
  release(&ctable.lock);
  return 0;
}

// Get the root directory for specified container.
int
getcontrootdir(char *cont_name, char *rootdir) {
  acquire(&ctable.lock);
  for (int ii = 0; ii < NCONT; ++ii) {
    struct container cont = ctable.cont[ii];
    int len1 = strlen(cont.name);
    int len2 = strlen(cont_name);
    if (len1 == len2 && strncmp(cont.name, cont_name, len1) == 0) {
      char *ptr = cont.rootpath;
      strncpy(rootdir, ptr, strlen(ptr));
      release(&ctable.lock);
      return 0;
    }
  }
  release(&ctable.lock);
  cprintf("No container %s created\n", cont_name);
  return -1;
}

// Create container at full path, container name is the last subdirectory name.
int
ccreate(char *fpath) {
  // Extract container name from full path.
  char cont_name[16];
  memset(cont_name, '\0', 16);
  if (extract_container_name(fpath, cont_name) < 0) {
    return -1;
  }

  // Check whether the container has been created.
  if (get_container_by_name(cont_name) != 0) {
    cprintf("Container %s has been created before.\n", cont_name);
    return -1;
  }

  // Allocate container(allocate space, set status to CEMBRYO).
  struct container *cont = 0;
  if ((cont = alloccont()) == 0) {
    cprintf("Container alllocation fail when creating container.\n");
    return -1;
  }

  // Check whether rootdir has been created.
  struct inode *rootdir = 0;
  if ((rootdir = namei(fpath)) == 0) {
    cprintf("Root directory %s check fail when creating container.\n", fpath);
    --nextcid;
    cont->state = CUNUSED;
    return -1;
  }

  // Set container status.
  acquire(&ctable.lock);
  cont->rootdir = idup(rootdir);
  safestrcpy(cont->rootpath, fpath, sizeof(cont->rootpath));
  cont->state = CREADY;
  cont->nextproc = 0;
  safestrcpy(cont->name, cont_name, sizeof(cont->name));
  release(&ctable.lock);
  return 0;
}

// Fork a process into container.
int
cfork(int cid) {
  struct container *cont = 0;
  if ((cont = get_container_by_cid(cid)) < 0) {
    cprintf("Container with cid %d doesn't exist\n", cid);
    return -1;
  }
  return fork(cont);
}

// Pause a container from being scheduled.
int
cpause(char *cont_name) {
  struct container *cont = 0;

  // Check whether the container exists, and its state is CRUNNABLE.
  if ((cont = get_container_by_name(cont_name)) == 0) {
    cprintf("Container %s doesn't exist\n", cont_name);
    return -1;
  }
  if (cont->state != CRUNNABLE && cont->state != CRUNNING) {
    cprintf("Container %s's state is not CRUNNABLE\n", cont_name);
    return -1;
  }

  acquire(&ctable.lock);
  cont->state = CPAUSED;
  curcont = curcont == cont ? 0 : curcont;
  release(&ctable.lock);
  return 0;
}

// Enable the container to be schedulable.
int
cresume(char *cont_name) {
  struct container *cont = 0;

  // Check whether the container exists.
  if ((cont = get_container_by_name(cont_name)) == 0) {
    cprintf("Container %s doesn't exist\n", cont_name);
    return -1;
  }

  // Check whether the container's status is CPAUSED.
  if (cont->state != CPAUSED) {
    cprintf("Container %s's state is not CPAUSED\n", cont_name);
    return -1;
  }

  acquire(&ctable.lock);
  cont->state = CRUNNABLE;
  release(&ctable.lock);
  return 0;
}

// Set container status to CSTOPPING, scheduler will kill processes inside.
// There're two cases:
// (1) There's no processes inside the container, mark it as CUNUSED.
// (2) There's processes ready to be schduled and execured, mark as CSTOPPING
// and wait for the scheduler.
int
cstop(char *cont_name) {
  struct container *cont = 0;

  // Check whether the container exists.
  if ((cont = get_container_by_name(cont_name)) == 0) {
    cprintf("Container %s doesn't exist\n", cont_name);
    return -1;
  }

  // Newly created container is bound to have 'sh' and 'init' proc, kill them
  // then exit.
  struct proc *p = 0;
  acquire(&ctable.lock);
  for (int ii = 0; ii < NPROC; ++ii) {
    p = &cont->ptable[ii];
    if (p->state != UNUSED) {
      p->parent = initproc;
      p->state = ZOMBIE;
    }
  }
  cont->state = CSTOPPING;
  curcont = curcont == cont ? 0 : curcont;
  release(&ctable.lock);
  return 0;
}

// Allow the scheduler to schedule the container.
int
cstart(char *cont_name) {
  struct container *cont = 0;

  // Check whether the container exists.
  if ((cont = get_container_by_name(cont_name)) == 0) {
    cprintf("Container %s doesn't exist\n", cont_name);
    return -1;
  }

  // Check whether the container's status is CREADY.
  if (cont->state != CREADY && cont->state != CRUNNING && cont->state != CRUNNABLE) {
    cprintf("Container %s's can only start at the status CREADY, CRUNNING or CRUNNABLE\n", cont_name);
    return -1;
  }

  acquire(&ctable.lock);
  cont->state = CRUNNABLE;
  curcont = cont;
  release(&ctable.lock);
  return cont->cid;
}