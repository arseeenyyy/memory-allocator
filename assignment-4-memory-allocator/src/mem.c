#define _DEFAULT_SOURCE

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <unistd.h>

#include "mem.h"
#include "mem_internals.h"
#include "util.h"

void debug_block(struct block_header* b, const char* fmt, ... );
void debug(const char* fmt, ... );

extern inline block_size size_from_capacity( block_capacity cap );
extern inline block_capacity capacity_from_size( block_size sz );

static bool block_is_big_enough( size_t query, struct block_header* block ) { return block->capacity.bytes >= query; }
//необходимое кол-во страниц для данных(страница обычно 4096, если не хватает целого кол-ва страниц для данных, то ++)
static size_t pages_count( size_t mem ) { return mem / getpagesize() + ((mem % getpagesize()) > 0); }
//округляем до ближайшего кратного размера страницы
static size_t round_pages( size_t mem ) { return getpagesize() * pages_count( mem ) ; }

static void block_init( void* restrict addr, block_size block_sz, void* restrict next ) {
  *((struct block_header*)addr) = (struct block_header) {
    .next = next,
    .capacity = capacity_from_size(block_sz),
    .is_free = true
  };
}

static size_t region_actual_size( size_t query ) { return size_max( round_pages( query ), REGION_MIN_SIZE ); }

extern inline bool region_is_invalid( const struct region* r );



static void* map_pages(void const* addr, size_t length, int additional_flags) {
  return mmap( (void*) addr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | additional_flags , -1, 0 );
}

/*  аллоцировать регион памяти и инициализировать его блоком */
static struct region alloc_region  ( void const * addr, size_t query ) {
  const size_t region_size = region_actual_size(size_from_capacity ((block_capacity){.bytes = query}).bytes);

  void *mp = map_pages(addr, region_size, MAP_FIXED_NOREPLACE);
  if (mp == MAP_FAILED) {
    mp = map_pages(addr, region_size, 0);
    if (mp == MAP_FAILED) {
      return REGION_INVALID;
    }
  }
  struct region region = (struct region) {
    .addr = mp, 
    .extends = mp == addr, 
    .size = region_size
  };
  block_init(region.addr, (block_size) {region.size}, NULL);
  return region; 
}

static void* block_after( struct block_header const* block );

void* heap_init( size_t initial ) {
  const struct region region = alloc_region( HEAP_START, initial );
  if ( region_is_invalid(&region) ) return NULL;

  return region.addr;
}

#define BLOCK_MIN_CAPACITY 24

/*  --- Разделение блоков (если найденный свободный блок слишком большой )--- */

static bool block_splittable( struct block_header* restrict block, size_t query) {
  return block-> is_free && query + offsetof( struct block_header, contents ) + BLOCK_MIN_CAPACITY <= block->capacity.bytes;
}

static bool split_if_too_big( struct block_header* block, size_t query ) {
  if (!block_splittable(block, query) || !block) return false;
  void* address = block->contents + query;
  block_init(address, (block_size) {.bytes = block->capacity.bytes - query}, block->next);
  block->next = address;
  block->capacity.bytes = query;
  return true;
}


/*  --- Слияние соседних свободных блоков --- */

static void* block_after( struct block_header const* block )              {
  return  (void*) (block->contents + block->capacity.bytes);
}
static bool blocks_continuous (struct block_header const* fst, struct block_header const* snd ) {
  return (void*)snd == block_after(fst);
}

static bool mergeable(struct block_header const* restrict fst, struct block_header const* restrict snd) {
  return fst->is_free && snd->is_free && blocks_continuous( fst, snd ) ;
}

static bool try_merge_with_next( struct block_header* block ) {
  if (!block) return false;
  struct block_header *next_block = block -> next;

  if (!next_block || !block -> is_free || !next_block -> is_free || !mergeable(block, next_block)) return false;
  uint8_t* end_of_block = (uint8_t*) block + size_from_capacity(block -> capacity).bytes;
  if (end_of_block != (uint8_t*) next_block) return false;

  block -> capacity.bytes += size_from_capacity(next_block -> capacity).bytes; 
  block -> next = next_block -> next;
  return true;
}


