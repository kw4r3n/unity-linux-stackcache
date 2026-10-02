// Measures pthread_getattr_np on the main thread with ~6,000 memory mappings, the size of a long-running
// Unity Editor, and checks the shim returns the same stack bounds as glibc.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>

static void get_stack(void **addr, size_t *size) {
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, addr, size);
    pthread_attr_destroy(&attr);
}

int main(void) {
    // Alternate protections so the kernel cannot merge the mappings into one
    for (int i = 0; i < 6000; i++)
        mmap(NULL, 4096, i % 2 ? PROT_READ : PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    void *first_addr;
    size_t first_size;
    get_stack(&first_addr, &first_size);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 300; i++) {
        void *addr;
        size_t size;
        get_stack(&addr, &size);
        if (addr != first_addr || size != first_size) {
            puts("FAIL: stack bounds changed between calls");
            return 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  300 calls: %.1f ms (stack %p, %zu bytes)\n",
           (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6, first_addr, first_size);
    return 0;
}
