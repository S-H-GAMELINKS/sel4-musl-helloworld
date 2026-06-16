#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FAKE_FD_BASE 1000
#define FAKE_EVENTFD_COUNT 4
#define FAKE_EPOLL_BASE 1100
#define FAKE_EPOLL_COUNT 2
#define FAKE_EPOLL_WATCH_COUNT 8

typedef struct fake_eventfd {
    int used;
    int fd;
    int flags;
    uint64_t counter;
} fake_eventfd_t;

typedef struct fake_epoll_watch {
    int used;
    int fd;
    struct epoll_event event;
} fake_epoll_watch_t;

typedef struct fake_epoll {
    int used;
    int fd;
    int flags;
    fake_epoll_watch_t watches[FAKE_EPOLL_WATCH_COUNT];
} fake_epoll_t;

static unsigned long fake_time_ns;
static uint64_t random_state = 0x6a09e667f3bcc909ULL;
static fake_eventfd_t fake_eventfds[FAKE_EVENTFD_COUNT];
static fake_epoll_t fake_epolls[FAKE_EPOLL_COUNT];

static fake_eventfd_t *fake_eventfd_from_fd(int fd)
{
    size_t i;

    for (i = 0; i < FAKE_EVENTFD_COUNT; i++) {
        if (fake_eventfds[i].used && fake_eventfds[i].fd == fd) {
            return &fake_eventfds[i];
        }
    }

    return 0;
}

static fake_epoll_t *fake_epoll_from_fd(int fd)
{
    size_t i;

    for (i = 0; i < FAKE_EPOLL_COUNT; i++) {
        if (fake_epolls[i].used && fake_epolls[i].fd == fd) {
            return &fake_epolls[i];
        }
    }

    return 0;
}

static int fake_fd_is_known(int fd)
{
    return fake_eventfd_from_fd(fd) != 0 || fake_epoll_from_fd(fd) != 0;
}

static void next_time(struct timespec *ts)
{
    fake_time_ns += 1000000;
    ts->tv_sec = fake_time_ns / 1000000000;
    ts->tv_nsec = fake_time_ns % 1000000000;
}

int clock_gettime(clockid_t clk, struct timespec *ts)
{
    (void)clk;

    if (ts == 0) {
        return -1;
    }

    next_time(ts);
    return 0;
}

