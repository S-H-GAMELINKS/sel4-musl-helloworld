#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#ifdef pthread_equal
#undef pthread_equal
#endif

#define MAX_KEYS 16

static void *key_values[MAX_KEYS];
static unsigned next_key;
static char main_stack;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
{
    (void)attr;
    (void)start;
    (void)arg;

    if (thread != 0) {
        *thread = (pthread_t)0;
    }

    return EAGAIN;
}

int pthread_detach(pthread_t thread)
{
    (void)thread;
    return 0;
}

void pthread_exit(void *value)
{
    (void)value;
    for (;;) {
    }
}

int pthread_join(pthread_t thread, void **value)
{
    (void)thread;
    if (value != 0) {
        *value = 0;
    }
    return ESRCH;
}

pthread_t pthread_self(void)
{
    return (pthread_t)1;
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

int pthread_kill(pthread_t thread, int sig)
{
    (void)thread;
    (void)sig;
    return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    (void)set;
    if (oldset != 0) {
        sigemptyset(oldset);
    }
    return 0;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    (void)destructor;

    if (key == 0 || next_key >= MAX_KEYS) {
        return EAGAIN;
    }

    *key = next_key++;
    return 0;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key < MAX_KEYS) {
        key_values[key] = 0;
    }
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    if (key >= MAX_KEYS) {
        return 0;
    }

    return key_values[key];
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= MAX_KEYS) {
        return EINVAL;
    }

    key_values[key] = (void *)value;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)attr;
    if (mutex != 0) {
        memset(mutex, 0, sizeof(*mutex));
    }
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    if (cond != 0) {
        memset(cond, 0, sizeof(*cond));
    }
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    (void)cond;
    (void)mutex;
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *timeout)
{
    (void)cond;
    (void)mutex;
    (void)timeout;
    return ETIMEDOUT;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr)
{
    (void)attr;
    if (lock != 0) {
        memset(lock, 0, sizeof(*lock));
    }
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    if (attr != 0) {
        memset(attr, 0, sizeof(*attr));
    }
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    (void)attr;
    (void)state;
    return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t *attr, int inherit)
{
    (void)attr;
    (void)inherit;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
    (void)attr;
    (void)size;
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t *attr, void **addr, size_t *size)
{
    (void)attr;
    if (addr != 0) {
        *addr = &main_stack;
    }
    if (size != 0) {
        *size = 1024 * 1024;
    }
    return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size)
{
    (void)attr;
    if (size != 0) {
        *size = 0;
    }
    return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr)
{
    (void)thread;
    return pthread_attr_init(attr);
}

int pthread_condattr_init(pthread_condattr_t *attr)
{
    if (attr != 0) {
        memset(attr, 0, sizeof(*attr));
    }
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock)
{
    (void)attr;
    (void)clock;
    return 0;
}

int pthread_setname_np(pthread_t thread, const char *name)
{
    (void)thread;
    (void)name;
    return 0;
}

int pthread_getschedparam(pthread_t thread, int *policy, struct sched_param *param)
{
    (void)thread;
    if (policy != 0) {
        *policy = SCHED_OTHER;
    }
    if (param != 0) {
        memset(param, 0, sizeof(*param));
    }
    return 0;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param)
{
    (void)thread;
    (void)policy;
    (void)param;
    return 0;
}
