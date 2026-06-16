#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#define RUBY_ALLOC_ARENA_SIZE (64 * 1024 * 1024)
#define DEFAULT_ALIGNMENT (sizeof(void *) * 2)
#define MMAP_ALIGNMENT 4096
#define ALLOC_MAGIC 0x52414c43u

typedef struct alloc_header {
    size_t size;
    size_t capacity;
    struct alloc_header *next_free;
    unsigned int is_free;
    unsigned int magic;
} alloc_header_t;

static unsigned char ruby_alloc_arena[RUBY_ALLOC_ARENA_SIZE] __attribute__((aligned(4096)));
static size_t ruby_alloc_offset;
static alloc_header_t *free_list;

static int is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

static void *header_payload(alloc_header_t *header)
{
    return (void *)(header + 1);
}

static alloc_header_t *payload_header(void *ptr)
{
    return (alloc_header_t *)ptr - 1;
}

static int block_matches(alloc_header_t *header, size_t size, size_t alignment)
{
    return header->capacity >= size &&
           ((uintptr_t)header_payload(header) % alignment) == 0;
}

static void *reuse_free_block(size_t size, size_t alignment)
{
    alloc_header_t *prev = 0;
    alloc_header_t *current = free_list;

    while (current != 0) {
        if (block_matches(current, size, alignment)) {
            if (prev == 0) {
                free_list = current->next_free;
            } else {
                prev->next_free = current->next_free;
            }
            current->size = size;
            current->next_free = 0;
            current->is_free = 0;
            return header_payload(current);
        }

        prev = current;
        current = current->next_free;
    }

    return 0;
}

static void *arena_alloc(size_t size, size_t alignment)
{
    uintptr_t base;
    uintptr_t header_addr;
    uintptr_t next;
    alloc_header_t *header;
    void *reused;

    if (size == 0) {
        size = 1;
    }
    if (alignment < DEFAULT_ALIGNMENT) {
        alignment = DEFAULT_ALIGNMENT;
    }
    if (!is_power_of_two(alignment)) {
        errno = EINVAL;
        return 0;
    }

    reused = reuse_free_block(size, alignment);
    if (reused != 0) {
        return reused;
    }

    base = (uintptr_t)ruby_alloc_arena + ruby_alloc_offset;
    header_addr = align_up(base + sizeof(*header), alignment) - sizeof(*header);
    next = header_addr + sizeof(*header) + size;

    if (next < header_addr || next > (uintptr_t)ruby_alloc_arena + sizeof(ruby_alloc_arena)) {
        errno = ENOMEM;
        return 0;
    }

    header = (alloc_header_t *)header_addr;
    header->size = size;
    header->capacity = size;
    header->next_free = 0;
    header->is_free = 0;
    header->magic = ALLOC_MAGIC;
    ruby_alloc_offset = next - (uintptr_t)ruby_alloc_arena;
    return header_payload(header);
}

void *malloc(size_t size)
{
    return arena_alloc(size, DEFAULT_ALIGNMENT);
}

void free(void *ptr)
{
    alloc_header_t *header;

    if (ptr == 0) {
        return;
    }

    header = payload_header(ptr);
    if (header->magic != ALLOC_MAGIC || header->is_free) {
        return;
    }

    header->is_free = 1;
    header->next_free = free_list;
    free_list = header;
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *ptr;

    if (count != 0 && size > (size_t)-1 / count) {
        errno = ENOMEM;
        return 0;
    }

    total = count * size;
    ptr = malloc(total);
    if (ptr != 0) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    void *new_ptr;
    size_t old_size;

    if (ptr == 0) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return 0;
    }

    old_size = payload_header(ptr)->size;
    if (payload_header(ptr)->capacity >= size) {
        payload_header(ptr)->size = size;
        return ptr;
    }

    new_ptr = malloc(size);
    if (new_ptr == 0) {
        return 0;
    }

    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    free(ptr);
    return new_ptr;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *ptr;

    if (memptr == 0 || alignment < sizeof(void *) || !is_power_of_two(alignment)) {
        return EINVAL;
    }

    ptr = arena_alloc(size, alignment);
    if (ptr == 0) {
        return errno == EINVAL ? EINVAL : ENOMEM;
    }

    *memptr = ptr;
    return 0;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    if (alignment == 0 || !is_power_of_two(alignment)) {
        errno = EINVAL;
        return 0;
    }

    return arena_alloc(size, alignment);
}

size_t malloc_usable_size(void *ptr)
{
    if (ptr == 0) {
        return 0;
    }

    return payload_header(ptr)->capacity;
}

void *__libc_malloc(size_t size)
{
    return malloc(size);
}

void *__libc_malloc_impl(size_t size)
{
    return malloc(size);
}

void __libc_free(void *ptr)
{
    free(ptr);
}

void *__libc_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *ptr;

    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;

    ptr = arena_alloc(length, MMAP_ALIGNMENT);
    if (ptr == 0) {
        return MAP_FAILED;
    }

    return ptr;
}

void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return mmap(addr, length, prot, flags, fd, offset);
}

int munmap(void *addr, size_t length)
{
    (void)addr;
    (void)length;
    return 0;
}

int __munmap(void *addr, size_t length)
{
    return munmap(addr, length);
}
