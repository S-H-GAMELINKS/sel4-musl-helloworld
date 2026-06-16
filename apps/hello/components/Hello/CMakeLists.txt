cmake_minimum_required(VERSION 3.16.0)

project(ComponentHello C)

set(HELLO_REPO_ROOT
    ""
    CACHE PATH "Path to the sel4-musl-helloworld repository root"
)

if(HELLO_REPO_ROOT)
    get_filename_component(HELLO_REPO_ROOT_ABS "${HELLO_REPO_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
    set(RUBY_BUILD_DIR "${HELLO_REPO_ROOT_ABS}/build/ruby-sel4-musl")
    set(RUBY_SOURCE_DIR "${HELLO_REPO_ROOT_ABS}/vendor/ruby")

    DeclareCAmkESComponent(
        Hello
        SOURCES
            src/hello.c
            src/ruby_alloc.c
            src/ruby_platform.c
            src/ruby_pthread.c
        INCLUDES
            ${RUBY_BUILD_DIR}/.ext/include/x86_64-linux-musl
            ${RUBY_SOURCE_DIR}/include
        LIBS
            ${RUBY_BUILD_DIR}/libruby-static.a
    )
else()
    DeclareCAmkESComponent(Hello SOURCES src/hello.c)
endif()
