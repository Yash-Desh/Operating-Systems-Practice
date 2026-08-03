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
[`demos/thread_stack_sharing.c`](../demos/thread_stack_sharing.c).

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

## Sources

- **OSTEP**, Ch. 26 "Concurrency: An Introduction," pp. 1-2 (per-thread stacks in
  a shared address space; Figure 26.1).
- **Silberschatz**, *Operating System Concepts* (10th ed.), Ch. 4 §4.1, p. 160
  (thread = basic unit of CPU utilization; shares code/data/resources with peers).
- **xv6 book**, Ch. 1, p. 21 (thread of execution; thread state on the thread's
  stacks).
