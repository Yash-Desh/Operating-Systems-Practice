// Author: Yash Deshpande
// Date  : 02-08-2026
// Tutor : Claude Opus 4.8

// Demonstrates that threads of the same process share one address space,
// so one thread can read/write another thread's stack IF given a pointer.
//
// Build: gcc -Wall -o thread_stack_sharing thread_stack_sharing.c -pthread
// Run:   ./thread_stack_sharing

#include <stdio.h>
#include <pthread.h>

// worker receives a pointer into main()'s stack frame and writes through it.
static void *worker(void *arg) {
    int *p = (int *)arg;          // p points at main's local `x`
    printf("[worker] arg address = %p, current value = %d\n", (void *)p, *p);
    *p = 42;                      // writing another thread's stack — legal here
    printf("[worker] wrote 42 into main's stack\n");
    return NULL;
}

int main(void) {
    int x = 0;                    // lives on main's stack

    printf("[main]   &x = %p, value before = %d\n", (void *)&x, x);

    pthread_t t;
    pthread_create(&t, NULL, worker, &x);   // hand worker a pointer to x
    pthread_join(t, NULL);

    printf("[main]   value after  = %d\n", x);
    printf("[main]   -> worker modified main's stack variable: %s\n",
           x == 42 ? "YES (shared address space)" : "no");
    return 0;
}
