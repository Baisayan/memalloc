#include "vedalloc.h"
#include <unistd.h>
#include <assert.h>

#define ALIGN8(x) (((x) + 7) & ~7)
#define HEAP_MAGIC 0x55
#define BLOCK_MAGIC 0xDD
#define PAGE_SIZE 4096

typedef struct block_header {
  uint8_t magic;
  bool in_use;
  size_t size;
  struct block_header *prev;
  struct block_header *next;
} block_header;

typedef struct heap_header {
  uint8_t magic;
} heap_header;

char *heap_start = NULL;

void vedalloc_reset() {
  heap_start = NULL;
}

static block_header *find_last_block() {
  block_header *block = (block_header *)(heap_start + sizeof(heap_header));
  while (block->next) block = block->next;
  return block;
}

bool vedfree(void *ptr) {
  if (!ptr) return false;
  block_header *block = (block_header *)((char *)ptr - sizeof(block_header));

  if (block->magic != BLOCK_MAGIC || !block->in_use) {
    return false;
  }
  block->in_use = false;

  // forward coalesce
  if (block->next && !block->next->in_use) {
    block_header *not_used_next = block->next;
    block->size += sizeof(block_header) + not_used_next->size;
    block->next = not_used_next->next;
    if (not_used_next->next) not_used_next->next->prev = block;
  }

  // backward coalesce
  if (block->prev && !block->prev->in_use) {
    block_header *prev = block->prev;
    prev->size += sizeof(block_header) + block->size;
    prev->next = block->next;
    if (block->next) block->next->prev = prev;
  }

  return true;
}

static void *add_used_block(size_t size) {
    block_header *block = (block_header *)(heap_start + sizeof(heap_header));
    block_header *fit = NULL;

    while (block) {
      if (!block->in_use && block->size >= size) {
        fit = block;
        break;
      } 
      block = block->next;
    }

    // if no big enough blocks, extend heap
    if (!fit) {
      block_header *last = find_last_block();

      size_t grow = ((size / PAGE_SIZE) + 1) * PAGE_SIZE;
      if (sbrk(grow) == (void *)-1) return NULL;

      last->size += grow;
      fit = last;
    }

    fit->in_use = true;
    size_t remaining = fit->size - size;

    if (remaining > sizeof(block_header) + 1) {
      block_header *new_block =
          (block_header *)((char *)fit + sizeof(block_header) + size);

      new_block->magic = BLOCK_MAGIC;
      new_block->in_use = false;
      new_block->size = remaining - sizeof(block_header);
      new_block->prev = fit;
      new_block->next = fit->next;

      if (new_block->next)
          new_block->next->prev = new_block;

      fit->next = new_block;
      fit->size = size;
  }

  return (char *)fit + sizeof(block_header);
}

void *vedalloc(size_t size) {
  if (size == 0) size = 1;
  size = ALIGN8(size); 

  if (!heap_start) {
    heap_start = sbrk(0);
    if (sbrk(PAGE_SIZE) == (void *)-1) return NULL;
  }

  if (*heap_start != HEAP_MAGIC) {
    heap_header *hdr = (heap_header *)heap_start;
    hdr->magic = HEAP_MAGIC;

    // create first free block
    block_header *first = (block_header *)(heap_start + sizeof(heap_header));

    first->magic = BLOCK_MAGIC;
    first->in_use = false;
    first->size = PAGE_SIZE - sizeof(heap_header) - sizeof(block_header);
    first->next = NULL;
    first->prev = NULL;
  }

  return add_used_block(size);
}
