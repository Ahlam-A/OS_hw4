/*
HOW TO RUN? (easiest(?) method)
a. copy the code
b. paste it in main.cpp file inside a clion project that includes three files:
	1. main.cpp (with this code pasted in it.)
	2. malloc_2.h (yours, theoretically you don't HAVE TO have one a .h)
	3. malloc_2.cpp (yours)
c. run/Debug


NOTE1: this code uses forking and calls your functions from child process. expect difficulties setting breakpoints in your code when debugging with clion. 
       if you still wish to set BP, you need to address this issue

NOTE2: you can run it with GDB or CLI if you wish.

NOTE3: it seems (from my understanding of the way the tests were coded) that srealloc(NULL, <somesize>); was supposed to act like smalloc(<somesize>);.
       if i didnt miss anything, in our HW oldp=NULL is not addreseed in srealloc. therefore expect SEGFAULT in srealloc. one easy fix is:
       		- if srealloc was called with NULL == oldp and size == <somesize>, call smalloc(<somesize>), check ret_val and return it. since i didnt see out staff address such a call, i assume it 			  will not happen and therefore do whatever is needed to keep the test from getting angry and SEGFAULTing.  

IMPORTANT_NOTE: I HOLD NO responsability for these tests no to their results, they may be faulty and it's up to YOU to decide, if you wish to use them.

 */

#include "malloc_2.cpp"
#include <unistd.h>
#include <assert.h>
#include <cstdlib>
#include <sys/wait.h>
#include <iostream>

#define assert_state(_initial, _expected)\
	do {\
		assert(_num_free_blocks() - _initial.free_blocks == _expected.free_blocks); \
		assert(_num_free_bytes() - _initial.free_bytes == _expected.free_bytes); \
		assert(_num_allocated_blocks() - _initial.allocated_blocks == _expected.allocated_blocks); \
		assert(_num_allocated_bytes() - _initial.allocated_bytes == _expected.allocated_bytes); \
		assert(_num_meta_data_bytes() - _initial.meta_data_bytes == _expected.meta_data_bytes); \
	} while (0)

typedef unsigned char byte;
const int BLOCK_MAX_COUNT=10, BLOCK_MAX_SIZE=10, LAST=9999;
const byte GARBAGE=255;

typedef struct {
    size_t free_blocks, free_bytes, allocated_blocks, allocated_bytes,
            meta_data_bytes;
} HeapState;

size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();
void *sbrk(intptr_t);

/*******************************************************************************
 *  AUXILIARY FUNCTIONS
 ******************************************************************************/

void get_initial_state(HeapState &initial) {
    initial.free_blocks = _num_free_blocks();
    initial.free_bytes = _num_free_bytes();
    initial.allocated_blocks = _num_allocated_blocks();
    initial.allocated_bytes = _num_allocated_bytes();
    initial.meta_data_bytes = _num_meta_data_bytes();
}

void get_block_addresses(byte **arr, byte *heap, const size_t SIZES[], int count) {
    const size_t MT = _size_meta_data();
    arr[0] = heap;
    for (int i=1; i<count; ++i)
        arr[i] = arr[i-1] + SIZES[i-1] + MT;
}

/* Tells the test-function that we're expecting a new block to be allocated on
 * the heap. Then the test-function has to check if this actually happenned.*/
void add_expected_block(HeapState &state, size_t size) {
    state.allocated_blocks++;
    state.allocated_bytes += size;
    state.meta_data_bytes += _size_meta_data();
}

/* Tells the test-function that a block was supposed to be
 * freed. Then the test-function has to check if this actually happenned.*/
void free_expected_block(HeapState &state, size_t size) {
    state.free_blocks++;
    state.free_bytes += size;
}

/* Tells the test-function that a previously-freed block should be "un-freed"
 * (reused for a new malloc). Then the test-function has to check if this
 * actually happenned.*/
void reuse_expected_block(HeapState &state, size_t size) {
    state.free_blocks--;
    state.free_bytes -= size;
}


/* Checks that the heap data, stored at pointer "heap", is identical to the
 * expected data. "count" is the number of blocks that are supposed to be
 * allocated. "sizes" is an ordered list of the blocks' sizes; generally this
 * list has too many items so this function will only test the first "count"
 * items. */
bool check_data(byte *heap, const byte expected[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE],
                int count, const size_t sizes[BLOCK_MAX_COUNT]) {
    heap += _size_meta_data();
    for (int i=0; i<count; ++i) {
        for (size_t j=0; j<sizes[i]; ++j) {
            if (expected[i][j] != GARBAGE && expected[i][j] != *heap)
                return false;
            ++heap;
        }
        heap += _size_meta_data();
    }
    return true;
}

