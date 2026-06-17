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
- `ruby_platform.c` provides a small read-only CPIO-backed file surface for
  `open`, `openat`, `access`, `stat`, `lstat`, `fstat`, `fstatat`, `getcwd`,
  `read`, `pread`, `lseek`, `close`, and `fcntl`.
- `hello.camkes` connects `Hello` to `SerialServer` through a `GetChar`
  interface, and `ruby_platform.c` maps `readv(STDIN_FILENO, ...)` to that
  SerialServer input buffer.
- `ruby_platform.c` provides no-op/failing process-environment shims such as
  `mprotect`, `sigaltstack`, and `readlink` where the current CAmkES component
  has no real equivalent yet.
- `./simulate` reaches `ruby_init()`, reads `shell.rb` from a generated CPIO
  archive, evaluates it with CRuby, enters the Ruby shell loop, and stays there.

Current successful runtime checkpoint:

```text
before ruby_init
libsel4muslcsys: Error attempting syscall 107
libsel4muslcsys: Error attempting syscall 108
after ruby_init
Ruby shell loaded from CPIO
commands: help, echo, version
hello from ruby shell
4.0.5
Entering Ruby shell loop
ruby>
help
commands: help, echo, version
ruby>
echo typed from qemu
typed from qemu
ruby>
version
4.0.5
ruby>
```

The current result is a successful file-backed bring-up milestone, not a
complete Ruby userspace. With SerialServer connected, QEMU input reaches
Ruby's `STDIN.gets`. The `107`/`108` syscall logs happen during CRuby
initialization and are separate from the CPIO/stdin path. The next work should
classify and replace the remaining broad POSIX shims with deliberate seL4
services or explicit feature disables.

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
- a read-only CPIO file surface for Ruby scripts embedded into the component
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

## CPIO Script Loading Checkpoint

Ruby script files are currently embedded below:

```text
apps/hello/components/Hello/ruby/
```

The component CMake file generates a `newc` archive from that directory and
converts it to a C array:

```text
apps/hello/components/Hello/src/ruby_archive.c
```

The generated archive is linked into the CAmkES component as `ruby_cpio`.
`ruby_platform.c` exposes that archive through a small read-only file surface.
The current `hello.c` reads `/shell.rb` with `open`, `fstat`, `read`, and
`close`, then passes the loaded source to `rb_eval_string_protect`.

This deliberately proves the CPIO-backed file path before depending on CRuby's
own `load` implementation. A direct Ruby `load '/shell.rb'` reached the VM
without a missing-file exception, but did not execute the expected top-level
script body in this environment. That path should be investigated separately
instead of mixing it with the CPIO bring-up.

CRuby's parser used more than the default 16 KiB CAmkES control stack for this
script path. The working component configuration uses the CAmkES control-thread
stack key:

```camkes
hello._stack_size = 1024 * 1024;
```

Using `hello.stack_size` did not affect the generated control stack. The
generated `hello/camkes.c` should contain:

```text
ROUND_UP_UNSAFE(1048576, PAGE_SIZE_4K)
```

## Serial stdin checkpoint

Interactive stdin is wired through the existing CAmkES SerialServer components.
`Hello` declares:

```camkes
uses GetChar stdin_getchar;
```

The assembly includes `SerialServer` and `TimeServer`, then connects:

```camkes
connection seL4SerialServer stdin_input(from hello.stdin_getchar, to serial.getchar);
```

`ruby_platform.c` implements `readv(STDIN_FILENO, ...)` by blocking on
`stdin_getchar_notification()` and reading bytes from `stdin_getchar_buf`.
Incoming `'\r'` is translated to `'\n'`, because the Ruby shell uses
`STDIN.gets`. The same path also echoes received input to stdout, with Enter
displayed as `"\r\n"` and Backspace/Delete displayed as `"\b \b"`.

Confirmed interactive test:

```text
ruby>
help
commands: help, echo, version
ruby>
echo typed from qemu
typed from qemu
ruby>
version
4.0.5
ruby>
```

## Next concrete problem

The current milestone has passed:

```text
ruby_init()
read /shell.rb from CPIO
rb_eval_string_protect(script)
enter the Ruby shell loop
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

The current milestone is:

```text
./simulate
before ruby_init
libsel4muslcsys: Error attempting syscall 107
libsel4muslcsys: Error attempting syscall 108
after ruby_init
Ruby shell loaded from CPIO
Entering Ruby shell loop
ruby>
help
commands: help, echo, version
ruby>
version
4.0.5
ruby>
```

That means the current path has already passed the original `ruby_init()` smoke
test, the first `rb_eval_string()` test, the CPIO script loading test, and the
SerialServer-backed stdin test.

The next good milestone should be one of:

- classify and replace the remaining syscall `107`/`108` paths;
- replace deterministic randomness with a real entropy source;
- move fake `eventfd`/`epoll` toward seL4 notifications if real Ruby thread
  wakeups are needed;
- investigate CRuby's own `load '/shell.rb'` behavior separately from the
  already-working C-level CPIO read plus `rb_eval_string_protect` path.
