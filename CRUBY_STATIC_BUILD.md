# CRuby static build experiment for seL4 + CAmkES + musl libc

This document describes an experimental path for embedding CRuby into a CAmkES
component. The first goal is not to run a full Ruby userland. The first goal is
to produce CRuby headers and a static Ruby library that can be linked into the
existing `apps/hello` component.

## Goal

Target milestone:

```text
CAmkES component run()
  -> ruby.h can be included
  -> libruby.a can be linked statically
  -> ruby_init() can be called
  -> a tiny Ruby expression can be evaluated
```

The current `hello.c` entry point is the right shape for this experiment:

```c
#include <ruby.h>

int run(void)
{
    ruby_init();
    rb_eval_string("1 + 1");
    ruby_finalize();
    return 0;
}
```

Avoid file loading, RubyGems, dynamic extensions, threads, and standard library
use in the first milestone.

## Expected difficulty

CRuby assumes a fairly POSIX-like userspace. seL4/CAmkES with musl libc is not a
normal Linux process environment. Even if CRuby compiles, link-time or runtime
failures are expected around functions such as:

```text
open
read
write
stat
mmap
getcwd
getenv
clock_gettime
pthread_*
dlopen
locale functions
signal functions
```

Treat each missing function as a portability decision: disable the CRuby feature,
provide a small stub, or implement a real service in the seL4/CAmkES system.

## Proposed layout

Keep third-party source and build output outside the CAmkES checkout when
possible:

```text
sel4-musl-helloworld/
├── apps/
│   └── hello/
├── vendor/
│   ├── camkes-project/
│   └── ruby/
└── build/
    └── ruby-sel4-musl/
```

`vendor/camkes-project` remains the seL4/CAmkES checkout created by `repo`.
`vendor/ruby` is the CRuby source checkout.

## 1. Fetch CRuby

Use the official release tarball for the latest stable CRuby release. As of
2026-06-15, the official Ruby downloads page lists Ruby 4.0.5 as the current
stable version.

Prefer the tarball over a Git checkout because the tarball already includes the
generated configure support files such as `configure`, `config.guess`, and
`config.sub`.

```sh
mkdir -p vendor
curl -fL https://cache.ruby-lang.org/pub/ruby/4.0/ruby-4.0.5.tar.gz \
  -o vendor/ruby-4.0.5.tar.gz
tar -C vendor -xf vendor/ruby-4.0.5.tar.gz
ln -sfn ruby-4.0.5 vendor/ruby
```

Confirm that the required configure files exist:

```sh
test -x vendor/ruby/configure
test -f vendor/ruby/config.guess
test -f vendor/ruby/config.sub
```

If using a Git checkout instead of the release tarball, CRuby needs its
generated files to be created first. That path is intentionally not the default
for this experiment:

```sh
git clone https://github.com/ruby/ruby.git vendor/ruby
cd vendor/ruby
git checkout v4.0.5
./autogen.sh
```

## 2. Identify the seL4/CAmkES sysroot

After configuring and building the existing CAmkES hello app, the CAmkES build
tree acts as the sysroot used by component builds:

```sh
cd vendor/camkes-project
find . -path '*musllibc*' -name libc.a
find . -name stdio.h | head
```

Expected output from the current workspace:

```text
./musllibc/build-temp/stage/lib/libc.a
./musllibc/build-temp/stage/include/stdio.h
./projects/musllibc/src/include/stdio.h
./projects/musllibc/include/stdio.h
```

The existing component compile command uses:

```text
--sysroot=vendor/camkes-project
```

Use that same sysroot for CRuby experiments.

The staged musl libc install inside the CAmkES build tree is:

```text
vendor/camkes-project/musllibc/build-temp/stage
```

Important paths from that staged install:

```text
vendor/camkes-project/musllibc/build-temp/stage/include
vendor/camkes-project/musllibc/build-temp/stage/lib/libc.a
```

## 3. Start with a minimal static configure

Create a separate CRuby build directory:

```sh
mkdir -p build/ruby-sel4-musl
cd build/ruby-sel4-musl
```

Start with a static, reduced configuration. Use the CAmkES build tree as the
sysroot, and point include/library search paths at the staged musl install:

