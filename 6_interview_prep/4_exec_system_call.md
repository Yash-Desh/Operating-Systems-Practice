# The `exec` System Call: Loading a Program Into a Process

- **Author:** Yash Deshpande
- **Date:** 07-08-2026
- **LLM Model:** Claude (Opus 5)

## Index

1. [Sources](#sources)
2. [What `exec` actually does](#2-what-exec-actually-does)
3. [Step-by-step](#3-step-by-step)
4. [What survives, what is destroyed](#4-what-survives-what-is-destroyed)
5. [The two design principles](#5-the-two-design-principles)
6. [Terminology map: generic name to xv6 name](#6-terminology-map-generic-name-to-xv6-name)
7. [Interview soundbite](#7-interview-soundbite)
8. [Related notes](#related-notes)

---

## Sources

**xv6 book**, `3_xv6/xv6-manual.pdf`. Printed page = PDF page (1:1).

| Chapter | Section | Printed / PDF pp. | Covers |
|---|---|---|---|
| **Ch. 2** — Page tables | Code: exec | 35–36 | ELF header and program section headers, per-segment load, stack + guard page, build-then-swap ordering, the integer-overflow check |
| **Ch. 1** — OS organization | The first system call: exec | 26–27 | `exec` replaces the memory image but keeps the process |

The body below is deliberately written in **generic OS terms** — the xv6 function
and constant names live only in [§5](#5-terminology-map-generic-name-to-xv6-name),
so the mechanism reads the same whether the kernel is xv6, Linux, or NT.

---

## 2. What `exec` actually does

`exec` replaces the memory image of the calling process with a new program
loaded from an executable file.

The process *identity* survives: its process ID, its open file descriptors, its
parent/child relationships. Everything about **what code it is running and what
memory it has** is thrown away and rebuilt. On success `exec` never returns —
the process resumes at the new program's entry point.

That split is the whole reason `fork` and `exec` are separate calls: `fork`
duplicates the process, `exec` re-aims it. The shell exploits the gap between
them to set up redirection in the child before the new program ever runs
(see [[2_init_the_first_user_process]]).

---

## 3. Step-by-step

### 1. Locate and open the executable

Resolve the pathname to a file in the filesystem and open it. If it does not
exist or is not readable, fail immediately — nothing has been modified yet.

### 2. Read and validate the file header

Executables use a structured binary format — ELF on Unix-likes, PE on Windows,
Mach-O on macOS. The file begins with a header identifying the format, typically
via a **magic number**: a fixed byte sequence at offset zero. If the magic does
not match, this is not a loadable executable; bail out.

The header also records the **entry point** — the virtual address where
execution should begin.

### 3. Read the program segment table

Following the header is a table of **segment descriptors**, each describing a
chunk of the file that must be mapped into memory.

| Field | Meaning |
|---|---|
| File offset | Where in the *file* the bytes live |
| Virtual address | Where in the *address space* they go |
| File size | How many bytes to copy from the file |
| Memory size | How much address space to reserve |
| Flags | Permissions: readable / writable / executable |

Typical segments: one for code, one for initialized data, sometimes one for
read-only constants. A minimal system may emit a single combined segment.

### 4. Build a fresh address space

Create a new, empty page table (or equivalent address-translation structure)
containing only the kernel's own mappings — no user mappings yet.

Critically, this is built **alongside** the existing address space, not on top
of it. The old one stays intact and the process keeps running on it.

### 5. Allocate and load each segment

For each descriptor:

- allocate physical memory covering the requested virtual range
- copy `file size` bytes from the file into it
- leave the remaining `memory size − file size` bytes **zeroed**

That gap exists because of uninitialized globals (the BSS). A program with a
10 MB zero-filled array should not need a 10 MB file — the format just says
"reserve this much, it starts as zeros."

### 6. Set up the stack

Allocate memory for the user stack, then build the initial frame the new program
expects:

- copy each argument string into stack memory
- build an array of pointers to those strings, null-terminated
- push the argument count, the pointer array, and a dummy return address,
  matching the calling convention for `main(argc, argv)`

The dummy return address is there purely so the frame *looks* like an ordinary
call frame; `main` is never expected to return through it.

Many kernels also place an **unmapped guard page** just below the stack. Two
benefits:

1. Stack overflow faults cleanly instead of silently corrupting whatever sits
   below it.
2. The copy routine that writes arguments hits an inaccessible page if the
   argument list is oversized — so "arguments too large" becomes an ordinary
   error return rather than a special-case length check.

### 7. Commit — and only then destroy the old image

Switch the process to the new page table, then free the old one. Set the saved
program counter to the entry point and the stack pointer to the new stack top.
When the kernel returns to user mode, the new program starts running.

---

## 4. What survives, what is destroyed

| Maintained | Destroyed |
|---|---|
| **Identity**<br>• Process ID (pid)<br>• Parent process ID (ppid)<br>• Process group ID, session ID<br>• Child processes | **Memory**<br>• Text, data, BSS<br>• Heap<br>• Stack<br>• All memory mappings, including `mmap`ed regions<br>• Shared memory attachments |
| **Credentials**<br>• Real user/group ID<br>• Effective user/group ID — *unless the binary is setuid/setgid*<br>• Supplementary groups<br>• umask | **Code-dependent state**<br>• Signal handlers → reset to default<br>• `atexit` handlers<br>• All threads except the calling one |
| **Files**<br>• Open file descriptors — *except those marked close-on-exec*<br>• File offsets and status flags<br>• Current working directory<br>• Root directory (if `chroot`ed)<br>• File locks | |
| **Resources**<br>• Resource limits (`rlimit`)<br>• Nice value, scheduling priority and policy<br>• CPU affinity<br>• Accumulated CPU times<br>• Controlling terminal | |
| **Signals**<br>• Signal mask (blocked signals)<br>• Pending signals<br>• Signals set to ignore | |

The dividing line: anything that is a **pointer into the old code or memory**
cannot survive, because the thing it points at is gone. Everything else is
kernel-side bookkeeping about the process, and has no reason to be reset.

### Three consequences worth knowing

**Signals split down that line.** The mask, the pending set, and "ignore this
signal" are all preserved — they are plain kernel state. An installed *handler*
is a function pointer into code that no longer exists, so it is forced back to
default. `SIG_IGN` and `SIG_DFL` are not addresses, so they carry over fine.

**File descriptors are inherited by default, closed by exception.** Only
descriptors explicitly marked close-on-exec are dropped. This is what makes
shell redirection work — see the fd-inheritance discussion in
[[2_init_the_first_user_process]].

**The survivors are the point of the `fork`/`exec` split.** Between the two
calls the child is still running the parent's code and can rearrange anything in
the left-hand column — reopen fd 1 onto a file, `chdir`, drop privileges, lower
its priority — and those edits persist into the new program. If `exec` reset
everything, it would need a dozen extra parameters describing the desired
starting state. Instead, the child's own execution *is* the configuration
language.

xv6 is a simplified case of this table: it has no threads, no `mmap`, no
setuid, no close-on-exec flag, and no per-process signal state. What it does
preserve is the process-table slot itself — the pid, parent pointer, open file
table, and working directory all live in that struct, and `exec` only swaps the
page table and trapframe inside it.

---

## 5. The two design principles

### Build-then-swap

Every step before #7 operates on a *new* address space while the old one is
still live and valid. This is what makes the error path work:

> If a segment is malformed, or an allocation fails, the kernel discards the
> half-built image and returns an error to the **original** program, which is
> still fully functional.

Had it freed the old image first, there would be nothing left to return an error
*to* — the process would be unrecoverable. So the ordering constraint is: **all
failures must be arranged to happen before the commit point.** The manual states
it directly (Ch. 2, p. 36):

> "Exec must wait to free the old image until it is sure that the system call
> will succeed: if the old image is gone, the system call cannot return –1 to
> it."

This is the same shape of argument as the zombie state in
[[3_process_teardown]]: you cannot free the ground you are standing on, so the
teardown is deferred until something else is standing somewhere safe.

### Executable files are untrusted input

The addresses in the header are chosen by whoever produced the file. A malicious
program can claim any virtual address it likes — **including addresses belonging
to the kernel**. Every one must be validated before use.

The subtle part is that a bounds check can be defeated by **integer overflow**:

| Step | What happens |
|---|---|
| The check | Loader verifies `virtual_address + memory_size` is below the kernel boundary |
| The attack | Attacker sets `virtual_address` inside the kernel, and `memory_size` large enough that the sum wraps past the maximum integer to a small value |
| The bypass | The wrapped sum is small, so it passes the check |
| The payoff | A later stage uses `virtual_address` *on its own*, unchecked, and copies attacker bytes straight into kernel memory |

Result: arbitrary code execution at kernel privilege.

The fix is an explicit overflow test — verify the sum is not **less than** either
operand, since wraparound is the only way that can happen. More generally:

- check every value that came from the file
- check for overflow in every arithmetic operation on those values
- never assume a value validated at one layer stays valid when passed to another

Real kernels have a long history of privilege-escalation bugs from exactly one
missing check on this path.

---

## 6. Terminology map: generic name to xv6 name

| Generic term used above | xv6 name | Ref. |
|---|---|---|
| Resolve pathname | `namei` | p. 35 |
| File header | `struct elfhdr` | `elf.h` |
| Segment descriptor | `struct proghdr` | `elf.h` |
| Magic number | `ELF_MAGIC` = `0x7F 'E' 'L' 'F'` | p. 35 |
| New page table, kernel mappings only | `setupkvm` | p. 35 |
| Allocate memory for a segment | `allocuvm` | p. 35 |
| Copy segment bytes from file | `loaduvm` (via `walkpgdir` + `readi`) | p. 35 |
| Copy args into the new stack | `copyout` | p. 36 |
| Argument frame being built | `ustack` | p. 36 |
| Kernel/user boundary address | `KERNBASE` | p. 35 |
| `file size` / `memory size` | `filesz` / `memsz` | p. 36 |
| The overflow check | `if(ph.vaddr + ph.memsz < ph.vaddr)` | p. 36 |
| Error path | `goto bad` → free new image, return −1 | p. 36 |

xv6 emits **one** program section header per binary; production toolchains emit
several with distinct permissions. Worked example from the manual (p. 35–36) —
`/init` has `filesz` 2240 and `memsz` 2252, so 2252 bytes are allocated but only
2240 are read, and the trailing 12 bytes are zeroed globals.

---

## 7. Interview soundbite

> `exec` swaps a process's memory image without swapping the process — same pid,
> same open files, new code. It opens the binary, checks the magic number, walks
> the segment table, and for each segment allocates the requested virtual range,
> copies `filesz` bytes in, and zero-fills up to `memsz` for the BSS. Then it
> builds a stack page with `argv` laid out for `main`, plus an unmapped guard
> page below it so overflow faults and oversized argument lists fail cleanly.
> The key ordering property is build-then-swap: the whole new image is
> constructed on a *separate* page table while the old one is still live, and
> only after every step that can fail has succeeded does it install the new
> table and free the old. Otherwise a failure would have no caller left to
> return −1 to. And because the addresses come from an attacker-controllable
> file, every one gets bounds-checked against the kernel boundary *including* an
> explicit integer-overflow test — skip that and a wrapped `vaddr + memsz` sails
> through the range check and copies user bytes into the kernel.

---

## Related notes

- [[2_init_the_first_user_process]] — the first `exec` in the system, and the
  fork-and-exec loop that uses this call.
- [[3_process_teardown]] — the same "cannot free what you are standing on"
  constraint, applied to process exit.
- [[1_os_boot_to_first_process]] — how the kernel address space that
  step 4 clones already exists before any user program runs.
