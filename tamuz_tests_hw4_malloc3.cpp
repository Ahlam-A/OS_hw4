/*
g++ malloc_2.cpp -fPIC -shared -o libmal.so
g++ tamuz_tests_hw4.cpp -L ./ -lmal

export LD_LIBRARY_PATH="/root/hw4/"
 */

#include <unistd.h>
#include <assert.h>
#include <cstdlib>
#include <sys/wait.h>
#include <iostream>

#define assert_state(_initial, _expected)\
	do {\
		assert(_num_allocated_blocks() - _initial.allocated_blocks == _expected.allocated_blocks); \
		assert(_num_allocated_bytes() - _initial.allocated_bytes == _expected.allocated_bytes); \
		assert(_num_free_blocks() - _initial.free_blocks == _expected.free_blocks); \
		assert(_num_free_bytes() - _initial.free_bytes == _expected.free_bytes); \
		assert(_num_meta_data_bytes() - _initial.meta_data_bytes == _expected.meta_data_bytes); \
	} while (0)

typedef unsigned char byte;
const int BLOCK_MAX_COUNT=10, BLOCK_MAX_SIZE=10, LAST=9999;
const byte GARBAGE=255, META=254;

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

size_t round(size_t size) {
	return ((size-1)/4+1)*4;
}

/* Tells the test-function that we're expecting a new block to be allocated on
 * the heap. Then the test-function has to check if this actually happenned.*/
void add_expected_block(HeapState &state, size_t size) {
	state.allocated_blocks++;
	state.allocated_bytes += round(size);
	state.meta_data_bytes += _size_meta_data();
}

/* Tells the test-function that a block was supposed to be
 * freed. Then the test-function has to check if this actually happenned.*/
void free_expected_block(HeapState &state, size_t size) {
	state.free_blocks++;
	state.free_bytes += round(size);
}

/* Tells the test-function that a previously-freed block should be "un-freed"
 * (reused for a new malloc). Then the test-function has to check if this
 * actually happenned.*/
void reuse_expected_block(HeapState &state, size_t size) {
	state.free_blocks--;
	state.free_bytes -= round(size);
}

/* Tells the tests that some two blocks were supposed to be merged now. */
void merge_expected_block(HeapState &state) {
	state.free_blocks--;
	state.free_bytes += _size_meta_data();
	state.allocated_blocks--;
	state.allocated_bytes += _size_meta_data();
	state.meta_data_bytes -= _size_meta_data();
}

byte* malloc_byte(int count) {
	return static_cast<byte*>(malloc(sizeof(byte)*count));
}

byte* calloc_byte(int count) {
	return static_cast<byte*>(calloc(count, sizeof(byte)));
}

byte* realloc_byte(void *oldp, size_t size) {
	return static_cast<byte*>(realloc(oldp, size));
}

void copy (byte *from, byte *to, int count) {
	for (int i=0; i<count; ++i) to[i] = from[i];
}

void range(byte *target, int from, int count) {
	for (int i=0; i<count; ++i) target[i] = from+i;
}

bool check_heap_straight(byte *heap, byte *expected, int count) {
	heap += _size_meta_data();
	for (int i=0; i<count; ++i) {
		if (expected[i] == GARBAGE)
			++heap;
		else if (expected[i] == META)
			heap += _size_meta_data();
		else {
			if (expected[i] != *heap)
				return false;
			++heap;
		}
	}
	return true;
}

/*******************************************************************************
 *  TESTS
 ******************************************************************************/

void test_basic_malloc_and_free() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte DATA[] = {1,GARBAGE,GARBAGE,GARBAGE,META, 2,3,4,5,META,
				6,7,8,9,10,GARBAGE,GARBAGE,GARBAGE};

	byte *p1,*p2,*p3;
	p1 = malloc_byte(1);
	add_expected_block(expected, 1);
	assert_state(initial, expected);
	p2 = malloc_byte(4);
	add_expected_block(expected, 4);
	assert_state(initial, expected);
	p3 = malloc_byte(5);
	add_expected_block(expected, 5);
	assert_state(initial, expected);

	p1[0] = 1;
	p2[0]=2;p2[1]=3;p2[2]=4;p2[3]=5;
	p3[0]=6;p3[1]=7;p3[2]=8;p3[3]=9;p3[4]=10;
	assert_state(initial, expected);
	assert(check_heap_straight(heap, DATA, 18));

	free(p1);
	free_expected_block(expected, 1);
	assert_state(initial, expected);
	DATA[0] = GARBAGE;
	assert(check_heap_straight(heap, DATA, 18));

	free(p2);
	free_expected_block(expected, 4);
	merge_expected_block(expected);
	assert_state(initial, expected);
	DATA[5]=GARBAGE; DATA[6]=GARBAGE; DATA[7]=GARBAGE; DATA[8]=GARBAGE;
	assert(check_heap_straight(heap, DATA, 18));

	free(p3);
	free_expected_block(expected, 5);
	merge_expected_block(expected);
	assert_state(initial, expected);
}

