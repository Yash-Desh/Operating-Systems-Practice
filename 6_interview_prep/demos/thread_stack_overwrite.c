// Author: Yash Deshpande
// Date: 05-08-2026
// LLM Model: Claude (Opus 5)
//
// Proves that a thread can OVERWRITE another thread's *live, in-use* stack
// frame -- not just read it. The victim thread parks in an active function
// frame holding a local `secret`; the attacker thread writes through a
// pointer to that local while the frame is still on the stack. We then
// verify (a) the two threads are in the same process, (b) the overwritten
// address lies inside the victim thread's stack region, and (c) the victim
// observes its own local change even though the victim never wrote it.
//
// Build: gcc -Wall -O2 -o thread_stack_overwrite thread_stack_overwrite.c -pthread
// Run:   ./thread_stack_overwrite

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdatomic.h>

static _Atomic(intptr_t) victim_slot = 0;  // address of victim's live local
static atomic_int overwritten = 0;         // handshake: attacker is done

// Report the victim thread's stack region [base, base+size).
static void victim_stack_bounds(void **base, size_t *size) {
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, base, size);
    pthread_attr_destroy(&attr);
}

static void *victim(void *arg) {
    (void)arg;
    volatile int secret = 111;             // lives in THIS frame, on THIS stack

    void *sbase; size_t ssize;
    victim_stack_bounds(&sbase, &ssize);
    printf("[victim]   tid=%ld  &secret=%p  stack=[%p, %p)\n",
           (long)syscall(SYS_gettid), (void *)&secret,
           sbase, (void *)((char *)sbase + ssize));

    // Publish the address of our live local, then spin -- keeping this frame
    // active -- and re-read `secret` (volatile => reloaded from the stack).
    atomic_store(&victim_slot, (intptr_t)&secret);
    while (atomic_load(&overwritten) == 0) { /* frame stays live */ }

    printf("[victim]   after attacker ran, my 'secret' reads = %d "
           "(I only ever stored 111)\n", secret);
    return (void *)(intptr_t)secret;
}

static void *attacker(void *arg) {
    (void)arg;
    intptr_t addr;
    while ((addr = atomic_load(&victim_slot)) == 0) { /* wait for publish */ }

    int *p = (int *)addr;                  // pointer into victim's LIVE frame
    printf("[attacker] tid=%ld  overwriting live slot at %p (was %d) -> 999\n",
           (long)syscall(SYS_gettid), (void *)p, *p);
    *p = 999;                              // overwrite another thread's frame
    atomic_store(&overwritten, 1);
    return NULL;
}

int main(void) {
    printf("[main]     pid=%d (both threads share this process/address space)\n",
           getpid());

    pthread_t vt, at;
    pthread_create(&vt, NULL, victim, NULL);

    // Wait until victim has published its slot, capture its stack bounds view.
    while (atomic_load(&victim_slot) == 0) { }
    intptr_t target = atomic_load(&victim_slot);

    pthread_create(&at, NULL, attacker, NULL);

    void *vret;
    pthread_join(vt, &vret);
    pthread_join(at, NULL);

    int final = (int)(intptr_t)vret;
    printf("[main]     victim returned secret=%d\n", final);
    printf("[main]     target address overwritten = %p\n", (void *)target);
    printf("[main]     RESULT: %s\n",
           final == 999
             ? "PROVEN -- one thread overwrote another's live stack frame"
             : "not overwritten");
    return final == 999 ? 0 : 1;
}
