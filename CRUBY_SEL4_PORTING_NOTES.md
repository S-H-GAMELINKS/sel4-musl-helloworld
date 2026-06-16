# CRuby on seL4/CAmkES porting notes

This file summarizes the current CRuby-on-seL4 experiment.

## Current status

Confirmed working:

- CRuby 4.0.5 release tarball can be configured for the CAmkES musl sysroot.
- `make libruby-static.a` succeeds.
- The CAmkES `hello` component can include `ruby.h`.
- `libruby-static.a` can be linked into `hello.instance.bin`.
- `./simulate` reaches the component entry point and enters `ruby_init()`.
- `ruby_alloc.c` overrides `malloc`, `calloc`, `realloc`, `free`,
  `posix_memalign`, `aligned_alloc`, `mmap`, and `munmap` for the component.
- The earlier `sys_munmap`/`heap_page_allocate` fault is no longer the first
  runtime stop after the local allocator/mmap override is linked.
- `ruby_platform.c` overrides `getrandom` and `getentropy`, so CRuby no longer
  falls back to opening `/dev/urandom` during initialization.
- `ruby_platform.c` also overrides `ioctl` and `isatty`, so tty probing does not
  hit libsel4muslcsys' unimplemented `sys_ioctl` assertion.
- `ruby_platform.c` provides component-local fake `eventfd` and `epoll`
  support for CRuby's native-thread wakeup path.
- `ruby_platform.c` provides no-op/failing process-environment shims such as
  `mprotect`, `sigaltstack`, and `readlink` where the current CAmkES component
  has no real equivalent yet.
- `./simulate` reaches `ruby_init()`, evaluates a tiny Ruby expression, prints
  from Ruby, and finalizes Ruby.

Current successful runtime checkpoint:

```text
before ruby_init
after ruby_init
after rb_eval_string
Hello from CRuby on seL4
after ruby_finalize
```

The current result is a successful bring-up milestone, not a complete Ruby
userspace. Remaining syscall logs still appear during initialization, so the
next work should classify and replace the remaining broad POSIX shims with
deliberate seL4 services or explicit feature disables.

## Build configuration

Use the Ruby release tarball, not a Git checkout, for this experiment. The
tarball already includes `configure`, `config.guess`, and `config.sub`.

Important configure decisions:

```sh
--host=x86_64-linux-musl
--disable-shared
--disable-install-doc
--with-compress-debug-sections=no
--without-gmp
--with-static-linked-ext
rb_cv_function_name_string=__func__
ac_cv_lib_z_uncompress=no
ac_cv_func_mremap=no
ac_cv_func_fork=no
ac_cv_func_vfork=no
ac_cv_func_popen=no
ac_cv_func_getpgrp=no
ac_cv_func_setpgid=no
```

`--with-thread=none` was tested and should not be used here. CRuby 4.0.5
configures with `with thread: none`, but the build fails because
`thread_native.h` still expects native thread types and reports:

```text
unsupported thread type
```

The better direction is:

```text
build CRuby with pthread model
link single-thread pthread stubs into the CAmkES component
```

## Compatibility files

The CAmkES component currently uses separate compatibility files:

```text
apps/hello/components/Hello/src/ruby_platform.c
apps/hello/components/Hello/src/ruby_pthread.c
apps/hello/components/Hello/src/ruby_alloc.c
```

`ruby_platform.c` handles platform/libc behavior:

- fake monotonic time for `clock_gettime`, `clock_getres`, `gettimeofday`, and
  `time`
- no-op `prctl`
- deterministic placeholder `getrandom`/`getentropy`, used only to move CRuby
  initialization forward until a real seL4 entropy source exists
- `ioctl` and `isatty` returning `ENOTTY`/not-a-tty, so Ruby's stdio/tty probing
  does not call the unimplemented libsel4muslcsys `sys_ioctl`
- fake `eventfd` plus fake `epoll`, enough for CRuby's timer/native-thread
  communication wakeup path in the current single-component experiment
- `readlink` returning `ENOENT`, because `/proc`-style path discovery is not
  available in this environment
- no-op `sigaltstack`
- no-op `mprotect`, because the current Ruby `mmap` path is backed by ordinary
  component arena memory rather than real VM mappings
- failing `mremap`/`__mremap` so musl can use fallback paths instead of the
  unimplemented CAmkES static `mremap` syscall

`ruby_pthread.c` handles single-thread pthread behavior:

