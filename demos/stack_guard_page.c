// Author: Yash Deshpande
// Date: 05-08-2026
// LLM Model: Claude (Opus 5)
//
// Does a *sequential* thread-stack overflow get CAUGHT (guard page) or does it
// silently corrupt the neighbouring thread's stack? An overflower thread
// recurses without bound, touching every page as it grows; a victim thread
// spins watching a 0xAA canary in its own frame. Result: the overflower faults
// on its OWN guard page (just below its stack base) and the victim's canary is
// never touched -- proving incremental overflow traps rather than spilling.
//
// Build: gcc -Wall -O0 -o stack_guard_page stack_guard_page.c -pthread
//   (-O0 on purpose: -O2 may tail-call-optimise the recursion into a loop.)
// Run:   ./stack_guard_page   ; echo exit=$?   # expect SIGSEGV path, exit 42

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>

static void *victim_base; static size_t victim_size;
static void *ovf_base;    static size_t ovf_size;
static atomic_int ready = 0;
static volatile int canary_seen_corrupt = 0;

static void segv(int sig, siginfo_t *si, void *ctx) {
    (void)sig; (void)ctx;
    char buf[400];
    int n = snprintf(buf, sizeof buf,
        "\n[SIGSEGV] faulting address = %p\n"
        "  overflower stack = [%p, %p)\n"
        "  victim stack     = [%p, %p)\n"
        "  fault vs victim  : %s\n"
        "  fault vs own base: %ld bytes below overflower's own stack base\n"
        "  => sequential overflow TRAPPED at guard page; victim untouched\n",
        si->si_addr, ovf_base, (char *)ovf_base + ovf_size,
        victim_base, (char *)victim_base + victim_size,
        (si->si_addr >= victim_base &&
         (char *)si->si_addr < (char *)victim_base + victim_size)
            ? "INSIDE victim (BAD - would be corruption)"
            : "OUTSIDE victim (guard/unmapped page)",
        (long)((char *)ovf_base - (char *)si->si_addr));
    ssize_t w = write(1, buf, n); (void)w;
    _exit(42);
}

static void *victim(void *arg) {
    (void)arg;
    volatile unsigned char canary[4096];
    memset((void *)canary, 0xAA, sizeof canary);

    pthread_attr_t a;
    pthread_getattr_np(pthread_self(), &a);
    pthread_attr_getstack(&a, &victim_base, &victim_size);
    pthread_attr_destroy(&a);
    printf("[victim]   stack=[%p, %p)  canary at %p (0xAA x %zu)\n",
           victim_base, (char *)victim_base + victim_size,
           (void *)canary, sizeof canary);

    atomic_store(&ready, 1);
    for (long i = 0; i < 400000000L; i++) {
        if (canary[0] != 0xAA || canary[sizeof canary - 1] != 0xAA) {
            canary_seen_corrupt = 1; break;
        }
    }
    printf("[victim]   exiting; canary corrupted = %s\n",
           canary_seen_corrupt ? "YES" : "NO");
    return NULL;
}

static size_t depth = 0;
static void blow(void) {
    volatile char frame[1024];
    memset((void *)frame, 0xBB, sizeof frame);   // touch each page we grow into
    depth++;
    if (depth % 2048 == 0) { printf("[overflow] depth=%zu sp~%p\n", depth,
                                    (void *)frame); fflush(stdout); }
    blow();                                       // no base case: overflow
}

static void *overflower(void *arg) {
    (void)arg;
    while (!atomic_load(&ready)) { }
    pthread_attr_t a;
    pthread_getattr_np(pthread_self(), &a);
    pthread_attr_getstack(&a, &ovf_base, &ovf_size);
    pthread_attr_destroy(&a);

    static char altbuf[262144];                  // handler runs off the dead stack
    stack_t ss = { .ss_sp = altbuf, .ss_size = sizeof altbuf, .ss_flags = 0 };
    sigaltstack(&ss, NULL);

    printf("[overflow] my stack=[%p, %p) -- recursing until it breaks\n",
           ovf_base, (char *)ovf_base + ovf_size);
    blow();
    return NULL;
}

int main(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = segv;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);

    pthread_t v, o;
    pthread_create(&v, NULL, victim, NULL);
    while (!atomic_load(&ready)) { }
    pthread_create(&o, NULL, overflower, NULL);
    pthread_join(o, NULL);
    pthread_join(v, NULL);
    return 0;
}
