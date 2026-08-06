// Author: Yash Deshpande
// Date: 05-08-2026
// LLM Model: Claude (Opus 5)
//
// Recon: where do thread stacks sit relative to each other, and is there a
// gap between them? Each pthread stack is a separate mmap region fronted by a
// guard page; this prints the regions and the gap (== guardsize) between them.
//
// Build: gcc -Wall -O2 -o thread_stack_layout thread_stack_layout.c -pthread
// Run:   ./thread_stack_layout

#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define N 4
static struct { void *base; size_t size; size_t guard; } info[N];
static pthread_barrier_t bar;

static void *probe(void *arg) {
    long i = (long)arg;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &info[i].base, &info[i].size);
    pthread_attr_getguardsize(&attr, &info[i].guard);
    pthread_attr_destroy(&attr);
    pthread_barrier_wait(&bar);
    return NULL;
}

static int cmp(const void *a, const void *b) {
    const char *x = *(const char **)a, *y = *(const char **)b;
    return (x > y) - (x < y);
}

int main(void) {
    pthread_t t[N];
    pthread_barrier_init(&bar, NULL, N + 1);
    for (long i = 0; i < N; i++) pthread_create(&t[i], NULL, probe, (void *)i);
    pthread_barrier_wait(&bar);
    for (int i = 0; i < N; i++) pthread_join(t[i], NULL);

    printf("thread stacks (base = LOW address, stack grows DOWN toward base):\n");
    for (int i = 0; i < N; i++)
        printf("  t%d: [%p, %p)  size=%zu KB  guardsize=%zu B\n", i,
               info[i].base, (char *)info[i].base + info[i].size,
               info[i].size / 1024, info[i].guard);

    void *bases[N];
    for (int i = 0; i < N; i++) bases[i] = info[i].base;
    qsort(bases, N, sizeof(void *), cmp);
    printf("\ngaps between adjacent stack regions (sorted by address):\n");
    for (int i = 0; i + 1 < N; i++) {
        size_t sz = 0;
        for (int j = 0; j < N; j++) if (info[j].base == bases[i]) sz = info[j].size;
        long gap = (char *)bases[i + 1] - ((char *)bases[i] + sz);
        printf("  %p end=%p -> next base %p : gap = %ld bytes\n",
               bases[i], (char *)bases[i] + sz, bases[i + 1], gap);
    }
    return 0;
}
