#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include "os_malloc.h"

#define MIN_SIZE 0
#define MAX_SIZE 100000000
#define SPLIT_MIN 128
#define LARGE_ALLOC 128 * 1024

using std::memset;
using std::memcpy;

typedef struct MetaData_t { 
    void* alloc_address;
    size_t alloc_size;
    bool is_free;
    struct MetaData_t* next;
    struct MetaData_t* prev;
} *MetaData;

MetaData memory_list = nullptr;

/* =================== Declarations ==================== */

size_t _size_meta_data();
void* map_smalloc(size_t size);
void* map_srealloc(void* oldp,size_t size);

/* ================= Helper Functions ================== */

void split(MetaData metaData, size_t requested_size) {
    size_t md_size = _size_meta_data();
    size_t split_size = metaData->alloc_size - requested_size - md_size;

    if (split_size < SPLIT_MIN) {
        return;
    }

    MetaData newMataData = (MetaData)((size_t)metaData + md_size + requested_size);
    newMataData->alloc_address = (void*)((size_t)newMataData + md_size);
    newMataData->alloc_size = split_size;
    newMataData->is_free = true;

    newMataData->next = metaData->next;
    if (newMataData->next) {
        newMataData->next->prev = newMataData;
    }

    newMataData->prev = metaData;
    metaData->next = newMataData;
    metaData->alloc_size = requested_size;
}

void merge(MetaData metaData) {
    size_t md_size = _size_meta_data();

    // Merge with next block if it's free
    MetaData next_block = metaData->next;
    if (next_block != nullptr && next_block->is_free) {
        metaData->alloc_size += next_block->alloc_size + md_size;
        metaData->next = next_block->next;
        if (next_block->next != nullptr) {
            next_block->next->prev = metaData;
        }
    }

    // Merge with previous block if it's free
    MetaData prev_block = metaData->prev;
    if (prev_block != nullptr && prev_block->is_free) {
        prev_block->alloc_size += metaData->alloc_size + md_size;
        prev_block->next = metaData->next;

        if (metaData->next != nullptr) {
            metaData->next->prev = prev_block;
        }
    }
}

/* ================ Upgraded Functions ================= */

void* smalloc(size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return nullptr;

    // First, search for free space in memory list
    if (memory_list) {
        for (MetaData md = memory_list; md != nullptr; md = md->next) {
            if (md->is_free && md->alloc_size >= size) {
                md->is_free = false;
                split(md, size);
                return md->alloc_address;
            }        
            
            // Check if wilderness chunck is free
            if (md->next == nullptr && md->is_free) {
                void* enlarge = sbrk(size - md->alloc_size);
                if (enlarge == (void*)(-1)) {
                    sbrk(md->alloc_size - size);
                    return nullptr;
                }
                md->alloc_size = size;
                md->is_free = false;
                return md->alloc_address;
            }
        }
    }

    // If not enough free space was found, allocate new memory
    MetaData metaData = (MetaData)sbrk(sizeof(*metaData));
    if (metaData == (void*)(-1)) {
        return nullptr;
    }

    void* alloc_addr = sbrk(size);
    if (alloc_addr == (void*)(-1)) {
        sbrk(-sizeof(*metaData));
        return nullptr;
    }

    metaData->alloc_address = alloc_addr;
    metaData->alloc_size = size;
    metaData->is_free = false;
    metaData->next = metaData->prev = nullptr;

    // Add the allocated meta-data to memory list
    if (!memory_list) {
        memory_list = metaData;
    }

    else {
        MetaData md = memory_list;
        while (md->next) {
            md = md->next;
        }
        metaData->prev = md;
        md->next = metaData;
    }

    return alloc_addr;
}

void* scalloc(size_t num, size_t size) {
    // First, allocate memory using smalloc
    void* alloc_addr = smalloc(num * size);

    if (!alloc_addr) 
        return nullptr;

    // Then, if allocation succeeds, reset the block
    else 
        return memset(alloc_addr, 0, num * size);
    
}

void sfree(void* p) {
    if (!p) 
        return;

    // Search for p in memory list
    for (MetaData md = memory_list; md != nullptr; md = md->next) {
        if (md->alloc_address == p) {
            // If 'p' has already been freed, don't do anything
            if (md->is_free) {
                return;
            }
            // Else, free the allocated block
            else {
                md->is_free = true;
                merge(md);
                return;
            }
        }
    }
}

void* srealloc(void* oldp, size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return nullptr;

    // If oldp is null, allocate memory for 'size' bytes and return a pointer to it
    if (!oldp) {
        void* alloc_addr = smalloc(size);
        if (!alloc_addr) 
            return nullptr;
        else 
            return alloc_addr; 
    }

    // If not, search for it assuming oldp is a pointer to a previously allocated block
    MetaData metaData = nullptr;
    for (MetaData md = memory_list; md != nullptr; md = md->next) {
        if (md->alloc_address == oldp) {
            metaData = md;
            break;
        }
    }

    // Check if allocation has enough memory to support the new block size
    if (metaData->alloc_size >= size) {
        return oldp;
    }
    
    // If not, allocate memory using smalloc
    else {
        void* alloc_addr = smalloc(size);
        if (!alloc_addr) 
            return nullptr;
        
        // Copy the data, then free the old memory using sfree
        memcpy(alloc_addr, oldp, size);
        sfree(oldp);
        return alloc_addr;
    }
}


size_t _num_free_blocks() {
    size_t free_blocks = 0;
    if (memory_list) {
        for (MetaData md = memory_list; md != nullptr; md = md->next) {
            if (md->is_free) {
                free_blocks++;
            }
        }
    }
    return free_blocks;
}

size_t _num_free_bytes() {
    size_t free_bytes = 0;
    if (memory_list) {
        for (MetaData md = memory_list; md != nullptr; md = md->next) {
            if (md->is_free) {
                free_bytes += md->alloc_size;
            }
        }
    }
    return free_bytes;
}

size_t _num_allocated_blocks() {
    size_t allocated_blocks = 0;
    if (memory_list) {
        for (MetaData md = memory_list; md != nullptr; md = md->next) {
            allocated_blocks++;
        }
    }
    return allocated_blocks;
}

size_t _num_allocated_bytes() {
    size_t allocated_bytes = 0;
    if (memory_list) {
        for (MetaData md = memory_list; md != nullptr; md = md->next) {
            allocated_bytes += md->alloc_size;
        }
    }
    return allocated_bytes;
}

size_t _size_meta_data() {
    return sizeof(struct MetaData_t);
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * _size_meta_data();
}