void test_malloc_wilderness() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte D8A[8], D8B[8], D4[4], D12[12], HEAP[21];
	range(D8A, 0, 8); range(D8B, 10, 8); range(D4, 20, 4); range(D12, 100, 12);
	copy(D12, HEAP, 12); HEAP[12] = META; copy(D8A, HEAP+13, 8);

	byte *p1, *p2;

	/* All following allocations should take place at the beginning of the heap */
	p1 = malloc_byte(8);
	add_expected_block(expected, 8);
	assert_state(initial, expected);
	copy(D8A, p1, 8);
	assert(check_heap_straight(heap, D8A, 8));

	free(p1);
	free_expected_block(expected, 8);
	assert_state(initial, expected);

	p1 = malloc_byte(8);
	reuse_expected_block(expected, 8);
	assert_state(initial, expected);
	copy(D8B, p1, 8);
	assert(check_heap_straight(heap, D8B, 8));

	free(p1);
	free_expected_block(expected, 8);
	assert_state(initial, expected);

	p1 = malloc_byte(4);
	reuse_expected_block(expected, 8);
	assert_state(initial, expected);
	copy(D4, p1, 4);
	assert(check_heap_straight(heap, D4, 4));

	free(p1);
	free_expected_block(expected, 8);
	assert_state(initial, expected);

	p1 = malloc_byte(12);
	expected.allocated_bytes = 12;
	expected.free_bytes = 0;
	expected.free_blocks = 0;
	assert_state(initial, expected);
	copy(D12, p1, 12);
	assert(check_heap_straight(heap, D12, 12));

	/* Now allocate a second block */
	p2 = malloc_byte(4);
	add_expected_block(expected, 4);
	assert_state(initial, expected);
	free(p2);
	free_expected_block(expected, 4);
	assert_state(initial, expected);

	p2 = malloc_byte(8);
	expected.free_blocks = 0;
	expected.free_bytes = 0;
	expected.allocated_bytes += 4;
	assert_state(initial, expected);
	copy(D8A, p2, 8);
	assert(check_heap_straight(heap, HEAP, 21));

	/* Free */
	free(p2);
	free_expected_block(expected, 8);
	assert_state(initial, expected);
	free(p1);
	free_expected_block(expected, 12);
	merge_expected_block(expected);
	assert_state(initial, expected);
}

void test_calloc() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte ZEROS[6] = { }, DATA[] = {0,1,2,3,4,5};

	byte *p, *p2;
	p = calloc_byte(3);
	add_expected_block(expected, 3);
	assert_state(initial, expected);
	assert(check_heap_straight(heap, ZEROS, 3));

	p[0]=0; p[1]=1; p[2]=2;
	assert(check_heap_straight(heap, DATA, 3));
	free(p);
	free_expected_block(expected, 3);
	assert_state(initial, expected);

	p = calloc_byte(6);
	expected.allocated_bytes = 8;
	expected.free_bytes = 0;
	expected.free_blocks = 0;
	assert_state(initial, expected);
	assert(check_heap_straight(heap, ZEROS, 6));
	copy(DATA, p, 6);
	assert(check_heap_straight(heap, DATA, 6));

	free(p);
	free_expected_block(expected, 6);
	assert_state(initial, expected);
}

