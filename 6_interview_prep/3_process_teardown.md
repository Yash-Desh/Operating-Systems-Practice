# Process Teardown: Exit, Zombie, and Reap

- **Author:** Yash Deshpande
- **Date:** 06-08-2026
- **LLM Model:** Claude (Opus 5)

## Index

1. [Sources](#sources)
2. [Teardown: the other half of the lifecycle](#2-teardown-the-other-half-of-the-lifecycle)
3. [Related notes](#related-notes)

---

## Sources

**xv6 book**, `3_xv6/xv6-manual.pdf`. Printed page = PDF page (1:1).

| Chapter | Section | Printed / PDF pp. | Covers |
|---|---|---|---|
| **Ch. 5** — Scheduling | Code: Wait, exit, and kill | 71–72 | zombie state, reparenting to `init`, why the parent frees the child |

---

## 2. Teardown: the other half of the lifecycle

Creation's mirror image, and it explains a state that otherwise looks like a
wart.

**Exit** closes the process's open files, releases its working directory, wakes
its parent, **reparents its children to the init process**, marks itself a
zombie, and jumps into the scheduler, never to return. The init process exiting
is a panic — pid 1 dying kills the system.

**Wait** does the actual freeing: the kernel stack, the page table, and the
process-table slot.

**Why the parent must do it** (Ch. 5, p. 71):

> "It is important that the parent process be the one to free `p->kstack` and
> `p->pgdir`: when the child runs exit, its stack sits in the memory allocated as
> `p->kstack` and it uses its own pagetable. They can only be freed after the
> child process has finished running for the last time by calling `swtch` (via
> `sched`). **This is one reason that the scheduler procedure runs on its own
> stack** rather than on the stack of the thread that called `sched`."

A process cannot free the ground it is standing on. The zombie state exists
**entirely** because of that — and the scheduler's independent stack, the one
`entry` set up in stage 3 of [[1_os_boot_to_first_process]], is what makes the
reclamation possible. Boot and teardown close the loop.

Reparenting orphans to init is why every process always has a parent to reap it.

---

## Related notes

- [[1_os_boot_to_first_process]] — the boot chain that sets up the scheduler
  stack this teardown depends on.
- [[2_init_the_first_user_process]] — the process that adopts the orphans
  reparented here.
- [[6_divide_by_zero_fault_to_reaped_process]] — a concrete end-to-end path that
  ends in this teardown.
