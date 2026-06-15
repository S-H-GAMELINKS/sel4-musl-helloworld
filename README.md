# seL4 + CAmkES + musl libc Hello World

This repository is intended to keep the local Hello World application and the
CAmkES checkout in one workspace.

The goal is:

```text
QEMU
  -> seL4
  -> CAmkES rootserver
  -> CAmkES component
  -> musl libc printf
  -> Hello World on the serial console
```

## Directory Layout

Recommended layout:

```text
sel4-musl-helloworld/
├── README.md
├── .gitignore
├── apps/
│   └── hello/
│       ├── CMakeLists.txt
│       ├── hello.camkes
│       └── components/
│           └── Hello/
│               ├── CMakeLists.txt
│               └── src/
│                   └── hello.c
└── vendor/
    └── camkes-project/
        ├── .repo/
        ├── projects/
        ├── kernel/
        ├── tools/
        ├── init-build.sh
        └── griddle
```

`apps/hello` is the application code owned by this repository.

`vendor/camkes-project` is the CAmkES/seL4 checkout created by `repo`. It should
be treated as generated/vendor content and excluded from normal Git tracking.

## .gitignore

Use at least:

```gitignore
/vendor/camkes-project/
/vendor/*/build*/
/build/
/build-*/
/CMakeFiles/
/CMakeCache.txt
```

## Application Files

### apps/hello/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16.0)

project(hello C)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/components/Hello)

DeclareCAmkESRootserver(hello.camkes)
add_simulate_test([=[wait_for "Hello World"]=])
```

### apps/hello/hello.camkes

```camkes
component Hello {
    control;
}

assembly {
    composition {
        component Hello hello;
    }
}
```

### apps/hello/components/Hello/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16.0)

project(ComponentHello C)

DeclareCAmkESComponent(Hello SOURCES src/hello.c)
```

### apps/hello/components/Hello/src/hello.c

```c
#include <stdio.h>

int run(void)
{
    printf("Hello World from seL4 + CAmkES + musl libc\n");
    return 0;
}
```

## Setup

Install host dependencies as needed. On Ubuntu/WSL, the following packages are
typical for this path:

```sh
sudo apt update
sudo apt install repo ninja-build cmake device-tree-compiler qemu-system-x86 haskell-stack python3-pip
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
cd /home/sh/oss/sel4-musl-helloworld
ln -sfn /home/sh/oss/sel4-musl-helloworld/apps/hello \
  /home/sh/oss/sel4-musl-helloworld/vendor/camkes-project/projects/camkes/apps/hello
```

Confirm that CAmkES can see the app:

```sh
ls -l /home/sh/oss/sel4-musl-helloworld/vendor/camkes-project/projects/camkes/apps/hello
```

## Configure

Configure the CAmkES checkout for the `hello` app. This uses the same
`vendor/camkes-project` tree that `repo sync` created.

For x86_64 QEMU:

```sh
cd /home/sh/oss/sel4-musl-helloworld/vendor/camkes-project
./init-build.sh -DPLATFORM=x86_64 -DSIMULATION=1 -DCAMKES_APP=hello
```

If the tree was already configured for another app, re-running this command
updates `CMakeCache.txt` to use `CAMKES_APP=hello`.

## Build

```sh
cd /home/sh/oss/sel4-musl-helloworld/vendor/camkes-project
ninja
```

## Run

```sh
cd /home/sh/oss/sel4-musl-helloworld/vendor/camkes-project
./simulate
```

Expected output should include:

```text
Hello World from seL4 + CAmkES + musl libc
```

For QEMU started with `-nographic`, exit with:

```text
Ctrl-A X
```

## Notes

- CAmkES builds one app at a time via `-DCAMKES_APP=<name>` at configure time.
- The app name must match a directory under `projects/camkes/apps/`.
- In this layout, `projects/camkes/apps/hello` is a symlink to
  `apps/hello`.
- `SIMULATION` must be enabled during configure, otherwise the `simulate`
  script may not be generated.
- `repo sync` may update files under `vendor/camkes-project`. If the symlink is
  ever removed, recreate it before configuring.
- This README uses `init-build.sh` instead of `griddle`. In this CAmkES
  checkout, `griddle` fails before configure because it cannot parse the
  multi-line `set(...)` entries in `easy-settings.cmake`.
- CAmkES already brings in `musllibc` in the standard project checkout. The
  first milestone is to confirm that normal CAmkES `printf` reaches the QEMU
  serial console. Later milestones can make the musl syscall/runtime boundary
  explicit.
