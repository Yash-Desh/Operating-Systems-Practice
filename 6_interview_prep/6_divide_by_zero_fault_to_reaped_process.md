# Divide by Zero: From Faulting Instruction to Reaped Process

- **Author:** Yash Deshpande
- **Date:** 06-08-2026
- **LLM Model:** Claude (Opus 5)

## Index

1. [Sources](#sources)
2. [The setup that exists before you run](#2-the-setup-that-exists-before-you-run)
3. [Step 1: The instruction faults](#3-step-1-the-instruction-faults)
4. [Step 2: Hardware crosses into kernel mode](#4-step-2-hardware-crosses-into-kernel-mode)
5. [Step 3: The entry stub captures register state](#5-step-3-the-entry-stub-captures-register-state)
6. [Step 4: The trap handler classifies the event](#6-step-4-the-trap-handler-classifies-the-event)
7. [Step 5: Why the kill is deferred](#7-step-5-why-the-kill-is-deferred)
8. [Step 6: The check at the kernel exit boundary](#8-step-6-the-check-at-the-kernel-exit-boundary)
9. [Step 7: Exit dismantles the process](#9-step-7-exit-dismantles-the-process)
10. [Step 8: The parent reaps the corpse](#10-step-8-the-parent-reaps-the-corpse)
11. [What real Linux does differently](#11-what-real-linux-does-differently)
12. [Fault vs. trap vs. abort](#12-fault-vs-trap-vs-abort)
13. [Interview soundbite](#13-interview-soundbite)

---

## Sources

**xv6 book**, `xv6-manual.pdf` at the repo root. Printed page = PDF page (1:1).

| Chapter                                       | Section                                   | Printed / PDF pp. | Covers                                                                    |
| --------------------------------------------- | ----------------------------------------- | ----------------- | ------------------------------------------------------------------------- |
| **Ch. 3** — Traps, interrupts, drivers | Systems calls, exceptions, and interrupts | 39–41            | trap vs. interrupt distinction; why the saved`%eip` matters             |
| **Ch. 3**                               | X86 protection                            | 41–42            | IDT, gate privilege, TSS stack switch, what hardware pushes               |
| **Ch. 3**                               | Code: Assembly trap handlers              | 42–43            | `vectors.S`, `alltraps`, the trap frame layout (Fig. 3-2)             |
| **Ch. 3**                               | Code: C trap handler                      | 44                | the`default:` case, `(tf->cs & 3)` user/kernel test, `proc->killed` |
| **Ch. 5** — Scheduling                 | Code: Wait, exit, and kill                | 71–72            | deferred kill, zombie state, why the*parent* frees the stack            |

**Silberschatz**, *Operating System Concepts*, 10th ed.
The printed→PDF offset is **not constant** in this book (it drifts from +28 in Ch. 1 to
+142 in Ch. 21), so each row below was verified individually.

| Section                                           | Printed p. | PDF p.   | Covers                                                                      |
| ------------------------------------------------- | ---------- | -------- | --------------------------------------------------------------------------- |
| **§1.2.1** Interrupts                      | 8–10      | 36–38   | interrupt vector, ISR dispatch, saving state                                |
| **§4.6.1** Signal Handling                 | 188        | 245      | divide by 0 as a**synchronous** signal; generate → deliver → handle |
| **§12.2.2** Interrupts                     | 494        | 622      | trap/exception vs. device interrupt; vectored dispatch                      |
| **§20.5.3** Kernel Synchronization (Linux) | 793–794   | 933–934 | top half / bottom half deferral                                             |
| **§21.3.4.6** IRQLs (Windows)              | 846        | 988      | `PASSIVE_LEVEL` vs. raised IRQL; DPC restrictions                         |

**OSTEP** — thin on this topic; the relevant fragment is Ch. 6, *Mechanism: Limited
Direct Execution*, which frames the OS response to misbehaving programs as
"simply terminate the offender," and describes the register save/restore split
during a trap.

> **On Linux specifics.** The `force_sig_fault` / `SIGFPE` / `TIF_SIGPENDING`
> details in §11 are **not** drawn from the three sources above — none of them
> covers the Linux fault path at that granularity. They are included for contrast
> and should be treated as unverified against a primary source, unlike everything
> in §2–§10, which is traceable to the xv6 chapters cited.

---

## 2. The setup that exists before you run

At boot the kernel fills the interrupt descriptor table — 256 entries, one per
vector. Entry 0 is the divide-error vector. It is installed as an **interrupt gate
at kernel privilege**, which means a user program cannot forge it with a software
interrupt instruction; only the divide unit can raise it.

Separately, every time your process was scheduled onto the CPU, the kernel wrote
the top address of that process's kernel stack into the task state segment. The
hardware will need that address in a moment.

---

## 3. Step 1: The instruction faults

Your division executes, the divisor is zero, and the CPU's divide unit raises the
divide-error exception, **vector 0**.

Two properties of this exception shape everything downstream.

**It is a fault, not a trap.** The saved instruction pointer refers to the
offending division *itself*, not to whatever follows it. If the kernel simply
returned control, the CPU would re-execute the same division and fault again,
forever. So either the machine state must change, or the process must not be
resumed at all.

**No error code is pushed** by hardware for this exception, unlike a page fault.
The kernel's per-vector entry stub pushes a dummy zero in its place so that every
exception, regardless of vector, hands the C code an identically shaped frame.

---

## 4. Step 2: Hardware crosses into kernel mode

This happens in microcode, before a single kernel instruction runs.

1. The processor compares the current privilege level (user) against the gate's
   privilege level (kernel), sees a transition, and performs a **stack switch**:
   it loads the stack pointer and stack segment from the task state segment, then
   pushes the *old* user stack pointer and stack segment onto that fresh kernel
   stack.
2. It pushes the flags register, the code segment, and the instruction pointer.
3. Because the gate is an interrupt gate, it **clears the interrupt-enable flag**.
4. It loads the code segment and instruction pointer from the IDT entry and begins
   executing the kernel's entry stub.

The user stack is never written to. The reason is defensive — xv6 Ch. 3, p. 41:
the kernel "shouldn't use the stack of the user process, because it may not be
valid. The user process may be malicious or contain an error that causes the user
`%esp` to contain an address that is not part of the process's user memory."

---

## 5. Step 3: The entry stub captures register state

The per-vector stub pushes the dummy error code and the trap number, then jumps to
the shared entry path. That common code pushes the remaining segment registers and
then all general-purpose registers.

What now sits on the kernel stack is a **complete snapshot of the machine at the
instant of the fault** — every register, the flags, the faulting address, the user
stack pointer (xv6 Fig. 3-2, p. 42). It is sufficient to resume the process exactly
where it stopped, as though nothing had happened.

The stub loads the kernel's own data segment selectors, passes a pointer to this
snapshot as an argument, and calls the C-level trap handler.

---

## 6. Step 4: The trap handler classifies the event

The handler examines the trap number. It is not a system call. It matches no device
interrupt. It falls through to the default case, whose interpretation is stated in
xv6 Ch. 3, p. 44: the trap "was caused by incorrect behavior (e.g., divide by zero)
as part of the code that was executing before the trap."

Now the decisive question — **who faulted?** The handler inspects the privilege bits
of the saved code segment.

| Saved CPL            | Meaning                      | Action                                                           |
| -------------------- | ---------------------------- | ---------------------------------------------------------------- |
| **0** (kernel) | The kernel divided by zero   | Print diagnostics,**`panic`** — the whole machine stops |
| **3** (user)   | Your program divided by zero | Print diagnostics, set`p->killed = 1`                          |

The kernel case is unrecoverable by construction: there is no one to blame, no
safe way to continue, and no reason to trust any kernel data structure afterwards.

For the user case, the handler prints the process id, process name, trap number,
CPU, and faulting instruction address — then sets the flag.

**Note what it does *not* do.** It does not tear the process down here. It only
records an intent.

---

## 7. Step 5: Why the kill is deferred

This is the design idea worth carrying away from the whole walkthrough.

The trap handler is executing **on the victim process's own kernel stack**. The
process may be holding locks. It may be partway through modifying a shared kernel
structure. Destroying it at this instant would mean freeing the very stack the
destroying code is standing on, and abandoning invariants mid-repair.

The same deferral applies when one process kills another. xv6 Ch. 5, p. 71: "kill
does very little: it just sets the victim's `p->killed` and, if it is sleeping,
wakes it up" — because "the victim might be executing on another CPU or sleeping
while midway through updating kernel data structures."

So death is postponed until a point where the kernel knows it is safe.

---

## 8. Step 6: The check at the kernel exit boundary

Just before the trap handler returns, it tests **two** conditions together:

1. Is this process marked killed?
2. Are we about to return to **user mode**?

Both must hold. The second is what makes the moment safe — if we are returning to
user mode, then by construction no locks are held and no kernel operation is
half-finished. That is the safe point. The handler calls `exit`.

This check also resolves the fault-versus-trap problem from §3: control never
reaches the return-to-user path, so the poisonous division is **never
re-executed**.

---

## 9. Step 7: Exit dismantles the process

`exit` works through the resources in order:

1. **Close every open file descriptor**, dropping each file's reference count.
2. **Release the reference on the current working directory's inode**, inside a
   filesystem transaction.
3. **Take the process table lock and wake the parent**, in case it is blocked
   waiting for a child to die.
4. **Reparent every child to `init`**, waking `init` if any of those children are
   already zombies. This is the mechanism by which orphans get adopted rather than
   leaking — see [[2_init_the_first_user_process]] §3, duty 6.
5. **Set its own state to zombie and call into the scheduler**, surrendering the
   CPU. That call never returns.

---

## 10. Step 8: The parent reaps the corpse

The process is now a **zombie**. It will never execute another instruction, but its
process structure and exit status persist so the parent can learn what happened.

The parent, woken in step 3 above, scans the process table, finds the zombie child,
records its pid, and performs the actual cleanup: freeing the kernel stack, freeing
the page directory and all the user memory it maps, clearing the pid, parent
pointer, name, and killed flag, and finally marking the slot unused for reuse.

**Why the parent and not the child?** Because the child cannot. xv6 Ch. 5, p. 71:
"when the child runs exit, its stack sits in the memory allocated as `p->kstack`
and it uses its own pagetable. They can only be freed after the child process has
finished running for the last time by calling `swtch`."

This is the same reason the scheduler executes on its own dedicated stack rather
than borrowing the stack of whichever thread called into it — someone has to be
standing on solid ground while the dying process's ground is removed. See
[[3_process_teardown]].

If the parent had already exited, `init` adopted this process in step 7.4, and
`init`'s perpetual wait loop performs the reaping.

---

## 11. What real Linux does differently

xv6 has no signals, so it hardcodes the outcome as "kill." Linux inserts a
**signal layer** between §6 and §8.

|                | xv6                              | Linux                                                 |
| -------------- | -------------------------------- | ----------------------------------------------------- |
| Handler        | `trap()` default case          | `do_divide_error()` → `do_error_trap()`          |
| Action         | set`p->killed = 1`             | raise`SIGFPE`, sub-code `FPE_INTDIV`              |
| Recoverable?   | Never                            | **Yes** — the program may install a handler    |
| Delivery point | check in`trap()` before return | `TIF_SIGPENDING` checked on the return-to-user path |
| Default action | terminate                        | terminate**+ core dump**                        |

Silberschatz frames the general rule (§4.6.1, printed p. 188 / PDF p. 245): divide
by 0 is a **synchronous** signal — "delivered to the same process that performed
the operation that caused the signal." That is what distinguishes it from something
like Ctrl-C, which arrives asynchronously from outside.

**The structural point survives the difference.** Linux also defers: raising the
signal only queues it and sets a pending flag; actual delivery happens later, on
the return-to-user-mode path — the *same* safe boundary xv6 uses. Deferring the
consequence to the kernel exit point is a universal kernel pattern, not a
simplification made for teaching. The same instinct shows up in Linux's top
half / bottom half split (Silberschatz §20.5.3) and in Windows deferring work to
DPCs at a lower IRQL (§21.3.4.7).

**A practical consequence of the fault semantics.** If you install a `SIGFPE`
handler and return from it normally, the instruction pointer still refers to the
division, so you fault again immediately and spin forever. A handler that actually
works must either `longjmp` out or reach into the saved machine context
(`ucontext_t`) and advance the instruction pointer past the division.

---

## 12. Fault vs. trap vs. abort

The x86 distinction that §3 turns on, generalized:

| Class           | Saved`%eip` points to            | Resumable?                 | Examples                                         |
| --------------- | ---------------------------------- | -------------------------- | ------------------------------------------------ |
| **Fault** | the**offending** instruction | yes, if the cause is fixed | divide error, page fault, general protection     |
| **Trap**  | the instruction**after**     | yes, naturally             | `int n` (system call), breakpoint, single-step |
| **Abort** | imprecise                          | no                         | double fault, machine check                      |

Page faults and divide errors are both faults, but only page faults are *usefully*
resumable — the handler maps a page and the re-executed instruction succeeds. For a
divide error nothing the kernel can do makes the re-execution succeed, which is
precisely why the outcome is termination rather than retry.

Note the terminology hazard: xv6 uses "trap" loosely for all of these. Ch. 3, p. 40:
"this chapter uses the terms trap and interrupt interchangeably, but it is important
to remember that traps are caused by the current process running on a processor…
and interrupts are caused by devices and may not be related to the currently running
process."

---

## 13. Interview soundbite

> Dividing by zero raises exception vector 0. Hardware does the mode switch —
> stack-switches to the process's kernel stack via the TSS, pushes flags/CS/EIP,
> clears IF, and jumps through the IDT — then the entry stub saves the remaining
> registers into a trap frame. The C handler sees it's not a syscall and not a
> device interrupt, checks the saved CPL, and if the fault came from the kernel it
> panics; from user mode it just **sets a killed flag**. It does not kill the
> process there, because it's running on that process's own kernel stack and may
> hold locks. The kill happens at the return-to-user boundary, the known-safe
> point. Exit then closes files, reparents children to `init`, and becomes a
> zombie; the **parent** does the final free, because you can't free the stack and
> page table you're currently executing on. Linux is the same shape with a signal
> layer inserted — `SIGFPE`, deferred to the same boundary, but overridable. And
> because it's a *fault*, the saved EIP points at the division itself, so returning
> normally would just fault again.

---

## Related notes

- [[2_init_the_first_user_process]] — who adopts the orphans this process leaves
  behind, and why pid 1 can never exit.
- [[1_os_boot_to_first_process]] — the boot chain that builds the IDT and
  TSS referenced in §2.
- [[3_process_teardown]] — the exit/zombie/reap path that §9 and §10 walk
  through, stated on its own.
- [[4_threads_share_stack_memory]] — guard pages and the other way a process dies
  from its own stack.
