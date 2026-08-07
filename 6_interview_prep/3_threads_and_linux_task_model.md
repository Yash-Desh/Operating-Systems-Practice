# Kernel Threads vs. User Threads, Thread Models, and the Linux Task Model

- **Author:** Yash Deshpande
- **Date:** 02-08-2026
- **LLM Model:** Claude (Opus 4.8)

## Index

1. [Kernel Threads vs. User Threads](#1-kernel-threads-vs-user-threads)
2. [Thread Models (Linux & Pthreads)](#2-thread-models-linux--pthreads)
3. [Process vs. Thread](#3-process-vs-thread)
4. [The Linux Task Model: Threads vs. Processes](#4-the-linux-task-model-threads-vs-processes)
5. [Sources](#sources)

---

## 1. Kernel Threads vs. User Threads

A **thread** is a single point of execution — a PC, a register set, and a stack —
within a process. The kernel-vs-user question is about **who manages the thread**:
the OS kernel, or a user-space library.

### Kernel threads

- Created, scheduled, and managed **by the OS kernel**. The kernel knows they exist.
- The kernel's scheduler picks kernel threads to run and performs their context
  switches.
- A blocking system call (e.g., disk I/O) blocks only **that** thread — the kernel
  can run another thread of the same process.
- On a multiprocessor, different kernel threads of one process can run **truly in
  parallel** on different cores.
- Cost: creation and switching go through the kernel (a trap + kernel bookkeeping),
  so they're heavier than user threads.

### User threads

- Implemented **entirely in a user-space library** (historically green threads,
  GNU Pth, early Java threads). The kernel sees only the single process and is
  unaware of the threads inside it.
- Scheduling and switching happen in user space — **fast**, no kernel trap.
- Fatal weaknesses in the pure form:
  1. If one user thread makes a blocking syscall, the kernel blocks the **whole
     process** (all its user threads stall).
  2. They **cannot use multiple cores** — the kernel schedules one process onto
     one core at a time.

### Quick contrast

| | User threads | Kernel threads |
|---|---|---|
| Managed by | User-space library | OS kernel |
| Switch cost | Cheap (no trap) | Heavier (kernel trap) |
| Blocking syscall | Blocks whole process | Blocks only that thread |
| Multicore parallelism | No | Yes |
| Kernel aware? | No | Yes |

---

## 2. Thread Models (Linux & Pthreads)

Because user threads must eventually run on a kernel-schedulable entity, systems
pick a **mapping** between user threads and kernel threads.

- **Many-to-one**: many user threads → 1 kernel thread. Fast switches, but no
  parallelism and one blocking call stalls all. (Pure user-thread libraries.)
- **One-to-one**: each user thread → its own kernel thread. Real parallelism,
  blocking is isolated. **Linux and Windows use this.**
- **Many-to-many**: multiplexes M user threads onto ≤ N kernel threads. Flexible
  but complex; largely abandoned in practice.

**Pthreads** is a **specification** (POSIX API — `pthread_create`, `pthread_join`,
etc.), not an implementation. On Linux, `pthread_create()` is a thin wrapper over
the `clone()` system call and produces a **one-to-one** kernel-backed thread.

**Bottom line:** modern `pthreads` on Linux are one-to-one — every user thread is
backed by a kernel thread. "User thread" as a distinct scheduled entity mostly
survives today as *green threads / coroutines / goroutines* layered on top of
kernel threads.

---

## 3. Process vs. Thread

| Aspect | Process | Thread (of a process) |
|---|---|---|
| Address space | Own, isolated (own page table) | Shared with peer threads |
| Code / data / heap | Private | Shared |
| Stack | Own | Own stack, but reachable by peers (see [[4_threads_share_stack_memory]]) |
| Registers / PC | Own | Own |
| Open files, signals | Private | Shared |
| Creation cost | High (copy address space) | Low (share address space) |
| Context switch cost | Higher (page-table + TLB flush) | Lower (address space preserved) |
| Isolation / fault containment | Strong (a crash is contained) | Weak (a bad write corrupts peers) |
| Communication | IPC (pipes, shm, sockets) | Shared memory directly |

Key idea: threads trade **isolation** for **cheap sharing and cheap switching**.
A crash in one thread can take down the whole process; separate processes are
protected from each other by separate address spaces.

---

## 4. The Linux Task Model: Threads vs. Processes

### The core insight

Linux's scheduling unit is the **task** (`struct task_struct`), created by
**`clone()`**. There is no separate "process object" vs. "thread object" in the
kernel scheduler. What we *call* a process vs. a thread is determined entirely by
**which resources the new task shares with its creator**, selected via `CLONE_*`
flags:

| Flag | What gets shared |
|---|---|
| `CLONE_VM` | Address space (memory) |
| `CLONE_FS` | Filesystem info (cwd, root) |
| `CLONE_FILES` | Open file-descriptor table |
| `CLONE_SIGHAND` | Signal handlers |
| `CLONE_THREAD` | Same thread group (shared PID) |

- `fork()` = `clone()` with **none** of these → nothing shared (copy everything)
  → what we call a **process**.
- `pthread_create()` = `clone()` with **all** of them → shares VM, files, signals,
  thread group → what we call a **thread**.
- Anything **in between** is legal — this "more or less degree of sharing" is how
  Linux **containers** and calls like `vfork` are built.

**Conclusion:** because every schedulable entity is just a task, the scheduler has
**no separate code path** for "switch thread" vs. "switch process." It always
switches *tasks*, treating a thread and a process identically as candidates.

### Refinement 1 — Context-switch cost still differs

The scheduler picks tasks uniformly, but the *actual switch work* depends on the
shared flags:

- Two tasks that **share `CLONE_VM`** (two threads of one process) → the page
  tables / CR3 are the same → **no address-space switch, no TLB flush**.
- Two tasks that **don't** share VM (two processes) → load a new page-table root
  (CR3 write) → **TLB flush**, which is the expensive part.

> The scheduler treats them identically as tasks, but the context switch is
> cheaper between threads because the address space (and thus the TLB) is
> preserved.

### Refinement 2 — Terminology precision (common interview trap)

- The kernel's word is **task** (`task_struct`). One `task_struct` = one
  schedulable thread.
- What userspace calls a "process" is, in kernel terms, a **thread group** — a set
  of tasks sharing a `tgid` (thread group ID). The `getpid()` you see is actually
  the **tgid**; each thread's unique kernel id is its **tid** (`pid` field in the
  kernel).
- So "a process" isn't a kernel object at all — it's *"the group of tasks that
  share these resources."*

### Silberschatz page references (10th ed.)

| Topic | Printed page | PDF page |
|---|---|---|
| §4.4.3 Linux Threads — `clone()` and "Linux uses the term task" | **p. 195** | **PDF 252** |
| Figure 4.22 — the `CLONE_*` flags | **p. 196** | **PDF 253** |
| Chapter summary — "Linux does not distinguish between processes and threads" | **p. 197** | **PDF 254** |

### Interview soundbite (bridges textbook + Linux)

> Generically, threads can be user-level or kernel-level, mapped
> many-to-one / one-to-one / many-to-many. Linux takes the one-to-one model to its
> logical end: it doesn't distinguish threads from processes at all — both are
> `task_struct`s created by `clone()`, differing only in which resources they
> share. Scheduling is therefore always task-switching; the thread-vs-process
> distinction only affects how much state the context switch must swap.

---

## Sources

- **Silberschatz**, *Operating System Concepts* (10th ed., 2018)
  (printed page + 57 = PDF page):
  - §4.2 Overview, Figure 4.6 "User and kernel threads" — **p. 163** (PDF 220).
  - §4.3 Multithreading Models (many-to-one, one-to-one, many-to-many) —
    **pp. 166-168** (PDF 223-225; one-to-one p. 167 / PDF 224, many-to-many
    p. 168 / PDF 225).
  - §4.4.3 Linux Threads / `clone()` / task model — **pp. 195-196** (PDF 252-253;
    Figure 4.22 CLONE_ flags on p. 196 / PDF 253).
  - Chapter 4 summary ("does not distinguish between processes and threads") —
    **p. 197** (PDF 254).
- **xv6 book**, *xv6: a simple, Unix-like teaching operating system* (Sept 2018)
  (printed page = PDF page):
  - Ch. 5 "Scheduling" — per-process kernel thread + per-CPU scheduler thread,
    `swtch` — **pp. 61-64** (PDF 61-64).
  - Ch. 1 — each process has one kernel thread; user vs. kernel stack —
    **pp. 21-22** (PDF 21-22).
- **OSTEP**, Ch. 26 "Concurrency: An Introduction"
  (`OSTEP/3_Concurrency_25-34/26_threads-intro.pdf`) — general thread abstraction,
  TCBs, context switch — **p. 1** (PDF 1; does not split user vs. kernel).
- **xv6 source** (`p56/cs537/p5/xv6-public`) — concrete one-kernel-thread-per-process
  implementation: `proc.h` (`struct context`, `kstack`), `swtch.S`, `proc.c`
  (`scheduler`, `sched`, `yield`, `allocproc`), `trap.c` (timer preemption).
  Note: p5 itself is a memory-mapping project, not a threads project.

### Related notes

- [[4_threads_share_stack_memory]] — threads of the same process can access each
  other's stack variables.