```sh
REPO_ROOT=$(cd ../.. && pwd)
SYSROOT="$REPO_ROOT/vendor/camkes-project"
MUSL_STAGE="$SYSROOT/musllibc/build-temp/stage"

CC=/usr/bin/gcc \
CPPFLAGS="--sysroot=$SYSROOT -I$MUSL_STAGE/include" \
CFLAGS="--sysroot=$SYSROOT -m64 -D__KERNEL_64__ -march=nehalem" \
LDFLAGS="--sysroot=$SYSROOT -L$MUSL_STAGE/lib -static" \
LIBS="$MUSL_STAGE/lib/libc.a" \
"$REPO_ROOT/vendor/ruby/configure" \
  --host=x86_64-linux-musl \
  --disable-shared \
  --disable-install-doc \
  --with-compress-debug-sections=no \
  --without-gmp \
  --with-static-linked-ext \
  rb_cv_function_name_string=__func__ \
  ac_cv_lib_z_uncompress=no \
  ac_cv_func_mremap=no \
  ac_cv_func_fork=no \
  ac_cv_func_vfork=no \
  ac_cv_func_popen=no \
  ac_cv_func_getpgrp=no \
  ac_cv_func_setpgid=no
```

This is only a starting point. `x86_64-linux-musl` approximates the target C
library, but the target is not a normal Linux environment.

If configure fails while trying to run target binaries, force cross-compilation
mode explicitly:

```sh
REPO_ROOT=$(cd ../.. && pwd)
SYSROOT="$REPO_ROOT/vendor/camkes-project"
MUSL_STAGE="$SYSROOT/musllibc/build-temp/stage"

CC=/usr/bin/gcc \
CPPFLAGS="--sysroot=$SYSROOT -I$MUSL_STAGE/include" \
CFLAGS="--sysroot=$SYSROOT -m64 -D__KERNEL_64__ -march=nehalem" \
LDFLAGS="--sysroot=$SYSROOT -L$MUSL_STAGE/lib -static" \
LIBS="$MUSL_STAGE/lib/libc.a" \
"$REPO_ROOT/vendor/ruby/configure" \
  --host=x86_64-linux-musl \
  --build=x86_64-pc-linux-gnu \
  --disable-shared \
  --disable-install-doc \
  --with-compress-debug-sections=no \
  --without-gmp \
  --with-static-linked-ext \
  rb_cv_function_name_string=__func__ \
  ac_cv_lib_z_uncompress=no \
  ac_cv_func_mremap=no \
  ac_cv_func_fork=no \
  ac_cv_func_vfork=no \
  ac_cv_func_popen=no \
  ac_cv_func_getpgrp=no \
  ac_cv_func_setpgid=no
```

Do not assume these flags are final. The goal is to expose the next concrete
failure.

`rb_cv_function_name_string=__func__` is intentionally set. Without it,
configure may fail to define `RUBY_FUNCTION_NAME_STRING`, and `make miniruby`
can stop with errors like:

```text
error: 'RUBY_FUNCTION_NAME_STRING' undeclared
```

If this happens, remove the CRuby build directory, re-run configure with
`rb_cv_function_name_string=__func__`, and build again.

`ac_cv_lib_z_uncompress=no` and `--with-compress-debug-sections=no` are also
intentional. The current seL4/CAmkES sysroot does not provide `zlib.h` or
`libz.a`. If configure enables zlib anyway, `make miniruby` can stop with:

```text
fatal error: zlib.h: No such file or directory
```

For the first CRuby VM experiment, keep zlib disabled. Adding zlib to the seL4
sysroot is a separate portability task.

`ac_cv_func_mremap=no` is also intentional. CRuby itself can avoid its
`HAVE_MREMAP` path, but musl's allocator may still use `mremap` internally.
The CAmkES static morecore path currently does not implement static `mremap`,
so the application still needs a local compatibility decision for `mremap`.

## 4. Build CRuby static artifacts

Try:

```sh
make -j"$(nproc)" miniruby
make -j"$(nproc)" libruby-static.a
```

Depending on the CRuby version, target names may differ. Check available targets
with:

```sh
make help
```

Useful outputs to look for:

```text
libruby-static.a
ruby.h
include/ruby/
```

## 5. Inspect unresolved symbols

Before linking into CAmkES, inspect what CRuby still expects:

```sh
nm -u libruby-static.a | sort -u > unresolved-ruby-symbols.txt
```