void test_malloc_split_and_merge() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);
	const size_t METASIZE = _size_meta_data();

	byte DATA[220];
	range(DATA, 0, 133 + 2*METASIZE);
	int i = 132 + 2*METASIZE;
	DATA[++i] = GARBAGE; DATA[++i] = GARBAGE; DATA[++i] = GARBAGE; DATA[++i] = META;
	DATA[++i] = 0; DATA[++i] = 1; DATA[++i] = 2; DATA[++i] = GARBAGE; DATA[++i] = META;
	DATA[++i] = 0; DATA[++i] = 1; DATA[++i] = 2; DATA[++i] = GARBAGE;

	byte *p1, *p2, *p3, *p1b;

	/* make 3 allocations */
	p1 = malloc_byte(133 + 2*METASIZE);
	add_expected_block(expected, 133 + 2*METASIZE);
	assert_state(initial, expected);
	p2 = malloc_byte(3);
	add_expected_block(expected, 3);
	assert_state(initial, expected);
	p3 = malloc_byte(3);
	add_expected_block(expected, 3);
	assert_state(initial, expected);

	copy(DATA, p1, 133 + 2*METASIZE);
	p2[0]=0; p2[1]=1; p2[2]=2;
	p3[0]=0; p3[1]=1; p3[2]=2;
	assert(check_heap_straight(heap, DATA, 136+2*METASIZE+8+2));

	/* release p1. then reuse and split it, then reuse and split again. */
	free(p1);
	free_expected_block(expected, 133+2*METASIZE);
	assert_state(initial, expected);
	p1b = malloc_byte(2);
	expected.allocated_blocks++;
	expected.allocated_bytes -= METASIZE;
	expected.free_bytes -= (4 + METASIZE);
	expected.meta_data_bytes += METASIZE;
	assert_state(initial, expected);
	p1 = malloc_byte(4);
	expected.allocated_blocks++;
	expected.allocated_bytes -= METASIZE;
	expected.free_bytes -= (4 + METASIZE);
	expected.meta_data_bytes += METASIZE;
	assert_state(initial, expected);
	p1b[0]=201;p1b[1]=201;
	p1[0]=200;p1[1]=200;p1[2]=200;p1[3]=200;

	assert(heap[0+METASIZE]==201);
	assert(heap[1+METASIZE]==201);
	assert(heap[4+2*METASIZE]==200);
	assert(heap[5+2*METASIZE]==200);
	assert(heap[6+2*METASIZE]==200);
	assert(heap[7+2*METASIZE]==200);
	assert(p2[0]==0);
	assert(p2[1]==1);
	assert(p2[2]==2);
	assert(p3[0]==0);
	assert(p3[1]==1);
	assert(p3[2]==2);

	/* release p3 then p2. assert that p2 is merged with both adjucent blocks. */
	free(p3);
	free_expected_block(expected, 3);
	assert_state(initial, expected);
	free(p2);
	free_expected_block(expected, 3);
	merge_expected_block(expected);
	merge_expected_block(expected);
	assert_state(initial, expected);

	free(p1b);
	free(p1);
	free_expected_block(expected, 4);
	free_expected_block(expected, 2);
	merge_expected_block(expected);
	merge_expected_block(expected);
	assert_state(initial, expected);
}

void test_realloc_just_contract() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte *p1, *p2, *p;
	p1 = realloc_byte(NULL, 4);
	add_expected_block(expected, 4);
	assert_state(initial, expected);
	p2 = realloc_byte(NULL, 7);
	add_expected_block(expected, 7);
	assert_state(initial, expected);

	free(p1);
	free_expected_block(expected, 4);
	assert_state(initial, expected);
	p = realloc_byte(p2, 3);
	assert_state(initial, expected);
	assert(p==p2);

	free(p);
	free_expected_block(expected, 7);
	merge_expected_block(expected);
	assert_state(initial, expected);
}

void test_realloc_split_then_merge() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte *p1, *p1b, *p2, *p3;
	p1 = realloc_byte(NULL, 200);
	add_expected_block(expected, 200);
	assert_state(initial, expected);
	p3 = realloc_byte(NULL, 200);
	add_expected_block(expected, 200);
	assert_state(initial, expected);

	free(p3);
	free_expected_block(expected, 200);
	assert_state(initial, expected);
	p1b = realloc_byte(p1, 4);  // shrink p1, split it, and the remainder is merged with p3
	assert(p1b==p1);
	expected.free_bytes += 200 - 4;
	assert_state(initial, expected);

	p2 = realloc_byte(NULL, 1);
	expected.allocated_bytes -= _size_meta_data();
	expected.allocated_blocks++;
	expected.meta_data_bytes += _size_meta_data();
	expected.free_bytes -= (4 + _size_meta_data());
	assert_state(initial, expected);
	assert(p2 == p1b+4+_size_meta_data());
}

void test_realloc_merge_then_split() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte *p1, *p2, *p;
	p1 = realloc_byte(NULL, 4);
	p2 = realloc_byte(NULL, 200);
	add_expected_block(expected, 4);
	add_expected_block(expected, 200);
	assert_state(initial, expected);

	free(p2);
	free_expected_block(expected, 200);
	assert_state(initial, expected);

	p = realloc_byte(p1, 7);
	assert(p==p1);
	expected.free_bytes -= 4;
	assert_state(initial, expected);
}

void test_realloc_on_wilderness() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte *p1, *p2;
	p1 = realloc_byte(NULL, 4);
	add_expected_block(expected, 4);
	assert_state(initial, expected);
	p2 = realloc_byte(p1, 5);
	expected.allocated_bytes += 4;
	assert_state(initial, expected);
	assert(p1==p2);

	free(p2);
	free_expected_block(expected, 8);
	assert_state(initial, expected);
}