int clock_getres(clockid_t clk, struct timespec *ts)
{
    (void)clk;

    if (ts != 0) {
        ts->tv_sec = 0;
        ts->tv_nsec = 1000000;
    }

    return 0;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    struct timespec ts;

    (void)tz;

    if (tv == 0) {
        return -1;
    }

    next_time(&ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}

time_t time(time_t *t)
{
    struct timespec ts;

    next_time(&ts);
    if (t != 0) {
        *t = ts.tv_sec;
    }

    return ts.tv_sec;
}

int prctl(int option, ...)
{
    (void)option;
    return 0;
}

int ioctl(int fd, int request, ...)
{
    (void)fd;
    (void)request;

    errno = ENOTTY;
    return -1;
}

int isatty(int fd)
{
    (void)fd;

    errno = ENOTTY;
    return 0;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path;
    (void)buf;
    (void)bufsiz;

    errno = ENOENT;
    return -1;
}

int sigaltstack(const stack_t *ss, stack_t *old_ss)
{
    if (old_ss != 0) {
        old_ss->ss_sp = 0;
        old_ss->ss_flags = SS_DISABLE;
        old_ss->ss_size = 0;
    }

    (void)ss;
    return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

int eventfd(unsigned int initval, int flags)
{
    size_t i;

    for (i = 0; i < FAKE_EVENTFD_COUNT; i++) {
        if (!fake_eventfds[i].used) {
            fake_eventfds[i].used = 1;
            fake_eventfds[i].fd = FAKE_FD_BASE + (int)i;
            fake_eventfds[i].flags = flags;
            fake_eventfds[i].counter = initval;
            return fake_eventfds[i].fd;
        }
    }

    errno = EMFILE;
    return -1;
}

int eventfd_read(int fd, eventfd_t *value)
{
    return read(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value) ? 0 : -1;
}

int eventfd_write(int fd, eventfd_t value)
{
    return write(fd, &value, sizeof(value)) == (ssize_t)sizeof(value) ? 0 : -1;
}

int epoll_create1(int flags)
{
    size_t i;

    for (i = 0; i < FAKE_EPOLL_COUNT; i++) {
        if (!fake_epolls[i].used) {
            fake_epolls[i].used = 1;
            fake_epolls[i].fd = FAKE_EPOLL_BASE + (int)i;
            fake_epolls[i].flags = flags;
            memset(fake_epolls[i].watches, 0, sizeof(fake_epolls[i].watches));
            return fake_epolls[i].fd;
        }
    }

    errno = EMFILE;
    return -1;
}

int epoll_create(int size)
{
    if (size <= 0) {
        errno = EINVAL;
        return -1;
    }

    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    fake_epoll_t *epoll = fake_epoll_from_fd(epfd);
    size_t i;

    if (epoll == 0) {
        return (int)syscall(SYS_epoll_ctl, epfd, op, fd, event);
    }

    switch (op) {
    case EPOLL_CTL_ADD:
        if (event == 0) {
            errno = EINVAL;
            return -1;
        }
        for (i = 0; i < FAKE_EPOLL_WATCH_COUNT; i++) {
            if (epoll->watches[i].used && epoll->watches[i].fd == fd) {
                errno = EEXIST;
                return -1;
            }
        }
        for (i = 0; i < FAKE_EPOLL_WATCH_COUNT; i++) {
            if (!epoll->watches[i].used) {
                epoll->watches[i].used = 1;
                epoll->watches[i].fd = fd;
                epoll->watches[i].event = *event;
                return 0;
            }
        }
        errno = ENOSPC;
        return -1;

    case EPOLL_CTL_MOD:
        if (event == 0) {
            errno = EINVAL;
            return -1;
        }
        for (i = 0; i < FAKE_EPOLL_WATCH_COUNT; i++) {
            if (epoll->watches[i].used && epoll->watches[i].fd == fd) {
                epoll->watches[i].event = *event;
                return 0;
            }
        }
        errno = ENOENT;
        return -1;

    case EPOLL_CTL_DEL:
        for (i = 0; i < FAKE_EPOLL_WATCH_COUNT; i++) {
            if (epoll->watches[i].used && epoll->watches[i].fd == fd) {
                epoll->watches[i].used = 0;
                return 0;
            }
        }
        errno = ENOENT;
        return -1;

    default:
        errno = EINVAL;
        return -1;
    }
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    fake_epoll_t *epoll = fake_epoll_from_fd(epfd);
    int ready = 0;
    size_t i;

    if (epoll == 0) {
        return (int)syscall(SYS_epoll_wait, epfd, events, maxevents, timeout);
    }
    if (events == 0 || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < FAKE_EPOLL_WATCH_COUNT && ready < maxevents; i++) {
        fake_eventfd_t *eventfd;

        if (!epoll->watches[i].used) {
            continue;
        }

        eventfd = fake_eventfd_from_fd(epoll->watches[i].fd);
        if (eventfd != 0 && eventfd->counter > 0 &&
            (epoll->watches[i].event.events & EPOLLIN) != 0) {
            events[ready] = epoll->watches[i].event;
            events[ready].events = EPOLLIN;
            ready++;
        }
    }

    if (ready > 0) {
        return ready;
    }

    (void)timeout;
    return 0;
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
{
    (void)sigmask;
    return epoll_wait(epfd, events, maxevents, timeout);
}

ssize_t read(int fd, void *buf, size_t count)
{
    fake_eventfd_t *event = fake_eventfd_from_fd(fd);

    if (event != 0) {
        uint64_t value;

        if (buf == 0 || count < sizeof(value)) {
            errno = EINVAL;
            return -1;
        }
        if (event->counter == 0) {
            errno = EAGAIN;
            return -1;
        }

        if ((event->flags & EFD_SEMAPHORE) != 0) {
            value = 1;
            event->counter--;
        } else {
            value = event->counter;
            event->counter = 0;
        }

        memcpy(buf, &value, sizeof(value));
        return (ssize_t)sizeof(value);
    }

    return syscall(SYS_read, fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    fake_eventfd_t *event = fake_eventfd_from_fd(fd);

    if (event != 0) {
        uint64_t value;

        if (buf == 0 || count < sizeof(value)) {
            errno = EINVAL;
            return -1;
        }

        memcpy(&value, buf, sizeof(value));
        event->counter += value;
        return (ssize_t)sizeof(value);
    }

    return syscall(SYS_write, fd, buf, count);
}

int close(int fd)
{
    fake_eventfd_t *event = fake_eventfd_from_fd(fd);
    fake_epoll_t *epoll = fake_epoll_from_fd(fd);

    if (event != 0) {
        event->used = 0;
        event->fd = -1;
        event->flags = 0;
        event->counter = 0;
        return 0;
    }
    if (epoll != 0) {
        epoll->used = 0;
        epoll->fd = -1;
        epoll->flags = 0;
        memset(epoll->watches, 0, sizeof(epoll->watches));
        return 0;
    }

    return (int)syscall(SYS_close, fd);
}

int fcntl(int fd, int cmd, ...)
{
    fake_eventfd_t *event = fake_eventfd_from_fd(fd);
    fake_epoll_t *epoll = fake_epoll_from_fd(fd);
    va_list ap;
    long arg = 0;

    if (cmd == F_SETFL || cmd == F_SETFD || cmd == F_DUPFD
#ifdef F_DUPFD_CLOEXEC
        || cmd == F_DUPFD_CLOEXEC
#endif
    ) {
        va_start(ap, cmd);
        arg = va_arg(ap, long);
        va_end(ap);
    }

    if (event != 0 || epoll != 0) {
        int *flags = event != 0 ? &event->flags : &epoll->flags;

        switch (cmd) {
        case F_GETFL:
            return *flags;
        case F_SETFL:
            *flags = (int)arg;
            return 0;
        case F_GETFD:
            return (*flags & O_CLOEXEC) != 0 ? FD_CLOEXEC : 0;
        case F_SETFD:
            if ((arg & FD_CLOEXEC) != 0) {
                *flags |= O_CLOEXEC;
            } else {
                *flags &= ~O_CLOEXEC;
            }
            return 0;
        case F_DUPFD:
#ifdef F_DUPFD_CLOEXEC
        case F_DUPFD_CLOEXEC:
#endif
            errno = EMFILE;
            return -1;
        default:
            errno = EINVAL;
            return -1;
        }
    }

    return (int)syscall(SYS_fcntl, fd, cmd, arg);
}

static uint64_t next_random64(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags)
{
    unsigned char *bytes = (unsigned char *)buf;
    size_t offset = 0;

    (void)flags;

    if (buf == 0 && buflen != 0) {
        errno = EFAULT;
        return -1;
    }

    while (offset < buflen) {
        uint64_t value = next_random64();
        size_t chunk = buflen - offset;
        size_t i;

        if (chunk > sizeof(value)) {
            chunk = sizeof(value);
        }

        for (i = 0; i < chunk; i++) {
            bytes[offset + i] = (unsigned char)(value >> (i * 8));
        }
        offset += chunk;
    }

    return (ssize_t)buflen;
}

int getentropy(void *buf, size_t buflen)
{
    if (buflen > 256) {
        errno = EIO;
        return -1;
    }

    return getrandom(buf, buflen, 0) == (ssize_t)buflen ? 0 : -1;
}

void *__mremap(void *old_addr, size_t old_len, size_t new_len, int flags, ...)
{
    (void)old_addr;
    (void)old_len;
    (void)new_len;
    (void)flags;

    errno = ENOMEM;
    return MAP_FAILED;
}

void *mremap(void *old_addr, size_t old_len, size_t new_len, int flags, ...)
{
    (void)old_addr;
    (void)old_len;
    (void)new_len;
    (void)flags;

    errno = ENOMEM;
    return MAP_FAILED;
}