byte* malloc_byte(int count) {
    return static_cast<byte*>(smalloc(sizeof(byte)*count));
}

byte* calloc_byte(int count) {
    return static_cast<byte*>(scalloc(count, sizeof(byte)));
}

byte* realloc_byte(void *oldp, size_t size) {
    return static_cast<byte*>(srealloc(oldp, size));
}

/* Copies a 2-dimensional array into another. Only copy the first "count" rows,
 * and in the i row only copy the first sizes[i] items. */
void fill_data(const byte source[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE],
               byte *target[BLOCK_MAX_COUNT], const int count,
               const size_t sizes[BLOCK_MAX_COUNT]) {
    for (int i=0; i<count; ++i)
        for (int j=0; j<sizes[i]; ++j)
            target[i][j] = source[i][j];
}

/*******************************************************************************
 *  TESTS
 ******************************************************************************/

void test_malloc_then_free() {
    /* Expected sizes of memory blocks after all allocations: */
    const size_t BLOCK_SIZES[BLOCK_MAX_COUNT] = {3, 8, 1, 7, LAST};

    /* Expected data in heap. The first group are the bytes in the first block,
     * the second group for the second block, etc. . Of course the blocks are
     * different sizes but I had to make the array's size constant, so the tests
     * just ignore the bytes after the end of the block. */
    const byte DATA[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE] =
            {{1,2,3}, {11,12,13,14,15,16,17,18}, {20}, {1,2,3,4,5,6,7}, {static_cast<byte>(LAST)}};

    /* Pointer to beginning of heap. Will be used later when I test that the
     * heap looks exactly as I expect it to. */
    byte *heap = static_cast<byte*>(sbrk(0));

    /* Initial heap state (_num_allocated_blocks, etc.). Theoretically it should
     * be all zeros, but sometimes the programs uses malloc before my tests even
     * run and I want to ignore everything that was malloc'ed before then. */
    HeapState initial;
    get_initial_state(initial);

    /* This variable will hold the expected state of the heap (_num_allocated_blocks,
     * etc.). I will compare it to the actual state as part of the tests. */
    HeapState expected = {0,0,0,0,0};

    byte *p[4];

    /* Allocate block 0 and fill with data */
    p[0] = static_cast<byte*>(smalloc(BLOCK_SIZES[0]));
    add_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    for (int j=0; j<BLOCK_SIZES[0]; ++j)
        *(p[0]+j) = DATA[0][j];
    assert(check_data(heap, DATA, 1, BLOCK_SIZES));

    /* Allocate block 1 */
    p[1] = static_cast<byte*>(smalloc(BLOCK_SIZES[1]));
    add_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);
    for (int j=0; j<BLOCK_SIZES[1]; ++j)
        *(p[1]+j) = DATA[1][j];
    assert(check_data(heap, DATA, 2, BLOCK_SIZES));

    /* Allocate block 2 */
    p[2] = static_cast<byte*>(smalloc(BLOCK_SIZES[2]));
    add_expected_block(expected, BLOCK_SIZES[2]);
    assert_state(initial, expected);
    for (int j=0; j<BLOCK_SIZES[2]; ++j)
        *(p[2]+j) = DATA[2][j];
    assert(check_data(heap, DATA, 3, BLOCK_SIZES));

    /* Allocate block 3 */
    p[3] = static_cast<byte*>(smalloc(BLOCK_SIZES[3]));
    add_expected_block(expected, BLOCK_SIZES[3]);
    assert_state(initial, expected);
    for (int j=0; j<BLOCK_SIZES[3]; ++j)
        *(p[3]+j) = DATA[3][j];
    assert(check_data(heap, DATA, 4, BLOCK_SIZES));

    /* Free all */
    sfree(p[0]);
    free_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    sfree(p[1]);
    free_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);
    sfree(p[2]);
    free_expected_block(expected, BLOCK_SIZES[2]);
    assert_state(initial, expected);
    sfree(p[3]);
    free_expected_block(expected, BLOCK_SIZES[3]);
    assert_state(initial, expected);
}

