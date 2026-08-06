// Author: Yash Deshpande
// Date: 05-08-2026
// LLM Model: Claude (Opus 5)
//
// Is the MAIN thread's stack different from a pthread stack, and how does it
// behave on overflow?
//   (a) Show the kernel's view: the main stack is a single [stack] VMA that
//       starts small and grows on demand up to RLIMIT_STACK; a pthread stack is
//       a fixed anonymous mmap allocated in full up front.
//   (b) Overflow the main stack by recursion and prove it TRAPS at the guard
//       gap (SIGSEGV just below [stack] / at RLIMIT_STACK) rather than silently
//       flowing into the pthread stack.
//
// Build: gcc -Wall -O0 -o main_stack_overflow main_stack_overflow.c -pthread
//   (-O0 on purpose: -O2 may tail-call-optimise the recursion into a loop.)
// Run:   ./main_stack_overflow ; echo exit=$?     # expect SIGSEGV path, exit 42

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <stdatomic.h>

static uintptr_t stack_lo, stack_hi;   // [stack] VMA bounds from /proc
static void *pth_base; static size_t pth_size;
static atomic_int pth_ready = 0;

static void read_main_stack_bounds(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "[stack]")) {
            sscanf(line, "%lx-%lx", &stack_lo, &stack_hi);
            printf("[maps]   main stack  [stack] = [%#lx, %#lx)  size=%lu KB\n",
                   stack_lo, stack_hi, (stack_hi - stack_lo) / 1024);
        }
    }
    fclose(f);
}

static void segv(int sig, siginfo_t *si, void *ctx) {
    (void)sig; (void)ctx;
    char b[400];
    uintptr_t f = (uintptr_t)si->si_addr;
    int in_pth = pth_base && f >= (uintptr_t)pth_base &&
                 f < (uintptr_t)pth_base + pth_size;
    int n = snprintf(b, sizeof b,
        "\n[SIGSEGV] fault addr = %#lx\n"
        "  [stack] VMA   = [%#lx, %#lx)\n"
        "  pthread stack = [%p, %p)\n"
        "  fault is %s [stack] base (guard gap / RLIMIT_STACK)\n"
        "  fault inside pthread stack? %s\n"
        "  => main-stack overflow TRAPPED; pthread stack NOT reached\n",
        f, stack_lo, stack_hi, pth_base, (char *)pth_base + pth_size,
        (f < stack_lo ? "BELOW" : "not below"),
        in_pth ? "YES (corruption!)" : "no");
    ssize_t w = write(1, b, n); (void)w;
    _exit(42);
}

static void *pth(void *a) {   // just to have a pthread stack mapped for contrast
    (void)a;
    pthread_attr_t at;
    pthread_getattr_np(pthread_self(), &at);
    pthread_attr_getstack(&at, &pth_base, &pth_size);
    pthread_attr_destroy(&at);
    atomic_store(&pth_ready, 1);
    while (1) pause();
    return NULL;
}

static size_t depth = 0;
static void recurse(void) {
    volatile char frame[4096];
    memset((void *)frame, 0xEE, sizeof frame);   // touch each page as we grow
    if (++depth % 4096 == 0) { printf("[main]   depth=%zu sp~%p\n", depth,
                                      (void *)frame); fflush(stdout); }
    recurse();
}

int main(void) {
    read_main_stack_bounds();

    pthread_t t;
    pthread_create(&t, NULL, pth, NULL);
    while (!atomic_load(&pth_ready)) { }
    printf("[main]   pthread stack = [%p, %p) for comparison\n",
           pth_base, (char *)pth_base + pth_size);

    static char alt[262144];
    stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = segv; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);

    printf("[main]   overflowing main stack by recursion...\n");
    recurse();
    return 0;
}