/*  --- ... ecли размера кучи хватает --- */

struct block_search_result {
  enum {BSR_FOUND_GOOD_BLOCK, BSR_REACHED_END_NOT_FOUND, BSR_CORRUPTED} type;
  struct block_header* block;
};


static struct block_search_result find_good_or_last(struct block_header* restrict block, size_t sz) {
  if (block == NULL) {
    return (struct block_search_result) {.type = BSR_CORRUPTED, .block = NULL};
  }
  struct block_header* last_block = block;
  while (block != NULL) {
    if (block->is_free) {
        while (try_merge_with_next(block)); 
        if (block_is_big_enough(sz, block)) {
            return (struct block_search_result) {.type = BSR_FOUND_GOOD_BLOCK, .block = block};
          }
      }
      last_block = block;
      block = block->next;
  }
  if (last_block->is_free && block_is_big_enough(sz, last_block)) {
      return (struct block_search_result) {.type = BSR_FOUND_GOOD_BLOCK, .block = last_block};
  }

  return (struct block_search_result) {.type = BSR_REACHED_END_NOT_FOUND, .block = last_block};
}


/*  Попробовать выделить память в куче начиная с блока `block` не пытаясь расширить кучу
 Можно переиспользовать как только кучу расширили. */
static struct block_search_result try_memalloc_existing(size_t query, struct block_header* block) {
  struct block_search_result res = find_good_or_last(block, query);
  if (res.type == BSR_CORRUPTED) {
    return (struct block_search_result) {.type = BSR_CORRUPTED, .block = NULL};
  }
  if (res.type == BSR_FOUND_GOOD_BLOCK) {
    if (split_if_too_big(res.block, query)) {
      res.block->is_free = false;
    }
  }
  return res;
}

static struct block_header* grow_heap(struct block_header* restrict last, size_t query) {
  struct region new_region = alloc_region(block_after(last), size_max(query, BLOCK_MIN_CAPACITY));
  if (region_is_invalid(&new_region)) {
    return NULL; 
  }
  if (last == NULL) {
    last = (struct block_header*)new_region.addr;
  } else {
    last->next = (struct block_header*)new_region.addr;
  }
  if (new_region.extends) {
    if (try_merge_with_next(last)) {
      return last;
    }
  }
  return last->next;
}


/*  Реализует основную логику malloc и возвращает заголовок выделенного блока */
static struct block_header* memalloc(size_t query, struct block_header* heap_start) {
  size_t aligned_query = size_max(query, BLOCK_MIN_CAPACITY);
  struct block_search_result search_result = try_memalloc_existing(aligned_query, heap_start);
  if (search_result.type == BSR_FOUND_GOOD_BLOCK) return search_result.block;
  if (search_result.type == BSR_REACHED_END_NOT_FOUND) {
    struct block_header* new_block = grow_heap(search_result.block, aligned_query);
    if (new_block) {
      search_result = try_memalloc_existing(aligned_query, new_block);
      if (search_result.type == BSR_FOUND_GOOD_BLOCK) {
        return search_result.block;
      }
    }
  }
  return NULL;
}


void* _malloc( size_t query ) {
  struct block_header* const addr = memalloc( query, (struct block_header*) HEAP_START );
  if (addr) return addr->contents;
  else return NULL;
}

static struct block_header* block_get_header(void* contents) {
  return (struct block_header*) (((uint8_t*)contents)-offsetof(struct block_header, contents));
}

void _free(void* mem) {
  if (!mem) return ;
  struct block_header* header = block_get_header( mem );
  header->is_free = true;
  while(try_merge_with_next(header));
}

/*  освободить всю память, выделенную под кучу */
void heap_term() {
  struct block_header *current = (struct block_header*) HEAP_START;
  while (current != NULL) {
    struct block_header *next = current->next;
    block_size to_free = size_from_capacity(current->capacity);
    while (next != NULL && blocks_continuous(current, next)) {
      next = next -> next;
      to_free.bytes += size_from_capacity(next->capacity).bytes;
    }
    if (munmap(current, to_free.bytes) == -1) {
      perror("munmap failed"); 
      return;
    }
    current = next;
  }
}