Review the result. Any syscall-like symbol that is not provided by the CAmkES
sysroot will need to be disabled, stubbed, or implemented.

## 6. Link CRuby into the Hello component

Once CRuby headers and `libruby-static.a` exist, update the component
declaration conceptually like this:

```cmake
set(HELLO_REPO_ROOT
    ""
    CACHE PATH "Path to the sel4-musl-helloworld repository root"
)

DeclareCAmkESComponent(
    Hello
    SOURCES
        src/hello.c
        src/ruby_alloc.c
        src/ruby_platform.c
        src/ruby_pthread.c
    INCLUDES
        ${HELLO_REPO_ROOT}/build/ruby-sel4-musl/.ext/include/x86_64-linux-musl
        ${HELLO_REPO_ROOT}/vendor/ruby/include
    LIBS
        ${HELLO_REPO_ROOT}/build/ruby-sel4-musl/libruby-static.a
)
```

The exact include paths depend on the CRuby build output. CRuby usually needs
both generated build headers and source tree headers.

`ruby_platform.c` is the place for seL4/CAmkES-specific libc compatibility
functions such as time and small platform shims. In the current experiment it
provides:

- `clock_gettime`, `clock_getres`, `gettimeofday`, and `time`, because the
  default CAmkES clock syscall path asserts without a real clock provider.
- `prctl`, because CRuby can call Linux-specific `prctl` operations during
  initialization and thread naming.
- `getrandom` and `getentropy`, because CRuby tries `getrandom()` first and
  falls back to `/dev/urandom` if it fails. The current shim is deterministic
  and is only suitable for VM bring-up, not secure randomness.
- `ioctl` and `isatty`, because Ruby stdio/tty probing can otherwise hit
  libsel4muslcsys' unimplemented `sys_ioctl` assertion.
- `mremap` and `__mremap`, returning failure so that musl allocator fallback
  paths can be used instead of calling the unimplemented CAmkES static
  `mremap` syscall.
- fake `eventfd` and fake `epoll`, enough for CRuby's timer/native-thread
  wakeup path during the current single-component bring-up.
- `readlink`, `sigaltstack`, and `mprotect` shims for process/VM behavior that
  does not exist as normal Linux functionality in the current CAmkES component.

`ruby_pthread.c` is the place for single-thread pthread stubs. Do not build
CRuby with `--with-thread=none` for this experiment: Ruby 4.0.5's public/native
thread headers still expect native thread types and the build stops with
`unsupported thread type`. Keep CRuby configured with the pthread model, then
override the small pthread surface needed by the embedded VM.

The first pthread stub should cover at least:

```text
pthread_self
pthread_equal
pthread_key_create
pthread_getspecific
pthread_setspecific
pthread_mutex_*
pthread_cond_*
pthread_rwlock_*
pthread_attr_*
pthread_getattr_np
pthread_sigmask
pthread_kill
pthread_setname_np
pthread_getschedparam
pthread_setschedparam
pthread_create
pthread_join
```

For the first VM milestone, `pthread_create` should not create a real seL4
thread. Returning `EAGAIN` is preferable to silently running the callback on the
same stack, because CRuby's timer thread path expects a separate native thread.

`ruby_alloc.c` is the place for the first Ruby-local allocator. In the current
experiment it provides:

```text
malloc
calloc
realloc
free
posix_memalign
aligned_alloc
malloc_usable_size
mmap
__mmap
munmap
__munmap
```

This allocator is intentionally simple: it uses a fixed-size bump arena and does
reuse blocks returned by `free`. It does not yet coalesce adjacent free blocks.
The reason for overriding `mmap`/`munmap` here is concrete: Ruby GC heap page
allocation can bypass `malloc` and call `mmap` directly. Without local
`mmap`/`munmap` symbols, the component falls into the CAmkES/musl static
morecore path and stops around `heap_page_allocate`.

Keep `munmap` separate from normal `free` at this stage. Ruby GC can call
`munmap` on page-trimming ranges derived from a larger mapping, so treating
every `munmap` address as a `malloc` pointer is unsafe without explicit mapping
metadata.

Then reconfigure CAmkES:

```sh
cd vendor/camkes-project
./init-build.sh \
  -DPLATFORM=x86_64 \
  -DSIMULATION=1 \
  -DCAMKES_APP=hello \
  -DHELLO_REPO_ROOT=../..
ninja -v
```

