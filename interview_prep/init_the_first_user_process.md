# `init`: The First User Process and What It Does

- **Author:** Yash Deshpande
- **Date:** 05-08-2026
- **LLM Model:** Claude (Opus 5)

## Index

1. [Sources](#sources)
2. [What `init` is](#2-what-init-is)
3. [The duties](#3-the-duties)
4. [Why `init` may never exit](#4-why-init-may-never-exit)
5. [Interview soundbite](#5-interview-soundbite)

---

## Sources

**xv6 book**, `xv6-manual.pdf` at the repo root. Printed page = PDF page (1:1).

| Chapter | Section | Printed pp. | PDF pp. | Covers |
|---|---|---|---|---|
| **Ch. 0** — OS interfaces | File descriptors | 10–13 | 10–13 | fd 0/1/2 convention, `dup`, inheritance across `fork`/`exec` |
| **Ch. 1** — OS organization | The first system call: exec | 26–27 | 26–27 | `initcode` execs `/init`; console opened as fds 0/1/2; "The system is up" |
| **Ch. 5** — Scheduling | Code: Wait, exit, and kill | 71–72 | 71–72 | zombie state, reparenting to `init`, why the parent frees the child |

The manual's one-paragraph summary of this program is Ch. 1, p. 27:

> "Init creates a new console device file if needed and then opens it as file
> descriptors 0, 1, and 2. Then it loops, starting a console shell, handles
> orphaned zombies until the shell exits, and repeats. The system is up."

---

## 2. What `init` is

`/init` is pid 1 — the first program loaded from the filesystem, and the ancestor
of every other process on the system. It is the third thing pid 1 runs, not the
first: the kernel hand-forges the process, that process runs a few instructions
linked into the kernel image, and *those* `exec` `/init`. The pid never changes
across the sequence — `exec` swaps the memory image but keeps the process.

The program is tiny — roughly 25 lines of real code — and its whole job is to
bring the system from "kernel is running" to "user can type a command."

---

## 3. The duties

**1. Create the console device file, if it does not already exist.**
   Makes a device node with major number 1, the console driver, so the terminal
   is reachable through the ordinary file API rather than a special case.

**2. Open the console and establish file descriptors 0, 1, and 2.**
   Opens the console — which returns fd 0, the lowest free descriptor — then
   duplicates it twice to produce fds 1 and 2.
   - This is the **origin of stdin, stdout, and stderr**. They are not
     kernel-provided and not magic; they exist only because `init` opened them
     before anything else ran.
   - All three refer to the *same* open file, which is why typed input and
     program output share one channel.
   - Every later process inherits them through `fork`, and `exec` preserves the
     descriptor table — which is why any program can print without opening
     anything itself.

**3. Fork a child and `exec` the shell in it.**
   The parent keeps being `init`; the child becomes the shell. Because the shell
   inherits fds 0/1/2, it can read commands and write results immediately.

**4. Restart the shell forever.**
   The fork-and-exec sits inside an infinite loop. If the shell exits — end of
   input, or a crash — `init` loops and starts a fresh one. The system never
   becomes unusable because a shell died.

**5. Wait for children, and distinguish the shell from everyone else.**
   The wait loop keeps reaping until the pid it gets back is the shell's; any
   other pid means an adopted orphan, which it reaps and then keeps waiting.
   Only the shell's death breaks the loop and triggers a restart.

**6. Reap orphaned processes — the system's garbage collector for pids.**
   When any process exits, the kernel reparents its surviving children to `init`.
   So every orphan eventually becomes `init`'s child, and `init`'s wait loop
   reclaims it.
   - This is what guarantees **every process always has a parent to clean up
     after it** (Ch. 5, p. 71: "If the parent exits before the child, the init
     process adopts the child and waits for it").
   - Reaping is not cosmetic. A process that has exited but not been waited for
     stays in the zombie state, holding a slot in the fixed-size process table.
     Without `init`, those slots would leak until the table filled.
   - The reason the *parent* must do the freeing is that a dying process is still
     running on its own kernel stack and page table and cannot release them
     itself — see [[os_startup_boot_to_first_process]] §9, "Teardown."

**7. Report failures to the console.**
   Prints a notice each time it starts a shell, and distinct messages if `fork`
   or `exec` fails. On a failed `fork` it gives up and exits — which, per the
   next section, deliberately brings the system down.

---

## 4. Why `init` may never exit

The kernel refuses to let pid 1 die: the exit path checks whether the caller is
the init process and **panics** if so, rather than allowing it.

Two things break if `init` disappears:

- Nobody is left to reap orphans, so the process table leaks until it is full.
- Nobody restarts the shell, so the machine is inert even though the kernel is
  fine.

Linux behaves the same way — killing pid 1 panics the kernel.

---

## 5. Interview soundbite

> `init` is pid 1, the first program loaded from disk and the ancestor of every
> process. It does three things: it opens the console as file descriptors 0, 1,
> and 2 — which is literally where stdin, stdout, and stderr come from, since
> everything else inherits them through `fork` — it forks and execs a shell in an
> infinite loop so the system always has one, and it reaps orphans. Orphans reach
> it because the kernel reparents a dying process's children to `init`, which is
> what guarantees every process has a parent to clean up after it and keeps the
> process table from leaking zombie slots. The kernel panics if pid 1 ever exits.

---

## Related notes

- [[os_startup_boot_to_first_process]] — the full boot chain that produces pid 1,
  and the teardown path that reparents orphans to it.
- [[kernel_vs_user_threads_and_linux_task_model]] — how Linux generalizes process
  creation through `clone()`.