- keys/TLS for CRuby's current thread state
- no-op mutexes, condition variables, rwlocks, and attributes
- `pthread_self`/`pthread_equal`
- signal mask and thread naming stubs
- `pthread_create` returns `EAGAIN` rather than creating a real thread

This is deliberately a stub layer. It is not a general pthread implementation.

`ruby_alloc.c` handles Ruby-local allocation:

- fixed-size bump arena
- free list reuse for `free`
- in-place shrink/reuse or copying `realloc`
- aligned allocation for `posix_memalign`/`aligned_alloc`
- `mmap`/`__mmap` backed by the same arena
- no-op `munmap`/`__munmap`

This is also deliberately a first-stage allocator. It avoids the unsupported
CAmkES static morecore `mmap`/`munmap` path, and it reuses blocks returned via
`free`. It does not yet coalesce adjacent free blocks, and `munmap` remains a
separate no-op because Ruby GC can pass page-trimming ranges that are not the
original pointer returned by `mmap`.

## What changed after pthread stubbing

Before pthread stubs:

```text
before ruby_init
```

Then execution stopped inside `ruby_init()`.

After pthread stubs:

```text
before ruby_init
sys_munmap is unsupported ...
FAULT HANDLER ...
```

This means the pthread surface was a real early blocker. Stubbing it moved the
failure forward into Ruby GC/memory management.

## Allocator checkpoint

The allocator issue was identified from the following evidence:

```text
before ruby_init
sys_munmap is unsupported ...
FAULT HANDLER ...
```

`addr2line` mapped the faulting PC to:

```text
heap_page_allocate
```

`nm` also showed that `malloc` was already overridden, but `mmap`/`munmap` were
still coming from the CAmkES/musl side. After adding `mmap`/`munmap` to
`ruby_alloc.c`, `nm` shows those symbols in the component's local text area:

```text
mmap
__mmap
munmap
__munmap
```

After that change, `./simulate` moves past the previous `sys_munmap` failure
and stops at `/dev/urandom`.

## Randomness checkpoint

Before the local random shim, CRuby reached:

```text
before ruby_init
libsel4muslcsys: Error attempting syscall 318
sys_open_impl@sys_io.c:178 Open only supports O_RDONLY, not 0x80900 on /dev/urandom
Assertion failed: flags == O_RDONLY
```

CRuby's `random.c` tries `getrandom()` first. If that fails, it falls back to
`/dev/urandom`. The local `getrandom`/`getentropy` shim prevents that fallback
for the current VM bring-up.

This is not cryptographically secure. It is a deterministic pseudo-random
placeholder and must be replaced before any Ruby code relies on secure random
bytes.

## Communication Pipe Checkpoint

Before the local eventfd/epoll shim, CRuby reached:

```text
before ruby_init
libsel4muslcsys: Error attempting syscall 290
libsel4muslcsys: Error attempting syscall 293
libsel4muslcsys: Error attempting syscall 22
<main>: [BUG] can not create communication pipe
```

The relevant CRuby code is in `thread_pthread.c`: it tries `eventfd`, then
`rb_cloexec_pipe`, which tries `pipe2` and falls back to `pipe`.

The current shim takes the `eventfd` path, so CRuby no longer falls back to
`pipe2`/`pipe`. CRuby then creates an epoll fd for its timer/native-thread
polling path, so the shim also provides a small fake epoll implementation that
can wait on fake eventfds.

This is still not a general fd/event subsystem. It is only enough for the
current CRuby initialization path.

## Next concrete problem

The current milestone has passed:

```text
ruby_init()
rb_eval_string("puts 'Hello from CRuby on seL4'")
ruby_finalize()
```

Useful next directions:

- Classify the remaining syscall logs seen before `after ruby_init`.
- Replace fake `eventfd`/`epoll` with a seL4 notification-backed design if real
  Ruby thread wakeups are needed.
- Replace deterministic `getrandom` with a real entropy source before using
  `Random.urandom` or security-sensitive Ruby code.
- Decide whether to keep CRuby pthread/native-thread support or reconfigure the
  VM toward a more explicitly single-threaded embedded profile.
- Compare with `ruby-on-bare-metal`, which uses a custom allocator, syscall
  layer, and single-thread pthread stubs:
  https://github.com/S-H-GAMELINKS/ruby-on-bare-metal

## Current milestone definition

The next good milestone is:

```text
./simulate
before ruby_init
after ruby_init
```

Only after `ruby_init()` returns should `rb_eval_string()` and Ruby stdout be
treated as the next layer of work.
