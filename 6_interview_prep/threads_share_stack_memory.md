# Threads of the Same Process Can Access Each Other's Stack Variables

- **Author:** Yash Deshpande
- **Date:** 02-08-2026
- **LLM Model:** Claude (Opus 4.8)

## The core idea

Each thread has **its own stack**, but that stack is not **private or protected**.
All threads of a process live in **one shared address space**, so a thread can
read/write another thread's stack **as long as it has a pointer into it**.

- **"Own stack"** → yes, needed for correct independent execution (each thread
  needs its own local variables, function-call return addresses, saved registers).
  If threads shared one stack, concurrent function calls would clobber each
  other's frames.
- **"Private/isolated stack"** → no. Same address space = mutually reachable
  given a pointer. The language and OS will **not** stop you.

The stack pointer (`%rsp`) and locals are private *by convention and addressing*,
not by *enforcement*.

## Why this is true

| | Two threads (same process) | Two processes |
|---|---|---|
| Address space | Shared (same page table) | Separate (different page tables) |
| Same virtual address → same memory? | Yes | No |
| Pass a raw stack pointer across? | Valid | Meaningless |

Threads skip the page-table switch on a context switch (OSTEP Ch. 26, p.1:
"the address space remains the same... no need to switch which page table we are
using"). That is exactly why a pointer handed to another thread stays valid.

## Demo that proves it

A runnable copy already lives in the repo at
[`7_demos/thread_stack_sharing.c`](../7_demos/thread_stack_sharing.c).

```c
#include <stdio.h>
#include <pthread.h>

static void *worker(void *arg) {
    int *p = (int *)arg;   // pointer into MAIN's stack
    *p = 42;               // legally writing another thread's stack frame
    return NULL;
}

int main(void) {
    int x = 0;                             // lives on main's stack
    pthread_t t;
    pthread_create(&t, NULL, worker, &x);  // hand worker a pointer to x
    pthread_join(t, NULL);
    printf("%d\n", x);                     // prints 42
}
```

Build & run:

```
gcc -Wall -o thread_stack_sharing thread_stack_sharing.c -pthread
./thread_stack_sharing
```

Key observation: `&x` in `main` and `arg` in `worker` print the **same address**,
and the worker's write to `42` is visible in `main` after `join`.

## The classic footgun

Never return or hand off a pointer to a local whose owning thread's frame may
disappear:

```c
int *bad() {
    int local = 5;
    return &local;   // frame is gone once bad() returns -> dangling pointer
}
```

Another thread dereferencing that pointer is **not** blocked by protection — it
just reads garbage or corrupts a reused frame. This is why thread-shared data is
usually placed on the **heap** (or in globals) and guarded with **locks**, rather
than passed around as stack pointers.

## Interview soundbite

> Each thread gets its own stack for independent execution, but all stacks live in
> the process's single shared address space. There's no memory protection between
> threads, so one thread can read or write another's stack variables if it holds a
> pointer to them. It usually shouldn't — and passing pointers to stale stack
> frames is a common bug — but nothing prevents it.

## Summary

- Own stack → yes, for correct independent execution.
- Private/isolated stack → no. Same address space = mutually reachable given a
  pointer.
- Threads can touch each other's stacks; they just usually shouldn't, and the
  language/OS won't stop them if they do.

## Guard Pages, Stack Overflow, and the Stack Clash

The "same address space, so anything goes" intuition is **incomplete**. Threads
*can* read/write each other's stacks via a pointer (proven above), but each stack
is a separate mapping fronted by a **guard page**, which changes what happens on
*overflow* vs. *deliberate access*.

**1. Thread stacks are separate mappings with a guard page between them.**
Each pthread stack is its own `mmap` region (8 MB by default here) with a **4 KB
guard page** (`PROT_NONE`, shown as `---p` in `/proc/self/maps`) just below it.
Adjacent stacks are separated by exactly that gap. Verified: `7_demos/thread_stack_layout.c`.

**2. Sequential overflow is TRAPPED, not spilled.**
Ordinary overflow (deep recursion, large arrays touched page-by-page) grows the
stack *downward* and hits the guard page first → **SIGSEGV**. It does **not**
silently corrupt the neighbouring thread's stack. Verified:
`7_demos/stack_guard_page.c` (overflower faults just below its own base; the
victim's canary stays intact).

**3. But a single oversized frame can SKIP the guard (Stack Clash).**
A local allocation larger than the guard page moves the stack pointer *past* the
guard in one step. If the code then writes at the low end, it lands in the
neighbour's stack **without ever touching the guard page** — silent corruption.
This is CVE-2017-1000364.

- **Without** `-fstack-clash-protection`: the neighbour's canary is corrupted.
- **With** it (modern GCC default, verified `[enabled]` on this toolchain): the
  compiler emits per-page **stack probes** that hit the guard → clean SIGSEGV.

Verified both ways: `7_demos/stack_clash.c`.

**4. The main thread's stack is structurally different but equally protected.**

| | Main thread stack | pthread stack |
|---|---|---|
| Kernel view | single `[stack]` VMA | anonymous `mmap` |
| Initial size | tiny (~136 KB), **grows on demand** | full size (8 MB) up front |
| Cap | `RLIMIT_STACK` (`ulimit -s`, 8 MB) | fixed at creation |
| Guard | kernel `stack_guard_gap` (~1 MB) | one 4 KB guard page |
| Sequential overflow | **traps (SIGSEGV)** | **traps (SIGSEGV)** |
| Pointer write by another thread | yes (worker writes `main`'s var, above) | yes |
| Clash if unmitigated | yes (compiler-codegen property) | yes |

Verified: `7_demos/main_stack_overflow.c` — the main stack grows down to the 8 MB
rlimit, then overflow **traps** below `[stack]`; it never reaches the pthread
stack (TBs away under ASLR).

### Interview soundbite (guard pages / clash)

> All thread stacks share one address space and are mutually writable via a
> pointer, but each is a separate mapping behind a guard page. *Incremental*
> overflow traps at the guard (SIGSEGV); a *single frame larger than the guard*
> can leap over it into a neighbour — the Stack Clash — unless the compiler's
> stack-clash probing (default on modern GCC) is present. The main thread's stack
> differs mechanically (a small, on-demand-growing `[stack]` VMA capped at
> `RLIMIT_STACK`) but carries the same three exposures.

**Demos:** `thread_stack_sharing.c` (pointer read/write), `thread_stack_overwrite.c`
(overwrite of a live frame), `thread_stack_layout.c` (stack gaps),
`stack_guard_page.c` (overflow trapped), `stack_clash.c` (guard skipped),
`main_stack_overflow.c` (main-thread overflow).

## Sources

- **OSTEP**, Ch. 26 "Concurrency: An Introduction," pp. 1-2 (per-thread stacks in
  a shared address space; Figure 26.1).
- **Silberschatz**, *Operating System Concepts* (10th ed.), Ch. 4 §4.1, p. 160
  (thread = basic unit of CPU utilization; shares code/data/resources with peers).
- **xv6 book**, Ch. 1, p. 21 (thread of execution; thread state on the thread's
  stacks).
- **Guard pages / Stack Clash** — empirically verified on this toolchain (see the
  demos listed above); background: CVE-2017-1000364 ("Stack Clash"), and
  `gcc -fstack-clash-protection` / kernel `stack_guard_gap`.

## Related notes

- [[kernel_vs_user_threads_and_linux_task_model]] — kernel vs. user threads, the
  Linux task model, and thread context switches.
