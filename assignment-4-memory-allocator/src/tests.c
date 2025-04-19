#include <assert.h>
#include <stdio.h>

#include "mem.h"
#include "mem_internals.h"
#include "tests.h"

int test1() {
    printf("Running test1: default memory allocation\n");
    size_t size = 128;
    void* ptr = _malloc(size);

    if (ptr != NULL) {
        struct block_header* block = (struct block_header*)((uint8_t*)ptr - offsetof(struct block_header, contents));
        assert(block->capacity.bytes >= size);
        assert(!block->is_free);

        printf("Test1 passed\n");
        _free(ptr);
        return 0;
    }
    printf("Test1 failed\n");
    return 1;
}

int test2() {
    printf("Running test2: freeing one block from several allocated ones\n");
    void* ptr1 = _malloc(64);
    void* ptr2 = _malloc(128);

    if (ptr1 && ptr2) {
        struct block_header* block1 = (struct block_header*)((uint8_t*)ptr1 - offsetof(struct block_header, contents));
        struct block_header* block2 = (struct block_header*)((uint8_t*)ptr2 - offsetof(struct block_header, contents));

        assert(!block1->is_free);
        assert(!block2->is_free);

        _free(ptr1);
        assert(block1->is_free);

        printf("Test2 passed\n");
        _free(ptr2);
        return 0;
    }
    printf("Test2 failed\n");
    return 1;
}

int test3() {
    printf("Running test3: Freeing two blocks from multiple allocations\n");
    void* ptr1 = _malloc(64);
    void* ptr2 = _malloc(128);
    void* ptr3 = _malloc(256);

    if (ptr1 && ptr2 && ptr3) {
        struct block_header* block1 = (struct block_header*)((uint8_t*)ptr1 - offsetof(struct block_header, contents));
        struct block_header* block3 = (struct block_header*)((uint8_t*)ptr3 - offsetof(struct block_header, contents));

        assert(!block1->is_free);
        assert(!block3->is_free);

        _free(ptr1);
        assert(block1->is_free);

        _free(ptr3);
        assert(block3->is_free);

        printf("Test3 passed\n");
        _free(ptr2);
        return 0;
    }
    printf("Test3 failed\n");
    return 1;
}

int test4() {
    printf("Running test4: new region expands the old one\n");
    void* ptr1 = _malloc(1024 * 1024); 
    void* ptr2 = _malloc(1024 * 1024); 

    if (ptr1 && ptr2) {
        struct block_header* block1 = (struct block_header*)((uint8_t*)ptr1 - offsetof(struct block_header, contents));
        struct block_header* block2 = (struct block_header*)((uint8_t*)ptr2 - offsetof(struct block_header, contents));

        assert(!block1->is_free);
        assert(!block2->is_free);

        assert((uint8_t*)block2 >= (uint8_t*)block1 + size_from_capacity(block1->capacity).bytes);

        printf("Test4 passed\n");
        _free(ptr1);
        _free(ptr2);
        return 0;
    }
    printf("Test4 failed\n");
    return 1;
}

int test5() {
    printf("Running test5: new region is allocated in a different location.\n");
    void* ptr1 = _malloc(1024 * 1024); 
    void* ptr2 = _malloc(1024 * 1024); 
    void* ptr3 = _malloc(1024 * 1024); 

    if (ptr1 && ptr2 && ptr3) {
        struct block_header* block1 = (struct block_header*)((uint8_t*)ptr1 - offsetof(struct block_header, contents));
        struct block_header* block3 = (struct block_header*)((uint8_t*)ptr3 - offsetof(struct block_header, contents));

        assert(!block1->is_free);
        assert(!block3->is_free);

        assert(block3 != block1 && block3 != (struct block_header*)((uint8_t*)ptr2 - offsetof(struct block_header, contents)));

        printf("Test5 passed\n");
        _free(ptr1);
        _free(ptr2);
        _free(ptr3);
        return 0;
    }
    printf("Test5 failed\n");
    return 1;
}
