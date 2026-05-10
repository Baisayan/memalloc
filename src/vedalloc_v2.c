#include "vedalloc_v2.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define NUM_CLASSES 8
#define MAGIC 0xAADCF
#define ALIGN8(x) (((x) + 7) & ~7)

static size_t size_classes[NUM_CLASSES] = {
    8, 16, 32, 64, 128, 256, 512, 1024
};

typedef struct block {
    struct block *next;
} block;

typedef struct page_header {
    struct page_header *next;
    size_t block_size;
    block *free_list;
} page_header;

typedef struct large_header {
    uint32_t magic;
    size_t size;
} large_header;

// one list per size class
static page_header *class_pages[NUM_CLASSES] = {0};

static int get_class_index(size_t size) {
    for (int i = 0; i < NUM_CLASSES; i++) {
        if (size <= size_classes[i]) return i;
    }
    return -1; // too large
}

static page_header *allocate_page(size_t block_size) {
    void *mem = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mem == MAP_FAILED) return NULL;

    page_header *page = (page_header *)mem;
    page->block_size = block_size;
    page->free_list = NULL;
    page->next = NULL;

    // split page into blocks
    size_t offset = sizeof(page_header);
    size_t usable = PAGE_SIZE - offset;
    size_t count = usable / block_size;
    char *cursor = (char *)mem + offset;

    for (size_t i = 0; i < count; i++) {
        block *b = (block *)cursor;
        b->next = page->free_list;
        page->free_list = b;
        cursor += block_size;
    }

    return page;
}


static void *alloc_large(size_t size) {
    size_t total = sizeof(large_header) + size;
    total = (total + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mem == MAP_FAILED) return NULL;
    large_header *hdr = (large_header *)mem;
    hdr->magic = MAGIC;
    hdr->size = total;

    return (char *)mem + sizeof(large_header);
}


void *vedalloc_v2(size_t size) {
    if (size == 0) size = 1;
    size = ALIGN8(size);

    int idx = get_class_index(size);
    if (idx == -1) return alloc_large(size); // large allocs

    size_t class_size = size_classes[idx];
    page_header *page = class_pages[idx];

    // find page with free block
    while (page && page->free_list == NULL) {
        page = page->next;
    }

    // if no page allocate new
    if (!page) {
        page = allocate_page(class_size);
        if (!page) return NULL;
        page->next = class_pages[idx];
        class_pages[idx] = page;
    }

    // pop block
    block *b = page->free_list;
    page->free_list = b->next;

    return (void *)b;
}

void vedfree_v2(void *ptr) {
    if (!ptr) return;
    large_header *lh = (large_header *)((char *)ptr - sizeof(large_header));

    if (lh->magic == MAGIC) {
        munmap((void *)lh, lh->size);
        return;
    }

    // find page start
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t page_addr = addr & ~(PAGE_SIZE - 1);
    page_header *page = (page_header *)page_addr;

    block *b = (block *)ptr;
    b->next = page->free_list;
    page->free_list = b;
}