# seL4 + CAmkES + musl libc + CRuby shell

This repository keeps a local CAmkES application and the CAmkES checkout in one
workspace. The current application embeds CRuby into a CAmkES component, loads a
Ruby script from an embedded CPIO archive, and exposes a small interactive Ruby
shell over QEMU serial input.

Current runtime path:

```text
QEMU
  -> seL4
  -> CAmkES rootserver
  -> Hello component
  -> CRuby 4.0.5 static library
  -> CPIO-backed /shell.rb
  -> SerialServer-backed STDIN
```

Expected interactive output:

```text
Ruby shell loaded from CPIO
commands: help, echo, version
hello from ruby shell
4.0.5
Entering Ruby shell loop
ruby>
help
commands: help, echo, version
ruby>
version
4.0.5
ruby>
```

## Documentation

- [CRUBY_STATIC_BUILD.md](CRUBY_STATIC_BUILD.md): CRuby static build procedure.
- [CRUBY_SEL4_PORTING_NOTES.md](CRUBY_SEL4_PORTING_NOTES.md): current porting
  state and compatibility surfaces.
- [CRUBY_BRINGUP_HISTORY.md](CRUBY_BRINGUP_HISTORY.md): implementation history,
  problems encountered, and why the current decisions were made.

## Directory Layout

Recommended layout:

```text
sel4-musl-helloworld/
├── apps/
│   └── hello/
│       ├── CMakeLists.txt
│       ├── hello.camkes
│       └── components/
│           └── Hello/
│               ├── CMakeLists.txt
│               ├── ruby/
│               │   └── shell.rb
│               └── src/
│                   ├── hello.c
│                   ├── ruby_alloc.c
│                   ├── ruby_platform.c
│                   ├── ruby_pthread.c
│                   └── ruby_archive.c
├── build/
│   └── ruby-sel4-musl/
├── vendor/
│   ├── camkes-project/
│   └── ruby -> ruby-4.0.5
└── README.md
```

`apps/hello` is the application code owned by this repository.

`vendor/camkes-project` is the CAmkES/seL4 checkout created by `repo`. It should
be treated as generated/vendor content and excluded from normal Git tracking.

`vendor/ruby` points to the CRuby 4.0.5 release source tree.

## Setup

Install host dependencies as needed. On Ubuntu/WSL, the following packages are
typical for this path:

```sh
sudo apt update
sudo apt install repo ninja-build cmake device-tree-compiler qemu-system-x86 haskell-stack python3-pip xxd cpio
```

Python dependencies that commonly appear during CAmkES builds:

```sh
python3 -m pip install --user plyplus lxml pyfdt
```

Fetch CAmkES under `vendor/`:

```sh
mkdir -p vendor/camkes-project
cd vendor/camkes-project
repo init -u https://github.com/seL4/camkes-manifest.git
repo sync
```

Expose this repository's app to CAmkES using a symlink:

```sh
ln -sfn ../../../../../apps/hello \
  vendor/camkes-project/projects/camkes/apps/hello
```

Confirm that CAmkES can see the app:

```sh
ls -l vendor/camkes-project/projects/camkes/apps/hello
```

## Build CRuby

Build CRuby before configuring the CAmkES app. The current project expects:

```text
build/ruby-sel4-musl/libruby-static.a
vendor/ruby/include
build/ruby-sel4-musl/.ext/include/x86_64-linux-musl
```

See [CRUBY_STATIC_BUILD.md](CRUBY_STATIC_BUILD.md) for the full configure and
build commands.

## Configure CAmkES

Configure the CAmkES checkout for the `hello` app. This uses the same
`vendor/camkes-project` tree that `repo sync` created.

```sh
cd vendor/camkes-project
./init-build.sh \
  -DPLATFORM=x86_64 \
  -DSIMULATION=1 \
  -DCAMKES_APP=hello \
  -DHELLO_REPO_ROOT=../..
```

`HELLO_REPO_ROOT=../..` is used by the component CMake file to find the local
CRuby build and source tree.

If the tree was already configured for another app, re-running this command
updates `CMakeCache.txt` to use `CAMKES_APP=hello`.

## Build

```sh
cd vendor/camkes-project
ninja -v
```

The build generates a CPIO archive from:

```text
apps/hello/components/Hello/ruby/
```

and converts it into:

```text
apps/hello/components/Hello/src/ruby_archive.c
```

`shell.rb` changes should trigger CPIO regeneration and relinking.

## Run

```sh
cd vendor/camkes-project
./simulate
```

At the prompt, type commands and press Enter:

```text
ruby>
help
commands: help, echo, version
ruby>
echo hello
hello
ruby>
version
4.0.5
ruby>
```

For QEMU started with `-nographic`, exit with:

```text
Ctrl-A X
```

## Current Implementation Notes

- CRuby is statically linked as `libruby-static.a`.
- Ruby source files are embedded through a generated `newc` CPIO archive.
- `hello.c` reads `/shell.rb` through the CPIO-backed file shim and evaluates it
  with `rb_eval_string_protect`.
- `hello.camkes` includes `SerialServer` and `TimeServer`, and connects
  `Hello.stdin_getchar` to `serial.getchar`.
- `ruby_platform.c` maps `readv(STDIN_FILENO, ...)` to the SerialServer input
  ring buffer.
- Input echo is implemented in the stdin shim. This is a simple echo path, not
  a full TTY line discipline.
- `hello._stack_size = 1024 * 1024` is required for the current CRuby parser
  path. `hello.stack_size` does not affect the generated control thread stack.

## Notes

- CAmkES builds one app at a time via `-DCAMKES_APP=<name>` at configure time.
- The app name must match a directory under `projects/camkes/apps/`.
- In this layout, `projects/camkes/apps/hello` is a symlink to `apps/hello`.
- `SIMULATION` must be enabled during configure, otherwise the `simulate`
  script may not be generated.
- `repo sync` may update files under `vendor/camkes-project`. If the symlink is
  ever removed, recreate it before configuring.
- This README uses `init-build.sh` instead of `griddle`; the current working
  flow is the CMake/Ninja build under `vendor/camkes-project`.
- Remaining syscall logs during `ruby_init()` are documented in
  [CRUBY_SEL4_PORTING_NOTES.md](CRUBY_SEL4_PORTING_NOTES.md).