Use `ninja -v` so that missing include paths, missing libraries, and unresolved
symbols are visible in the full compile/link commands.

## 7. First runtime test

Start with a tiny embedded Ruby expression:

```c
#include <ruby.h>

int run(void)
{
    ruby_init();
    rb_eval_string("1 + 1");
    ruby_finalize();
    return 0;
}
```

Only after this links and boots should stdout be tested:

```c
rb_eval_string("puts 'Hello from CRuby on seL4'");
```

If `puts` fails, keep the VM experiment separate from Ruby IO support. Ruby IO
may require more OS support than expression evaluation.

## 8. Likely follow-up work

Expected next tasks:

- Disable CRuby features that require process control.
- Disable dynamic extension loading.
- Avoid RubyGems and filesystem-backed standard library loading.
- Provide minimal time, environment, and IO behavior if CRuby requires it.
- Keep CRuby's pthread model enabled, but provide single-thread pthread stubs
  until real seL4 threading is intentionally introduced.
- Decide whether to implement `munmap`/dynamic morecore properly or replace the
  allocation strategy used by CRuby/musl.
- Increase CAmkES component heap if Ruby initialization runs out of memory.
- Decide how Ruby scripts should be embedded, for example as C strings or as
  generated object files linked into the component.

## Current checkpoint from this workspace

The current experiment has reached this point:

```text
CRuby 4.0.5 release tarball
configure succeeds
make libruby-static.a succeeds
CAmkES hello links with ruby.h and libruby-static.a
single-thread pthread stubs link into the component
./simulate reaches ruby_init()
Ruby-local allocator/mmap overrides link into the component
Ruby-local getrandom/getentropy overrides avoid /dev/urandom fallback
Ruby-local eventfd/epoll overrides avoid communication pipe failure
rb_eval_string prints from Ruby under ./simulate
```

The pthread stub changes moved the runtime failure forward. Before the stub,
execution reached `before ruby_init` and then stopped inside `ruby_init()`
without useful progress. After the stub, execution reached Ruby GC allocation
and faulted in:

```text
heap_page_allocate
```

The boot log also showed repeated:

```text
sys_munmap is unsupported. This may have been called due to a large malloc'd region being free'd.
```

Adding `ruby_alloc.c` and overriding `mmap`/`munmap` moved execution past that
allocator failure. Adding local `getrandom`/`getentropy` then moved execution
past the `/dev/urandom` fallback:

```text
before ruby_init
libsel4muslcsys: Error attempting syscall 318
sys_open_impl@sys_io.c:178 Open only supports O_RDONLY, not 0x80900 on /dev/urandom
Assertion failed: flags == O_RDONLY
```

Adding local `ioctl`/`isatty` then moved execution past tty probing. Before the
eventfd/epoll shim, the runtime stopped at:

```text
before ruby_init
libsel4muslcsys: Error attempting syscall 290
libsel4muslcsys: Error attempting syscall 293
libsel4muslcsys: Error attempting syscall 22
<main>: [BUG] can not create communication pipe
```

On this target, syscall 290 is `eventfd2`, syscall 293 is `pipe2`, and syscall
22 is `pipe`. CRuby's `thread_pthread.c` uses these for its native-thread
communication pipe setup. So the next concrete issue is no longer the initial
pthread surface, the first GC heap allocation fault, or random source
initialization. It is the CRuby pthread/native-thread wakeup pipe path.

Adding local fake `eventfd` and fake `epoll` support moved execution past that
path. The current successful runtime checkpoint is:

```text
before ruby_init
after ruby_init
after rb_eval_string
Hello from CRuby on seL4
after ruby_finalize
```

Remaining syscall logs still appear during initialization. Treat those as the
next cleanup/classification task rather than as blockers for the first embedded
CRuby expression milestone.

## Success criteria

The first successful checkpoint is:

```text
ninja links hello.instance.bin with CRuby included
./simulate boots
the component reaches ruby_init()
```

The second checkpoint is:

```text
rb_eval_string("1 + 1") returns without crashing
```

The current workspace also reaches:

```text
rb_eval_string("puts 'Hello from CRuby on seL4'")
```

Loading Ruby files and replacing the fake platform shims with real seL4-backed
services are later milestones.
