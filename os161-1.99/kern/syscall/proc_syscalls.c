#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>

#include <mips/trapframe.h>
#include <synch.h>
#include <opt-A2.h>

#if OPT_A2

#include <vfs.h>
#include <kern/fcntl.h>

#endif

#if OPT_A2

//  thread_fork(const char *name,
//              struct proc *proc,
//              void (*entrypoint)(void *data1, unsigned long data2),
//              void *data1, unsigned long data2)
void call_enter_forked_process(void *data1, unsigned long data2);

void call_enter_forked_process(void *data1, unsigned long data2) {
    struct trapframe *tf = (struct trapframe *)data1;
    (void)data2;
    enter_forked_process(tf);
}

// 1. create process structure for child process
// 2. create and copy address space (and data) from parent to child
// 3. attach the newly created address space to the child process structure
// 4. assign PID to child process and create the parent/child relationship
// 5. create thread for child process (need a safe way to pass the trapframe to the child thread)
// 6. child thread needs to put the trapframe onto the stack and modify it so that it returns the current value (and executes the next instruction)
// 7. call mips_usermod in the child to go back to userspace
int sys_fork(struct trapframe *tf, int *retval) {
    // 1
    struct proc *child_proc = proc_create_runprogram(curproc->p_name);
    if (!child_proc) {
        return ENPROC;
    }
    // 2, 3
    int err = as_copy(curproc_getas(), &(child_proc->p_addrspace));
    if (err) {
        proc_destroy(child_proc);
        return err;
    }
    // 4
    struct procinfo *pi = procinfoarray_get_by_pid(procinfolist, child_proc->pid);
    pi->ppid = curproc->pid;
    // 5
    struct trapframe *ctf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
    if (!ctf) {
        proc_destroy(child_proc);
        return ENOMEM;
    }
    *ctf = *tf;
    // 6, 7
    err = thread_fork(child_proc->p_name, child_proc, call_enter_forked_process, (void *)ctf, 0ul);
    if (err) {
        proc_destroy(child_proc);
        kfree(ctf);
        return err;
    }
    *retval = child_proc->pid;
    return 0;
}

#endif

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
#if OPT_A2
  if (!procinfolist_cv) {
      procinfolist_cv = cv_create("procinfolist_cv");
  }

  lock_acquire(procinfolist_lock);

  struct procinfo *pi = procinfoarray_get_by_pid(procinfolist, p->pid);
  pid_t pid = pi->pid;
  pid_t ppid = pi->ppid;
  if (ppid == -1) {
      pi->state = EXITED;
      procinfoarray_remove_by_pid(procinfolist, pid);
  } else {
      pi->exit_status = _MKWAIT_EXIT(exitcode);
      struct procinfo *pp = procinfoarray_get_by_pid(procinfolist, ppid);
      if (!pp || RUNNING == pp->state) {
          pi->state = ZOMBIE;
          cv_broadcast(procinfolist_cv, procinfolist_lock);
      } else {
          pi->state = EXITED;
          procinfoarray_remove_by_pid(procinfolist, pid);
      }
  }
  // cleanup zombine
  unsigned size = procinfoarray_num(procinfolist);
  // assumption: no multi child
  for (unsigned i=0; i<size; ++i) {
      struct procinfo *pi = procinfoarray_get(procinfolist, i);
      if (pi->ppid == pid && pi->state == ZOMBIE) {
          pi->state = EXITED;
          procinfoarray_remove(procinfolist, i);
          break;
      }
  }

  lock_release(procinfolist_lock);
#endif

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
    *retval = curproc->pid;
#else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }

#if OPT_A2
  if (!procinfolist_cv) {
      procinfolist_cv = cv_create("procinfolist_cv");
  }

  lock_acquire(procinfolist_lock);

  struct procinfo *wait_proc = procinfoarray_get_by_pid(procinfolist, pid);
  while (RUNNING == wait_proc->state) {
      cv_wait(procinfolist_cv, procinfolist_lock);
  }
  exitstatus = wait_proc->exit_status;

  lock_release(procinfolist_lock);
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2

// 1. count the number of arguments and copy them into the kernel
// 2. copy the program path into the kernel
// 3. open the program file using vfs_open
// 4. create new address space, set process to the new address space, and activate it
// 5. using the opened program file, load the program image using load_elf
// 6. need to copy the arguments into the new address space
// 7. delete old address space
// 8. call enter_new_process (args_on_stack, stack_pointer, prog_entry_point)
int sys_execv(struct trapframe *tf, int *retval) {
    (void)retval;

    char *progname = (char *)(tf->tf_a0);
    char **arglist = (char **)(tf->tf_a1);

    // 1
    int argc = 0;
    for (; arglist[argc]!=NULL; ++argc) { }
    char **argv = (char **)kmalloc((argc + 1) * sizeof(char *));
    for (int i=0; i<argc; ++i) {
        size_t len = strlen(arglist[i]) + 1;
        argv[i] = (char *)kmalloc(len * sizeof(char));
        copyinstr((const_userptr_t)(arglist[i]), argv[i], len, NULL);
    }
    argv[argc] = NULL;

    // 2
    size_t progname_len = strlen(progname) + 1;
    char *binpath = (char *)kmalloc(progname_len * sizeof(char));
    copyinstr((const_userptr_t)progname, binpath, progname_len, NULL);

    // 3
    struct vnode *vn;
    int err = vfs_open(binpath, O_RDONLY, 0, &vn);
    if (err) {
        return err;
    }
    // kfree(binpath);

    // 4
    struct addrspace *as = as_create();
    if (!as) {
        vfs_close(vn);
        return ENOMEM;
    }
    struct addrspace *old_as = curproc_setas(as);
    as_activate();

    // 5
    vaddr_t entrypoint;
    err = load_elf(vn, &entrypoint);
    if (err) {
        vfs_close(vn);
        return err;
    }
    vfs_close(vn);

    // 6
    vaddr_t stkptr;
    err = as_define_stack(as, &stkptr);
    if (err) {
        return err;
    }
    // store actual content (*argv[i])
    vaddr_t *argv_locs = kmalloc((argc+1)*sizeof(vaddr_t));
    for (int i=argc-1; i>=0; --i) {
        size_t len = strlen(argv[i]) + 1;
        stkptr -= ROUNDUP(len, 4);
        err = copyoutstr(argv[i], (userptr_t)stkptr, len, NULL);
        if (err) {
            return err;
        }
        argv_locs[i] = stkptr;
    }
    argv_locs[argc] = (vaddr_t)NULL;

    // store pointer to content (argv[i])
    size_t pointer_size = sizeof(vaddr_t);
    for (int i=argc; i>=0; --i) {
        stkptr -= ROUNDUP(pointer_size, 4);
        err = copyout(argv_locs+i, (userptr_t)stkptr, pointer_size);
        if (err) {
            return err;
        }
    }

    // for (int i=0; i<argc; ++i) kfree(argv[i]);
    // kfree(argv);
    // kfree(argv_locs);

    // 7
    as_destroy(old_as);

    // 8
    enter_new_process(argc, (userptr_t)stkptr, stkptr, entrypoint);

    return EINVAL;
}

#endif
