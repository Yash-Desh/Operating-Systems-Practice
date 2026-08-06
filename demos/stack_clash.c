// Author: Yash Deshpande
// Date: 05-08-2026
// LLM Model: Claude (Opus 5)
//
// Claim: can a single stack allocation larger than the guard page skip over it
// and land in a NEIGHBOUR thread's stack? (The "Stack Clash", CVE-2017-1000364.)
//
// Layout (both stacks placed by hand so the geometry is known exactly):
//   [ victim stack ][ 1 guard page PROT_NONE ][ attacker stack ]
//   low addresses ................................. high addresses
// Stacks grow DOWN. The attacker (higher) does one big alloca that moves its SP
// *past* the guard page into the victim's region, then writes only at the low
// end -- never touching the guard page in between. If the write lands on the
// victim's canary, the guard page was skipped.
//
// Build (shows the vulnerability):
//   gcc -Wall -O1 -fno-stack-clash-protection -o stack_clash stack_clash.c -pthread
//   -> victim canary CORRUPTED (exit 0)
// Build (modern default mitigation): add -fstack-clash-protection instead
//   -> per-page probing hits the guard page: SIGSEGV (exit 139)
// Run: ./stack_clash ; echo exit=$?

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <alloca.h>
#include <stdatomic.h>

#define VS (1024*1024)   // victim stack size
#define AS (1024*1024)   // attacker stack size
#define PS 4096          // one guard page

static char *vstack, *guard, *astack;
static _Atomic(unsigned long *) g_canary = 0;
static atomic_int ready = 0, done = 0, corrupted = 0;

static void *victim(void *arg) {
    (void)arg;
    volatile unsigned long canary = 0xAAAAAAAAAAAAAAAAUL;
    atomic_store(&g_canary, (unsigned long *)&canary);
    printf("[victim]   canary at %p = 0x%lx  (victim stack [%p,%p))\n",
           (void *)&canary, canary, vstack, vstack + VS);
    atomic_store(&ready, 1);
    while (!atomic_load(&done)) {
        if (canary != 0xAAAAAAAAAAAAAAAAUL) { atomic_store(&corrupted, 1); break; }
    }
    printf("[victim]   final canary = 0x%lx\n", canary);
    return NULL;
}

static void *attacker(void *arg) {
    (void)arg;
    while (!atomic_load(&ready)) { }
    unsigned long local;
    uintptr_t sp     = (uintptr_t)&local;
    uintptr_t target = (uintptr_t)atomic_load(&g_canary);

    size_t big = (size_t)(sp - target) + 4096;   // reach past guard into victim
    printf("[attacker] sp=%p  target=%p  alloca(%zu KB) jumps over guard@[%p,%p)\n",
           (void *)sp, (void *)target, big / 1024, guard, guard + PS);

    volatile char *buf = alloca(big);            // moves SP below the guard page
    volatile unsigned long *hit = (volatile unsigned long *)buf;
    for (int i = 0; i < 1024; i++) hit[i] = 0xDEADBEEFDEADBEEFUL;  // low end only

    atomic_store(&done, 1);
    printf("[attacker] wrote 0xDEADBEEF pattern at low end (%p) -- survived\n",
           (void *)buf);
    return NULL;
}

int main(void) {
    char *region = mmap(NULL, VS + PS + AS, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) { perror("mmap"); return 2; }
    vstack = region;
    guard  = region + VS;
    astack = region + VS + PS;
    if (mprotect(guard, PS, PROT_NONE) != 0) { perror("mprotect"); return 2; }
    printf("[main]     victim=[%p,%p)  guard=[%p,%p) PROT_NONE  attacker=[%p,%p)\n",
           vstack, vstack + VS, guard, guard + PS, astack, astack + AS);

    pthread_attr_t va, aa;
    pthread_attr_init(&va); pthread_attr_setstack(&va, vstack, VS);
    pthread_attr_init(&aa); pthread_attr_setstack(&aa, astack, AS);

    pthread_t vt, at;
    pthread_create(&vt, &va, victim, NULL);
    pthread_create(&at, &aa, attacker, NULL);
    pthread_join(at, NULL);
    pthread_join(vt, NULL);

    printf("[main]     RESULT: victim canary %s\n",
           atomic_load(&corrupted)
             ? "CORRUPTED -> clash skipped the guard page into neighbour stack"
             : "intact");
    return atomic_load(&corrupted) ? 0 : 1;
}
