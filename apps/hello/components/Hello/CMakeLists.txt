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
    set(HELLO_RUBY_DIR "${CMAKE_CURRENT_LIST_DIR}/ruby")
    set(HELLO_RUBY_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/ruby.cpio")
    set(HELLO_RUBY_ARCHIVE_C "${CMAKE_CURRENT_LIST_DIR}/src/ruby_archive.c")
    find_program(XXD_EXECUTABLE xxd REQUIRED)

    add_custom_command(
        OUTPUT ${HELLO_RUBY_ARCHIVE}
        COMMAND /bin/sh -c "find . -type f | sed 's#^./##' | sort | cpio --quiet --create -H newc --file='${HELLO_RUBY_ARCHIVE}'"
        DEPENDS ${HELLO_RUBY_DIR}/shell.rb
        WORKING_DIRECTORY ${HELLO_RUBY_DIR}
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${HELLO_RUBY_ARCHIVE_C}
        COMMAND /bin/sh -c "'${XXD_EXECUTABLE}' -i -n ruby_cpio '${HELLO_RUBY_ARCHIVE}' > '${HELLO_RUBY_ARCHIVE_C}'"
        DEPENDS ${HELLO_RUBY_ARCHIVE}
        VERBATIM
    )
    set_source_files_properties(${HELLO_RUBY_ARCHIVE_C} PROPERTIES GENERATED TRUE)
    add_custom_target(hello_ruby_archive DEPENDS ${HELLO_RUBY_ARCHIVE_C})

    DeclareCAmkESComponent(
        Hello
        SOURCES
            src/hello.c
            src/ruby_alloc.c
            src/ruby_platform.c
            src/ruby_pthread.c
            ${HELLO_RUBY_ARCHIVE_C}
        INCLUDES
            ${RUBY_BUILD_DIR}/.ext/include/x86_64-linux-musl
            ${RUBY_SOURCE_DIR}/include
        LIBS
            ${RUBY_BUILD_DIR}/libruby-static.a
    )

    if(TARGET hello.instance.bin)
        add_dependencies(hello.instance.bin hello_ruby_archive)
    endif()
else()
    DeclareCAmkESComponent(Hello SOURCES src/hello.c)
endif()