void test_reuse_after_free() {
    const size_t BLOCK_SIZES[BLOCK_MAX_COUNT] = {3, 8, 1, 7, 9, 2, LAST};
    byte DATA[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE] =
            {{1,2,3}, {11,12,13,14,15,16,17,18}, {20}, {21,22,23,24,25,26,27},
             {31,32,33,34,35,36,37,38,39}, {91, 92}, {static_cast<byte>(LAST)}};
    byte re1[] = {111,112,113,114,115};
    byte re3[] = {101, 102, 103, 104, 105, 106, 107};
    byte *heap = static_cast<byte*>(sbrk(0));
    HeapState initial;
    get_initial_state(initial);

    HeapState expected = {0,0,0,0,0};
    byte *p[6];

    /* allocate and copy first 4 arrays: */
    for (int i=0; i<4; ++i) {
        p[i] = static_cast<byte*>(smalloc(BLOCK_SIZES[i]));
        add_expected_block(expected, BLOCK_SIZES[i]);
        for (int j = 0; j < BLOCK_SIZES[i]; ++j) {
            *(p[i]+j) = DATA[i][j];
        }
    }
    assert_state(initial, expected);
    assert(check_data(heap, DATA, 4, BLOCK_SIZES));

    /* free blocks 0, 1, 3: */
    sfree(p[3]);
    free_expected_block(expected, BLOCK_SIZES[3]);
    assert_state(initial, expected);
    sfree(p[0]);
    free_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    sfree(p[1]);
    free_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);
    for (int i=0; i<3; ++i)
        DATA[0][i] = GARBAGE;
    for (int i=0; i<8; ++i)
        DATA[1][i] = GARBAGE;
    for (int i=0; i<7; ++i)
        DATA[3][i] = GARBAGE;

    /* malloc should reuse block 1 then 3: */
    p[1] = static_cast<byte*>(smalloc(sizeof(byte)*5));
    reuse_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);
    for (int i=0; i<5; ++i) {
        *(p[1]+i) = re1[i];
        DATA[1][i] = re1[i];
    }

    p[3] = static_cast<byte*>(smalloc(sizeof(byte)*7));
    reuse_expected_block(expected, BLOCK_SIZES[3]);
    assert_state(initial, expected);
    for (int i=0; i<7; ++i) {
        *(p[3]+i) = re3[i];
        DATA[3][i] = re3[i];
    }

    check_data(heap, DATA, 4, BLOCK_SIZES);

    /* now need to allocate more space: */
    p[4] = static_cast<byte*>(smalloc(BLOCK_SIZES[4]));
    add_expected_block(expected, BLOCK_SIZES[4]);
    assert_state(initial, expected);
    for (int i=0; i<BLOCK_SIZES[4]; ++i)
        *(p[4]+i) = DATA[4][i];
    check_data(heap, DATA, 5, BLOCK_SIZES);

    /* reuse block 0: */
    p[0] = static_cast<byte*>(smalloc(sizeof(byte)));
    reuse_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    *(p[0]) = 200;
    DATA[0][0] = 200;
    check_data(heap, DATA, 5, BLOCK_SIZES);

    /* allocate last block: */
    p[5] = static_cast<byte*>(smalloc(BLOCK_SIZES[5]));
    add_expected_block(expected, BLOCK_SIZES[5]);
    assert_state(initial, expected);
    for (int i=0; i<BLOCK_SIZES[5]; ++i)
        *(p[5]+i) = DATA[5][i];
    check_data(heap, DATA, 6, BLOCK_SIZES);

    /* free block 3 again */
    sfree(p[3]);
    free_expected_block(expected, BLOCK_SIZES[3]);
    assert_state(initial, expected);
}

void test_calloc() {
    const size_t BLOCK_SIZES[BLOCK_MAX_COUNT] = {1,2,3, LAST};
    byte DATA[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE] =
            {{10}, {20,21}, {30,31,32}, {static_cast<byte>(LAST)}};
    const byte ZEROS[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE] = {{},{},{},{static_cast<byte>(LAST)}};
    byte *heap = static_cast<byte*>(sbrk(0));
    HeapState initial;
    get_initial_state(initial);

    HeapState expected = {0,0,0,0,0};
    byte *p[BLOCK_MAX_COUNT];

    /* Calloc 3 blocks, then fill them with data */
    for (int i=0; i<3; ++i) {
        p[i] = calloc_byte(BLOCK_SIZES[i]);
        add_expected_block(expected, BLOCK_SIZES[i]);
    }
    assert_state(initial, expected);
    assert(check_data(heap, ZEROS, 3, BLOCK_SIZES));
    fill_data(DATA, p, 3, BLOCK_SIZES);
    assert(check_data(heap, DATA, 3, BLOCK_SIZES));

    /* Free second block */
    sfree(p[1]);
    free_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);

    /* Re-allocate using calloc */
    p[1] = calloc_byte(BLOCK_SIZES[1]);
    reuse_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);
    DATA[1][0] = 0;
    DATA[1][1] = 0;
    assert(check_data(heap, DATA, 3, BLOCK_SIZES));
}

