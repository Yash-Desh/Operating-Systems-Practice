# Operating-Systems-Practice
Repository to hold all the study material for Operating Systems

## Open Topics to Explore Deeper
1. Thread context switches
2. User Threads vs Kernel Threads
3. Thread scheduling
4. Change of PTBR during process context switches
5. Where is the data returned from a system call stored?
6. Thread pools
7. Interrupt context
8. Process creation from 1st process init to normal process -> e2e
9. Reading from a file (drivers/Io) -> e2e
10. Context switch/system call -> e2e
11. interrupt -> e2e
12. loading a program into memory -> e2e
13. Boot Time procedure -> e2e
14. Accessing memory (pte walk) -> e2e
15. How the intial page table is setup -> e2e
16. Why the kernel is mapped into every process

18. Entry pgdir is a statically created at compile time already baked into the binary.
the kernel does not create a entrypgdir, its already there. 
19. What is reentrancy ? 
20. What do you mean by stack can grow but also hit a stack overflow error ? 
21. Partition table & where is it ? How does the bootloader know ? 
22. So How does the BIOS/BootLoader do a disk read that is clearly an I/O operation requiring
drivers ? 
23. Why 4MB kernel loaded in kernel at boot ? xv6 manual
24. function call & return mechanism -> particularly the stack push pop
25. Page tables are in memory, but they are privileged info so user space can't see it. So they are 
located somewhere in the kernel ? where ? 
But if the page tables are stored in the kernel, each memory access will need us to jump into the kernel ? 
26. Cache Miss -> Performance Implications. 
 


28. Some circular dependency: The page tables of all processes are located in the kernel memory, but the kernel is mapped into the address space of the process. 
So the the page table of each process is mapped within its own address space ? 
29. Who does the page walk ? s/w or h/w
lets say if h/w does the page walk, then to update pagetable entries you need kernel software.
30. Swapping page tables to disk


32. What is kernel virtual memory ? the part of virtual memory where the kernel is mapped



33. Does the kernel maintain its own independent stack & page table ?  
    27. why does the kernel need a page table. Also that means a page table switch ----> Only Kernel Page Table Isolation after Meltdown in OSTEP 
        I don't think it needs one. Its an xv6 only concept.
    17. Scheduler context/Interrupt context. 
When the kernel runs in interrupt / system call -> it uses per-process kernel stack. 

34. what is initramfs ?
35. IPC 
36. Signals 
37. interrupt inside an interrupt.
37.b fault inside kernel ? 


38. Difference between a seg fault & page fault. 
    31. What is a page fault ? page not allocated/lazy allocation/swapping
seg faults are just page faults that cannot be recovered. 

39. Do locks disable interrupts ? 
In user processes we don't, we kernel we do to avoid interrupting a 

40. thread parent exit vs process parent exit before child 

Confirmed: Linux kernel has its own page table & stack that belong
to no other process. 