# x86 int instruction

- **Author:** Yash Deshpande
- **Date:** 06-08-2026
- **LLM Model:** Claude (Opus 5)

The int instruction, step by step

  This is xv6 manual Ch. 3, p. 39 (PDF p. 39 — 1:1). Every one of these steps is microcode — the CPU does all of it before your handler's first instruction runs.

  The running example: user code at CPL 3 executes int 64 to make a system call.

---

## Step 1 — Fetch the n'th descriptor from the IDT

> "Fetch the n'th descriptor from the IDT, where n is the argument of `int`."

The IDT is a 256-entry table in memory; the CPU finds it via the `IDTR` register, loaded once at boot by `lidt`. The operand `n` is a plain array index — `int 64` fetches entry 64.

Each entry (a *gate descriptor*, 8 bytes) carries three things that matter here:

| Field | Used in step |
|---|---|
| **DPL** — privilege required to invoke via `int` | 2 |
| **Selector** — the `%cs` the handler runs with | 3, 10 |
| **Offset** — the `%eip` the handler starts at | 10 |

Nothing has changed yet. This is a read.

---

## Step 2 — Check `CPL <= DPL`

> "Check that CPL in `%cs` is <= DPL, where DPL is the privilege level in the descriptor."

**This is the security gate**, and it's the only step that can abort the whole sequence.

CPL is the low 2 bits of the current `%cs`. Lower number = more privileged, so `CPL <= DPL` reads as "you are at least as privileged as this gate demands."

For our `int 64`: CPL is 3, and xv6 deliberately set vector 64's DPL to 3. `3 <= 3` passes.

Now try `int 32` (the timer vector) from user code. Its DPL is 0. `3 <= 0` **fails** — and p. 40 tells you what happens:

> "the check `CPL <= DPL` allows the kernel to forbid `int` calls to inappropriate IDT entries such as device interrupt routines. For a user program to execute `int`, the IDT entry's DPL must be 3. If the user program doesn't have the appropriate privilege, then `int` will result in `int 13`, which is a general protection fault."

So the failure is itself a trap — vector 13 — and the process gets killed.

**Vector 64 is the only doorway user code may open.** Every other IDT entry is DPL 0.