void test_realloc() {
    const size_t BLOCK_SIZES[BLOCK_MAX_COUNT] = {3,4,5, LAST};
    byte DATA[BLOCK_MAX_COUNT][BLOCK_MAX_SIZE] =
            {{1,2,3}, {1,2,3,4}, {11,12,13,14,15}, {static_cast<byte>(LAST)}};
    byte *heap = static_cast<byte*>(sbrk(0));
    HeapState initial;
    get_initial_state(initial);

    HeapState expected = {0,0,0,0,0};
    byte *p1, *p2, *p3;

    /* Allocate first */
    p1 = realloc_byte(NULL, BLOCK_SIZES[0]);
    add_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    fill_data(DATA, &p1, 1, BLOCK_SIZES);
    assert(check_data(heap, DATA, 1, BLOCK_SIZES));

    /* Reallocate to same size (do nothing) */
    p1 = realloc_byte(p1, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    assert(check_data(heap, DATA, 1, BLOCK_SIZES));

    /* Reallocate to bigger */
    p2 = realloc_byte(p1, BLOCK_SIZES[1]);
    add_expected_block(expected, BLOCK_SIZES[1]);
    free_expected_block(expected, BLOCK_SIZES[0]);
    DATA[0][0] = GARBAGE; DATA[0][1] = GARBAGE; DATA[0][2] = GARBAGE;
    p2[3] = DATA[1][3];
    assert_state(initial, expected);
    assert(check_data(heap, DATA, 2, BLOCK_SIZES));

    /* Reallocate to smaller (do nothing) */
    p2 = realloc_byte(p2, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    DATA[1][3] = GARBAGE;
    assert(check_data(heap, DATA, 2, BLOCK_SIZES));

    /* New allocation (too big to reuse previous freed allocation) */
    p3 = realloc_byte(NULL, BLOCK_SIZES[2]);
    add_expected_block(expected, BLOCK_SIZES[2]);
    assert_state(initial, expected);
    for (int i=0; i<BLOCK_SIZES[2]; ++i)
        p3[i] = DATA[2][i];
    assert(check_data(heap, DATA, 3, BLOCK_SIZES));

    /* Free data that was already freed by realloc (do nothing) */
    sfree(p1);
    assert_state(initial, expected);
    assert(check_data(heap, DATA, 3, BLOCK_SIZES));

    /* New allocation - reuse freed area */
    p1 = realloc_byte(NULL, 1);
    reuse_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    assert(check_data(heap, DATA, 1, BLOCK_SIZES));

    /* Free everything */
    sfree(p1);
    free_expected_block(expected, BLOCK_SIZES[0]);
    assert_state(initial, expected);
    sfree(p2);
    free_expected_block(expected, BLOCK_SIZES[1]);
    assert_state(initial, expected);
    sfree(p3);
    free_expected_block(expected, BLOCK_SIZES[2]);
    assert_state(initial, expected);
}

void test_failures() {
    byte *heap = static_cast<byte*>(sbrk(0));
    HeapState initial;
    get_initial_state(initial);
    byte *p;
    HeapState expected = {0,0,0,0,0};

    assert(!smalloc(0));
    assert(!smalloc(long(1e8 + 10)));
    assert(!scalloc(sizeof(byte), 0));
    assert(!scalloc(sizeof(byte), (long(1e8 + 10))));
    assert_state(initial, expected);

    assert(p = malloc_byte(1));
    add_expected_block(expected, 1);
    assert_state(initial, expected);
    *p = 10;
    assert(!srealloc(p, 0));
    assert(!srealloc(p, (long(1e8 + 10))));
    assert_state(initial, expected);
    assert(*p == 10);

    sfree(NULL);
    assert_state(initial, expected);
    sfree(p);
    free_expected_block(expected, 1);
    assert_state(initial, expected);
    sfree(NULL);
    assert_state(initial, expected);
}

/*******************************************************************************
 *  MAIN
 ******************************************************************************/

static void callTestFunction(void (*func)()) {
    if (!fork()) {  // test as son, to get a clear heap
        func();
        exit(0);
    } else {		// father waits for son before continuing to next test
        int exit_status = 0;
        wait(&exit_status);
        if (exit_status)
            std::cout << "*** FAILED with exit status " << exit_status << std::endl;
    }
}

int main()
{
    std::cout << "test_malloc_then_free" << std::endl;
    callTestFunction(test_malloc_then_free);
    std::cout << "test_reuse_after_free" << std::endl;
    callTestFunction(test_reuse_after_free);
    std::cout << "test_calloc" << std::endl;
    callTestFunction(test_calloc);
    std::cout << "test_realloc" << std::endl;
    callTestFunction(test_realloc);
    std::cout << "test_failures" << std::endl;
    callTestFunction(test_failures);
    return 0;
}
