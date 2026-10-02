// LD_PRELOAD shim for the Unity Linux Editor.
// Mono checks the remaining stack on every ScriptableObject.CreateInstance / Object.Instantiate through
// pthread_getattr_np. For the main thread glibc answers that by parsing every line of /proc/self/maps,
// which costs milliseconds once the editor has thousands of mappings. The main thread's stack bounds do
// not move, so read them once and answer from the cache afterwards.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

static int (*real_getattr)(pthread_t, pthread_attr_t *);
static void *cached_addr;
static size_t cached_size, cached_guard;
static int cached;

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr) {
    if (!real_getattr) real_getattr = dlsym(RTLD_NEXT, "pthread_getattr_np");
    int is_main = getpid() == (pid_t)syscall(SYS_gettid) && pthread_equal(thread, pthread_self());
    if (is_main && cached) {
        int r = pthread_attr_init(attr);
        if (r) return r;
        pthread_attr_setstack(attr, cached_addr, cached_size);
        pthread_attr_setguardsize(attr, cached_guard);
        return 0;
    }
    int r = real_getattr(thread, attr);
    if (r == 0 && is_main) {
        pthread_attr_getstack(attr, &cached_addr, &cached_size);
        pthread_attr_getguardsize(attr, &cached_guard);
        cached = 1;
    }
    return r;
}