void test_realloc_copy_to_wilderness() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);

	byte *p1,*p2,*p3,*p4;
	p1 = realloc_byte(NULL, 4);
	p2 = realloc_byte(NULL, 4);
	p3 = realloc_byte(NULL, 4);
	add_expected_block(expected, 4);
	add_expected_block(expected, 4);
	add_expected_block(expected, 4);
	assert_state(initial, expected);
	free(p3);
	free_expected_block(expected, 4);

	p4 = realloc_byte(p1, 8);
	assert(p4 == p3);
	expected.allocated_bytes += 4;
	assert_state(initial, expected);

	free(p1);  //do nothing
	assert_state(initial, expected);
	free(p2);  //merged to p1
	free_expected_block(expected, 4);
	merge_expected_block(expected);
	assert_state(initial, expected);
	free(p3);  //merged to previous;
	free_expected_block(expected, 8);
	merge_expected_block(expected);
	assert_state(initial, expected);
}

void test_failures() {
	byte *heap = static_cast<byte*>(sbrk(0));
	HeapState initial, expected = {0,0,0,0,0};
	get_initial_state(initial);
	const int BIG = int(1e8)+10;

	byte *p, *p2;
	p = malloc_byte(0);
	assert(p==NULL);
	assert_state(initial, expected);
	p = malloc_byte(BIG);
	assert(p==NULL);
	assert_state(initial, expected);
	p = calloc_byte(0);
	assert(p==NULL);
	assert_state(initial, expected);
	p = calloc_byte(BIG);
	assert(p==NULL);
	assert_state(initial, expected);
	p = realloc_byte(NULL, 0);
	assert(p==NULL);
	assert_state(initial, expected);
	p = realloc_byte(NULL, BIG);
	assert(p==NULL);
	assert_state(initial, expected);
	free(NULL);
	assert_state(initial, expected);

	p = malloc_byte(1);
	add_expected_block(expected, 1);
	assert_state(initial, expected);
	p[0] = 1;
	assert(*(heap + _size_meta_data()) == 1);
	p2 = realloc_byte(p, 0);
	assert(p2 == NULL);
	assert_state(initial, expected);
	assert(*(heap + _size_meta_data()) == 1);
	p2 = realloc_byte(p, BIG);
	assert(p2 == NULL);
	assert_state(initial, expected);
	assert(*(heap + _size_meta_data()) == 1);

	free(p);
	free_expected_block(expected, 1);
	assert_state(initial, expected);
	free(p);
	assert_state(initial, expected);
}

/*******************************************************************************
 *  MAIN
 ******************************************************************************/

void align() {
	void *heap = sbrk(0);
	int inc = (int)(heap) % 4;
	void *result;
	if (inc) {
		result = sbrk(inc);
		if (result == (void*)-1) {
			std::cout << "can't test, failed to align heap base" << std::endl;
			exit(1);
		}
	}
}

static void callTestFunction(void (*func)()) {
	if (!fork()) {  // test as son, to get a clear heap
		func();
		exit(0);
	} else {		// father waits for son before continuing to next test
		int exit_status = 0;
		wait(&exit_status);
		if (!exit_status)
			return;
		if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status))
			std::cout << "Exit status ERROR " << WEXITSTATUS(exit_status) << ". ";
		if (WIFSIGNALED(exit_status))
			std::cout << "Error signal " << WTERMSIG(exit_status);
		std::cout << std::endl;
	}
}

int main()
{
	align();
	std::cout << "test_basic_malloc_and_free" << std::endl;
	callTestFunction(test_basic_malloc_and_free);
	std::cout << "test_malloc_wilderness" << std::endl;
	callTestFunction(test_malloc_wilderness);
	std::cout << "test_calloc" << std::endl;
	callTestFunction(test_calloc);
	std::cout << "test_malloc_split_and_merge" << std::endl;
	callTestFunction(test_malloc_split_and_merge);
	std::cout << "test_realloc_just_contract" << std::endl;
	callTestFunction(test_realloc_just_contract);
	std::cout << "test_realloc_split_then_merge" << std::endl;
	callTestFunction(test_realloc_split_then_merge);
	std::cout << "test_realloc_merge_then_split" << std::endl;
	callTestFunction(test_realloc_merge_then_split);
	std::cout << "test_realloc_on_wilderness" << std::endl;
	callTestFunction(test_realloc_on_wilderness);
	std::cout << "test_realloc_copy_to_wilderness" << std::endl;
	callTestFunction(test_realloc_copy_to_wilderness);
	std::cout << "test_failures" << std::endl;
	callTestFunction(test_failures);
	std::cout << "Done." << std::endl;
	return 0;
}
