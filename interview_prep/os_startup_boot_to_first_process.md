# How the OS Starts Up: From Power-On to the First User Process

- **Author:** Yash Deshpande
- **Date:** 05-08-2026
- **LLM Model:** Claude (Opus 5)

## Index

1. [Sources](#sources)
2. [The five stages](#2-the-five-stages)
3. [Stage 1: BIOS and the boot sector](#3-stage-1-bios-and-the-boot-sector)
4. [Stage 2: The boot loader](#4-stage-2-the-boot-loader)
5. [Stage 3: `entry` — the kernel's first address space](#5-stage-3-entry--the-kernels-first-address-space)
   - [The 4 MB is a ceiling, not a truncation](#the-4-mb-is-a-ceiling-not-a-truncation)
6. [Stage 4: `main` — building the real kernel](#6-stage-4-main--building-the-real-kernel)
7. [Stage 5: Fabricating the first process](#7-stage-5-fabricating-the-first-process)
8. [The three stacks](#8-the-three-stacks)
9. [Teardown: the other half of the lifecycle](#9-teardown-the-other-half-of-the-lifecycle)
10. [What is xv6-specific vs. universal](#10-what-is-xv6-specific-vs-universal)
11. [Interview soundbite](#11-interview-soundbite)

---

## Sources

**xv6 book**, `xv6-manual.pdf` at the repo root. Printed page = PDF page (1:1).

| Chapter / Appendix | Printed pp. | PDF pp. |
|---|---|---|
| **Appendix B** — The boot loader | 99–103 | 99–103 |
| **Ch. 1** — Operating system organization | 20–27 | 20–27 |
| **Ch. 2** — Page tables | 29–37 | 29–37 |
| **Ch. 3** — Traps, interrupts, and drivers | 41 | 41 |
| **Ch. 5** — Scheduling | 71–72 | 71–72 |
| **Silberschatz**, §2.9.2 System Boot | 93–95 | 123–125 |

---

## 2. The five stages

Boot is a chain of handoffs, each stage building just enough machinery to reach
the next.

| # | Stage | Runs in | Ends by |
|---|---|---|---|
| 1 | BIOS | 16-bit real mode | loading the boot sector at `0x7c00` |
| 2 | Boot loader | real → 32-bit protected mode | jumping to the kernel's ELF entry point |
| 3 | `entry` | 32-bit, paging off → on | jumping to `main` at a high address |
| 4 | `main` | 32-bit, paging on | calling `userinit`, then `scheduler` |
| 5 | First process | user mode | `exec`ing `/init`, which forks a shell |

---

## 3. Stage 1: BIOS and the boot sector

Power-on leaves the CPU in **16-bit real mode**, where there is no memory
protection and addresses are formed by segment-shift-plus-offset arithmetic.
The BIOS initializes hardware, then loads the first 512-byte disk sector to
physical `0x7c00` and jumps there.

The manual is candid that this stage is heavily abstracted (Appendix B, p. 103):

> "This appendix is written as if the only thing that happens between power on
> and the execution of the boot loader is that the BIOS loads the boot sector.
> In fact the BIOS does a huge amount of initialization... The BIOS is really a
> small operating system embedded in the hardware."

**Constraint that shapes everything:** 512 bytes. That is why the boot loader is
split into assembly and C halves, and why it cannot parse a filesystem.

---

## 4. Stage 2: The boot loader

Two files, two jobs.

**Assembly half** — get into a sane execution mode:

1. Disable interrupts (no handlers exist yet).
2. Zero the segment registers.
3. Enable the **A20 line** — a legacy hack that pinned the 21st address bit to
   zero for 1980s compatibility. Until it is undone, the machine cannot address
   past 1 MB.
4. Load a **bootstrap GDT** whose segments all have base 0 and maximum limit, so
   logical addresses map straight through to physical ones. This neutralizes
   segmentation so that paging can be the only translation that matters later.
5. Set the protection-enable bit → **32-bit protected mode**.
6. Point the stack somewhere unused and call into C.

**C half** — load the kernel:

1. Read the first page off disk and check the **ELF magic number**.
2. Walk the program headers, reading each segment to the physical address the
   header specifies, zero-filling any `memsz > filesz` tail (that is the BSS).
3. Read the entry point out of the ELF header and call it. Never returns.

Two design consequences worth naming:

- **No filesystem.** The kernel must sit contiguously starting at sector 1. A
  real two-stage loader would use this stage only to load a larger loader.
- **Polling, not interrupts.** Disk reads busy-wait on a status port. Acceptable
  here precisely because there is nothing else to run.

**Why the kernel lands at physical `0x100000`** (Ch. 1, p. 22):

- Not at `0x80100000`, where the kernel *expects* to be, because "there may not
  be any physical memory at such a high address on a small machine."
- Not at `0x0`, because "the address range `0xa0000:0x100000` contains I/O
  devices."

That single decision creates the problem the next stage exists to solve.

---

## 5. Stage 3: `entry` — the kernel's first address space

### The problem

The kernel is **linked high but loaded low**:

- Its linker script places it at `0x80100000`, so every symbol — every function,
  global, and the boot stack — carries a high address.
- Its bytes physically sit at `0x100000`.
- Paging is off, so virtual equals physical, and `0x80100000` refers to memory
  2 GB up that does not exist.
- The boot loader had to jump using the **physical** address, so the instruction
  pointer is currently low.

Both facts must be true at once: the processor is executing low, and every
symbol it names is high. `entry` is the bridge.

### The bridge: one page directory, two entries

The boot page directory is a **statically initialized array compiled into the
kernel binary** — not built at runtime. That is the whole trick: nothing needs
allocating, because no allocator exists yet.

Only 2 of its 1024 slots are live:

| Slot | Virtual range | → Physical range | Role |
|---|---|---|---|
| `0` | `0` – `0x400000` | `0` – `0x400000` | identity map; keeps the low-running code alive **during** the switch |
| `512` | `0x80000000` – `0x80400000` | `0` – `0x400000` | the high addresses the kernel was linked for |

Both point at the **same physical memory**. The manual flags this as a general
technique (Ch. 1, p. 22):

> "Setting up two ranges of virtual addresses that map to the same physical
> memory range is a common use of page tables, and we will see more examples
> like this one."

Slot 512 because the kernel base `0x80000000`, shifted right by the 22-bit
page-directory shift, is 512 — the exact midpoint of 1024 slots, since the base
sits at 2 GB of a 4 GB space.

### Why 4 MB, and why superpages

A 32-bit virtual address splits **10 bits directory / 10 bits table / 12 bits
offset**. One directory entry therefore governs whatever the low 22 bits can
express: `2^22` = `0x400000` = **4 MB**. Normally those 22 bits are subdivided by
a second-level table; setting the page-size bit says *do not subdivide* — map the
whole 4 MB flat.

That is what makes the static array possible. A two-level table would need a
second page allocated at runtime. So the 4 MB ceiling the manual mentions is not
a design target — it is simply one directory entry's reach.

> Hex sanity check: `0x100000` = 1 MB, so strip five hex zeros and read the MB
> count directly. `0x400000` → `4` → 4 MB. `0xE000000` → `0xE0` = 224 MB.

### The 4 MB is a ceiling, not a truncation

A natural misreading of "restricts the kernel instructions and data to 4 Mbytes"
is that the boot loader loads a large kernel and the boot table can only reach
the first slice of it, leaving the rest present-but-unmapped until the real page
table arrives. **That is not what happens.** The entire kernel sits inside the
window from the very first instruction, with room to spare.

Measured on the built kernel in this repo (`p1234/CS537/p2/solution/kernel`),
`readelf -l` reports exactly two loadable segments — and note the link-vs-load
split visible in the two address columns:

| Segment | Virtual | Physical | Size |
|---|---|---|---|
| `.text` + `.rodata` | `0x80100000` | `0x00100000` | `0x07c6f` |
| `.data` + `.bss` | `0x80108000` | `0x00108000` | `0x0d4d0` (memsz) |

Nothing is loaded above `0x1154d0`. Cross-checked two ways: `nm` puts the `end`
symbol at `0x801154d0`, which minus the kernel base is exactly the top of the
second segment.

So the window's 4 MB breaks down as:

| Region | Physical | Size |
|---|---|---|
| BIOS / I/O hole — mapped but unusable | `0x0` – `0x100000` | 1.00 MB |
| **The whole kernel** | `0x100000` – `0x1154d0` | **87 KB** |
| Free RAM above the kernel | `0x1154d0` – `0x400000` | 2.92 MB |

Two conclusions:

1. **The kernel is never partially mapped.** All 87 KB is reachable immediately —
   which it has to be, since `main` calls into subsystems scattered across the
   whole text section. The 4 MB figure bounds how large the kernel *could* grow
   before boot breaks; it does not describe how much is currently visible.
2. **The usable budget is ~3 MB, not 4 MB.** The window starts at the kernel base
   but the kernel is *linked* 1 MB higher, at `KERNLINK` (the linker script
   comments the link address as "Must be equal to KERNLINK"). That first
   megabyte is the I/O hole the boot loader had to skip. The manual's "4 Mbytes"
   states one page-directory entry's reach, not the remainder after the hole.

The 2.92 MB of slack is not idle. `kinit1` walks from the `end` symbol to the
4 MB mark and frees every page into the allocator — which is the **only** reason
the next line has pages available to build the real kernel page table. The
bootstrap depends on the window being far larger than the kernel.

### The five steps

1. **Enable page-size extension.** The superpage bit is inert without it.
2. **Load the page-directory register** with the table's *physical* address,
   computed by subtracting the kernel base. The manual on why (Ch. 1, p. 23):
   > "It wouldn't make sense for `%cr3` to hold the virtual address of
   > `entrypgdir`, because the paging hardware doesn't know how to translate
   > virtual addresses yet; it doesn't have a page table yet."
3. **Turn paging on.** The critical instant — the *next* instruction fetch goes
   through the page table while the instruction pointer is still low. The
   identity mapping is the only reason it resolves (Ch. 1, p. 23):
   > "If xv6 had omitted entry 0 from `entrypgdir`, the computer would have
   > crashed when trying to execute the instruction after the one that enabled
   > paging."

   With no interrupt table installed yet, that fault would cascade into a triple
   fault and reboot. **This is the single most important question about this
   stage.**
4. **Set the stack pointer** to a static buffer in the kernel's BSS. Because it
   is a kernel symbol its address is high — chosen deliberately, so the stack
   survives the low mapping's removal (Ch. 1, p. 23):
   > "All symbols have high addresses, including `stack`, so the stack will
   > still be valid even when the low mappings are removed."
5. **Jump indirectly** to the C entry point — load its absolute address into a
   register, then jump through the register. A direct jump assembles to a
   **PC-relative** displacement; with the instruction pointer still low, relative
   arithmetic would land in the low copy. Correct code, wrong address.

After step 5 both the stack pointer and instruction pointer are high. The
crossing is complete. The jump was a jump, not a call, so nothing was pushed —
the C entry point has no return address and can never return.

---

## 6. Stage 4: `main` — building the real kernel

`main` replaces the bootstrap scaffolding almost immediately (Ch. 2, p. 29):

> "The page table created by `entry` has enough mappings to allow the kernel's C
> code to start running. However, `main` immediately changes to a new page table
> by calling `kvmalloc`, because kernel has a more elaborate plan for describing
> process address spaces."

### The allocator/page-table circular dependency

Building the real page table requires allocating pages; initializing the
allocator requires being able to address memory. The manual states it plainly
(Ch. 2, p. 32):

> "There is a bootstrap problem: all of physical memory must be mapped in order
> for the allocator to initialize the free list, but creating a page table with
> those mappings involves allocating page-table pages."

Broken with **two passes**: claim only what the 4 MB boot mapping can see, build
the real table, then claim the rest. The two-phase allocator init exists solely
because of the boot table's 4 MB reach.

What the first pass claims is the 2.92 MB of slack measured above — the gap
between the kernel's `end` symbol and the top of the boot window. The manual's
assessment (Ch. 2, p. 32) that this allocator "is limited by the 4 MB mapping in
the `entrypgdir`, but that is sufficient to allocate the first kernel page table"
is a statement about that slack: 3 MB of headroom against an 87 KB kernel is
ample.

### Boot table vs. real table

| | Boot table (`entrypgdir`) | Real table (`kpgdir`) |
|---|---|---|
| Origin | static array in the binary | allocated at runtime |
| Entries | 2 | four described ranges, expanded page by page |
| Granularity | 4 MB superpages | 4 KB pages |
| Reach above kernel base | 4 MB | all RAM + device space |
| Identity map at VA 0 | **yes** | **no** — this is the teardown |
| Permissions | everything writable | text/rodata read-only, data writable |

The identity map is not explicitly unmapped; the fresh table is simply built
without it. The permission split is the other real upgrade — one superpage
covering both text and data could not mark code read-only.

Note what the real table does **not** add: more of the kernel. The kernel was
fully mapped all along. The upgrade is about reaching more *RAM* (all of it
instead of 4 MB), reaching devices, and protecting things properly — not about
finally revealing kernel code that had been invisible.

The boot table is **demoted, not destroyed**. It is a static array and cannot be
freed, and additional CPUs still need it: each one starts in low memory and must
repeat the same low→high crossing, so each needs the same identity map.

### The kernel in every address space

Every page table in the system — one per process, plus the kernel's own — is
built from the **same description of the kernel half**. One physical kernel,
identical virtual→physical translations replicated into every process
(Ch. 2, p. 31):

> "Having every process's page table contain mappings for both user memory and
> the entire kernel is convenient when switching from user code to kernel code
> during system calls and interrupts: such switches do not require page table
> switches. For the most part the kernel does not have its own page table; it is
> almost always borrowing some process's page table."

Two consequences:

- **Syscalls need no page-table switch.** A trap raises privilege and swaps
  stacks; the page-table register is untouched. No reload, no TLB flush.
- **Protection is a bit, not absence.** The user-accessible flag is clear on
  every kernel mapping (Ch. 2, p. 31): "only the kernel can use them." The kernel
  is *present but unreachable* from user mode — which is why kernel code can
  dereference user pointers directly.

> Caveat for the modern world: Meltdown broke this. The permission bit stopped
> architectural access but not *speculative* access. Linux's KPTI now gives each
> process two page tables — a user-mode one holding only a trampoline — so a
> syscall *does* switch the page-table register, trading latency for real
> isolation.

Note that the caller order matters: the last subsystem initialized before the
first process is the one the first process will immediately need.

---

## 7. Stage 5: Fabricating the first process

Every other process is born by copying a parent. There is no parent here, so the
kernel **hand-forges** one. The design principle (Ch. 1, p. 24):

> "`allocproc` is written so that it can be used by fork as well as when creating
> the first process... This setup is the same for ordinary fork and for creating
> the first process, though in the latter case the process will start executing
> at user-space location zero rather than at a return from fork."

### Allocating the slot

Scan the process table for an unused entry, mark it **EMBRYO** to claim it,
assign a pid, allocate a kernel stack.

### Forging the kernel stack

The stack is laid out so the process "returns" into code it never called
(Figure 1-4, p. 23). From the top down: a trap frame, then the address of the
trap-return routine, then a saved context whose instruction pointer is the
fork-return routine.

When the scheduler switches to it, the context is restored, execution begins at
the fork-return routine, and *its* return address — planted just above — is the
trap-return routine, which pops the trap frame into real registers.

### Forging the trap frame

No trap ever occurred, so the kernel writes what one *would* have left behind
(Ch. 1, p. 25):

> "`userinit` writes values at the top of the new stack that look just like those
> that would be there if the process had entered the kernel via an interrupt."

User-mode code and data segments, interrupts enabled, stack pointer at the top
of the single user page, instruction pointer at **virtual address 0**.

### The program that isn't a file

The first process's program is not loaded from disk — it is a handful of
instructions **linked into the kernel image**, copied into a freshly allocated
page mapped at virtual 0.

Mark the process runnable. The scheduler picks it up, the forged stack unwinds
through fork-return and trap-return, and the trap-return instruction drops the
CPU into user mode at address 0.

One wrinkle: the fork-return routine runs filesystem initialization **on its
first invocation only**, because those routines can sleep and sleeping requires
a process context that `main` does not have. So the filesystem becomes usable
only after the first process is scheduled.

### The first system call

That tiny program does exactly one thing: push arguments and trap, requesting
`exec` of `/init`. Again, no special case (Ch. 1, p. 26):

> "This code manually crafts the first system call to look like an ordinary
> system call... this setup avoids special-casing the first process (in this
> case, its first system call), and instead reuses code that xv6 must provide for
> standard operation."

`exec` reads the real `/init` ELF from disk, builds a new page table, loads each
segment, constructs a user stack holding the argument vector, then **commits** by
swapping in the new page table and freeing the old. Same process slot, entirely
new memory image.

### The system is up

`/init` opens the console and duplicates that descriptor twice — **this is where
stdin, stdout, and stderr come from**; someone had to open them first. Then it
loops forever: fork, exec a shell, wait. The manual's closing line (Ch. 1, p. 27):

> "Init creates a new console device file if needed and then opens it as file
> descriptors 0, 1, and 2. Then it loops, starting a console shell, handles
> orphaned zombies until the shell exits, and repeats. **The system is up.**"

---

## 8. The three stacks

Easy to conflate, because the manual introduces them in different chapters.

| Stack | Where it lives | Whose | Exists from |
|---|---|---|---|
| Boot / scheduler | static buffer in the kernel's BSS, one per CPU | no process — the CPU itself | `entry`, forever |
| Kernel (`p->kstack`) | allocated per process | that process, while in the kernel | process creation |
| User | in the process's own address space | that process, in user mode | process creation |

The boot stack **is not** a user stack and **is not** a process kernel stack — at
the time it is set up, no process exists. It is simply a region of memory plus a
register pointing at it, which is all a stack ever is.

It is never abandoned. `main` never returns and ends in the scheduler, which
never returns either — so that buffer becomes the CPU's **scheduler stack**
permanently. That fact is load-bearing for teardown (see below).

The per-process pair is described in Ch. 1, p. 21:

> "Each process has two stacks: a user stack and a kernel stack... The kernel
> stack is separate (and protected from user code) so that the kernel can execute
> even if a process has wrecked its user stack."

---

## 9. Teardown: the other half of the lifecycle

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
`entry` set up in stage 3, is what makes the reclamation possible. Boot and
teardown close the loop.

Reparenting orphans to init is why every process always has a parent to reap it.

---

## 10. What is xv6-specific vs. universal

Three layers, and they separate cleanly.

**The problem is universal.** Any kernel linked high but loaded low faces this,
and nearly all kernels want to be high so the kernel mapping can be shared across
every address space.

**The solution shape is the industry standard.** Identity-map → cross → tear down
is what Linux's early boot assembly does on both i386 and x86-64. On x86-64 the
identity map is not merely convenient but *architecturally required*, since
entering long mode demands paging already on. Even the indirect-jump trick is
standard, being a property of how x86 encodes relative jumps.

**The implementation is teaching-scale.** What would not survive contact with a
modern machine:

| xv6 | Reality |
|---|---|
| 2-entry static array, 4 MB superpages | multi-level tables built at runtime |
| plain 32-bit 2-level paging | PAE (3-level) or x86-64 (4–5 level), 64-bit entries, NX bit |
| kernel base fixed at 2 GB | different split per OS; x86-64 uses a canonical hole |
| link address hardcoded | KASLR relocates at boot |
| legacy BIOS handoff, paging off | UEFI hands off with paging already on and its own tables |
| memory top hardcoded | probed from the firmware memory map |
| no PAE / NX / SMEP / SMAP / PCID | all standard |

**Interview framing:** explain the mechanism, then volunteer where it stops
applying. Presenting it as universal invites one follow-up question and falls
over; naming the boundary reads as understanding both the shape *and* its scope.

---

## 11. Interview soundbite

> The BIOS loads a 512-byte boot sector, which switches to 32-bit protected mode
> and reads the kernel ELF off disk to physical `0x100000` — low, because a small
> machine may have no RAM 2 GB up and the region under 1 MB is I/O space. But the
> kernel was *linked* high, so every symbol currently resolves to nothing. The
> kernel's entry code installs a prebuilt two-entry page directory: an identity
> map so the low-running code survives the instant paging turns on, and one at the
> kernel base mapping those high addresses onto the low physical bytes. It points
> the stack at a high symbol and jumps *indirectly* to C, because a direct jump is
> PC-relative and would land in the low copy. Then it builds the real page table
> without the identity map. For the first process there is no parent to copy, so
> the kernel forges a kernel stack and a trap frame that look exactly like a
> process that trapped in, and "returns" it to user space running a few
> instructions linked into the kernel image — which immediately `exec`s `/init`,
> which opens the console as fds 0/1/2 and forks a shell.

**The one question to be airtight on:** *why is the identity mapping needed?*
Because the instruction pointer is still low when paging turns on, so the very
next instruction fetch goes through the page table at a low address. Without it,
you fault with no handlers installed and the machine triple-faults.

---

## Related notes

- [[kernel_vs_user_threads_and_linux_task_model]] — the Linux task model, and how
  `clone()` generalizes the process creation described here.
- [[threads_share_stack_memory]] — the per-thread stacks that live inside the
  user half of the address space set up in stage 5.