> **Important:** a *hardware* interrupt skips this step entirely. There is no CPL to compare, because no code requested the transition. This check constrains software only.

  ---
  Step 3 — Conditionally save the old %esp and %ss

  ▎ "Save %esp and %ss in CPU-internal registers, but only if the target segment selector's PL < CPL."

  This is where the "is a stack switch happening?" decision is made, and steps 4–6 all hang off it.

  The condition compares the handler's privilege (from the descriptor's selector) against the current privilege:

  ┌─────────────────┬───────────┬───────────────┐
  │      Case       │ Condition │ Stack switch? │
  ├─────────────────┼───────────┼───────────────┤
  │ User → kernel   │ 0 < 3 ✓   │ Yes           │
  ├─────────────────┼───────────┼───────────────┤
  │ Kernel → kernel │ 0 < 0 ✗   │ No            │
  └─────────────────┴───────────┴───────────────┘

  Our case is user→kernel, so the CPU squirrels away the user's %ss:%esp into invisible internal registers — not memory, because there is no safe stack to write them to yet. That's the entire reason this step
  exists: the values must survive step 4 clobbering them, and they need somewhere to live for one moment.

  If no privilege change, this step and steps 4–6 are all skipped. p. 40:

  ▎ "If the int instruction didn't require a privilege-level change, the x86 won't save %ss and %esp."

  ---
  Step 4 — Load %ss and %esp from the task segment

  ▎ "Load %ss and %esp from a task segment descriptor."

  The stack switch itself. The CPU reads ss0 and esp0 out of the TSS and installs them. From this instant the CPU is on the kernel stack.

  esp0 holds the top of the current process's p->kstack, because switchuvm rewrote it during the last context switch (p. 41):

  ▎ "switchuvm stores the address of the top of the kernel stack of the user process into the task segment descriptor."

  Why this can't be software's job: the handler's first act would be a push, and pushing needs a valid stack. You can't run code to find the stack when running code requires the stack. So the kernel deposits
  the answer in the TSS ahead of time and the hardware picks it up.

  And why it can't just keep the user's stack, p. 40:

  ▎ "the int instruction cannot use the user stack to save values, because the process may not have a valid stack pointer; instead, the hardware uses the stack specified in the task segment, which is set by
  ▎ the kernel."

  If it did, a hostile process would point %esp at kernel memory and turn every system call into an arbitrary kernel write.

  ---
  Steps 5–9 — Push five values

  Now that a trusted stack is in place, the CPU records the old state. All five pushes go to the kernel stack.

  ┌─────┬─────────┬──────────────────────────────────────┐
  │  #  │  Push   │               Purpose                │
  ├─────┼─────────┼──────────────────────────────────────┤
  │ 5   │ %ss     │ user stack segment — conditional     │
  ├─────┼─────────┼──────────────────────────────────────┤
  │ 6   │ %esp    │ user stack pointer — conditional     │
  ├─────┼─────────┼──────────────────────────────────────┤
  │ 7   │ %eflags │ flags, including the pre-clear IF    │
  ├─────┼─────────┼──────────────────────────────────────┤
  │ 8   │ %cs     │ where you came from, and at what CPL │
  ├─────┼─────────┼──────────────────────────────────────┤
  │ 9   │ %eip    │ the instruction to resume at         │
  └─────┴─────────┴──────────────────────────────────────┘

  Steps 5–6 are the conditional pair. They push the values stashed in step 3. If no privilege change occurred, they don't happen — which is exactly why Figures 3-1 and 3-2 bracket %ss/%esp as "only present on
  privilege change." The trap frame has two sizes.

  Step 7 must precede step 10. %eflags is pushed before IF is cleared, so the saved copy holds the original state — and iret popping it is what silently restores the prior interrupt-enable setting. Nobody
  explicitly re-enables interrupts on the way out.

  Step 8 is what xv6 later interrogates. The saved %cs carries the old CPL in its low 2 bits. That's the (tf->cs & 3) test — did the kernel or the user cause this? — deciding panic versus killing one process.

  Step 9's subtlety: what gets pushed depends on the trigger.

  ┌──────────────────────────────────┬──────────────────────────────────┐
  │             Trigger              │       Saved %eip points to       │
  ├──────────────────────────────────┼──────────────────────────────────┤
  │ int n / interrupt                │ the instruction after            │
  ├──────────────────────────────────┼──────────────────────────────────┤
  │ Fault (divide error, page fault) │ the offending instruction itself │
  └──────────────────────────────────┴──────────────────────────────────┘

  p. 43: "the saved %eip is the address of the instruction right after the int instruction." Convenient for syscalls. But for a divide by zero the saved %eip aims at the division — so iret would re-execute it
  and fault forever. That's why the process is killed instead of resumed.

  Note the ordering: pushed 5→9 on a downward-growing stack means %ss sits at the highest address and %eip at the lowest. That's Figure 3-1 read bottom-to-top.

  ---
  Step 10 — Clear IF, but only on an interrupt

  ▎ "Clear the IF bit in %eflags, but only on an interrupt."

  "Interrupt" here means interrupt gate, not "hardware interrupt." The IDT holds two gate types that behave identically except for this one bit:

  ┌────────────────┬────────────┬──────────────────────────┐
  │   Gate type    │ Clears IF? │     xv6 uses it for      │
  ├────────────────┼────────────┼──────────────────────────┤
  │ Trap gate      │ no         │ vector 64 (system calls) │
  ├────────────────┼────────────┼──────────────────────────┤
  │ Interrupt gate │ yes        │ vectors 32–63 (devices)  │
  └────────────────┴────────────┴──────────────────────────┘

  p. 42–43 on the syscall gate:

  ▎ "the gate is of type 'trap' by passing a value of 1 as second argument. Trap gates don't clear the IF flag, allowing other interrupts during the system call handler."

  And p. 46 on device vectors:

  ▎ "The only difference between vector 32 and vector 64 (the one for system calls) is that vector 32 is an interrupt gate instead of a trap gate. Interrupt gates clear IF, so that the interrupted processor
  ▎ doesn't receive interrupts while it is handling the current interrupt."

  The reasoning is asymmetric on purpose. A syscall can be long and may block, so leaving interrupts on keeps the machine responsive. A device handler should be short and must not be re-entered by its own
  line.

  Our int 64 goes through a trap gate, so IF stays set. Interrupts remain enabled inside the system call.

  ---
  Step 11 — Set %cs and %eip from the descriptor

  ▎ "Set %cs and %eip to the values in the descriptor."

  The jump. Both halves must move together — a code segment without a matching offset names nothing.

  Loading the descriptor's selector (a CPL-0 kernel selector) is the privilege escalation. There is no separate "switch to kernel mode" step in this list; installing a new %cs is that step.

  p. 40:

  ▎ "After both cases, %eip is pointing to the address specified in the descriptor table, and the instruction at that address is the next instruction to be executed and the first instruction of the handler for
  ▎ int n."

  Microcode is done. The next fetched instruction is vector64's first, running in ring 0 on the process's kernel stack.

  ---
  The shape of it

  Eleven steps, four jobs:

  ┌───────┬────────────────────────────────────────────────────────────────┐
  │ Steps │                              Job                               │
  ├───────┼────────────────────────────────────────────────────────────────┤
  │ 1–2   │ Look up and authorize — find the gate, check permission        │
  ├───────┼────────────────────────────────────────────────────────────────┤
  │ 3–4   │ Get a trusted stack — stash the old, install kernel's from TSS │
  ├───────┼────────────────────────────────────────────────────────────────┤
  │ 5–9   │ Save resumable state — five pushes onto that stack             │
  ├───────┼────────────────────────────────────────────────────────────────┤
  │ 10–11 │ Enter the handler — maybe mask interrupts, then jump           │
  └───────┴────────────────────────────────────────────────────────────────┘

  Two structures feed it, cleanly divided:

  - IDT → where to go (%cs:%eip) and who may go there (DPL)
  - TSS → what stack to stand on (%ss:%esp)

  What still isn't saved

  Notice what's absent: %eax, %ebx, %ecx, %edx, %esi, %edi, %ebp, %ds, %es, %fs, %gs. Hardware saves the bare minimum needed to return, nothing more.

  Software finishes the job. p. 42:

  ▎ "Alltraps continues to save processor registers: it pushes %ds, %es, %fs, %gs, and the general-purpose registers."

  Hardware's five pushes plus a trap number, error code, and alltraps's pushes together compose the full struct trapframe of Figure 3-2.

  iret runs it backwards

  p. 40: "An operating system can use the iret instruction to return from an int instruction. It pops the saved values during the int instruction from the stack, and resumes execution at the saved %eip."

  Pops %eip, %cs, %eflags — and then inspects the just-restored %cs. If its CPL is 3, it also pops %ss/%esp; if CPL is 0, it doesn't, because they were never pushed. The instruction infers the frame's size
  from the same condition that produced it.

  That's also the mode switch back: restoring a CPL-3 %cs drops privilege, and restoring %eflags quietly re-enables interrupts if they were on before.