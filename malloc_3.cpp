#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sys/mman.h>


#define MIN_SIZE 0
#define MAX_SIZE 100000000

using std::memset;
using std::memcpy;

class MetaData {
     
    bool _is_free;
    size_t _original_size;
    size_t _requested_size;
    void* _allocation_addr;
    MetaData* _next, _prev;
    MetaData* _next_free, _prev_free;

public:

    void set_is_free(bool free) {_is_free = free;}
    void set_original_size(size_t size) {_original_size = size;}
    void set_requested_size(size_t size) {_requested_size = size;}
    void set_allocation_addr(void *addr) {_allocation_addr = addr;}
    void set_next(MetaData* next) {_next = next;}
    void set_prev(MetaData* prev) {_prev = prev;}
    bool is_free() {return _is_free;}
    size_t get_original_size() {return _original_size;}
    size_t get_requested_size() {return _requested_size;}
    void* get_allocation_addr() {return _allocation_addr;}
    MetaData* get_next() {return _next;}
    MetaData* get_prev() {return _prev;}
};

MetaData* histogram[128] = {nullptr};

MetaData* allocHistory = NULL;

void* smalloc(size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return NULL;

    // First, search for free space in our global list
    if (allocHistory) {
        for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
            if (iter->get_original_size() >= size && iter->is_free()) {
                iter->set_is_free(false);
                iter->set_requested_size(size);
                return iter->get_allocation_addr();
            }
        }
    }

    // If not enough free space was found, allocate new space
    MetaData* metaData = (MetaData*)sbrk(sizeof(MetaData));
    if (metaData == (void*)(-1)) {
        return NULL;
    }

    void* allocation_addr = sbrk(size);
    if (allocation_addr == (void*)(-1)) {
        sbrk(-sizeof(MetaData));
        return NULL;
    }

    metaData->set_is_free(false);
    metaData->set_original_size(size);
    metaData->set_requested_size(size);
    metaData->set_allocation_addr(allocation_addr);
    metaData->set_next(NULL);
    metaData->set_prev(NULL);

    // Add the allocated meta-data to the allocation history list
    // If this it the first allocation:
    if (!allocHistory) {
        allocHistory = metaData;
    }
    
    // Else if there are others, we need to find the last allocation made
    else {
        MetaData* iter = allocHistory;
        while (iter->get_next()) {
            iter = iter->get_next();
        }

        metaData->set_prev(iter);
        iter->set_next(metaData);
    }

    return allocation_addr;
}

void* scalloc(size_t num, size_t size) {
    // First, we allocate memory using smalloc
    void* alloc_addr = smalloc(num * size);

    if (!alloc_addr) 
        return NULL;

    // Then, if we succeed in allocating, we reset the block
    else 
        return memset(alloc_addr, 0, num * size);
    
}

void sfree(void* p) {
    if (!p) 
        return;

    // Search for p in our global list
    for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
        if (iter->get_allocation_addr() == p) {
            // If 'p' has already been freed, don't do anything
            if (iter->is_free()) {
                return;
            }
            // Else, free the allocated block
            else {
                iter->set_is_free(true);
                return;
            }
        }
    }
}

void* srealloc(void* oldp, size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return NULL;

    // If oldp is NULL, allocate memory for 'size' bytes and return a poiter to it
    if (!oldp) {
        void* allocation_addr = smalloc(size);
        if (!allocation_addr) 
            return NULL;
        
        else 
            return allocation_addr; 
    }

    // If not, search for it assuming oldp is a pointer to a previously allocated block
    MetaData* metaData = NULL;
    for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
        if (iter->get_allocation_addr() == oldp) {
            metaData = iter;
            break;
        }
    }

    // Check if allocation has enough memory to support the new block size
    if (metaData->get_original_size() >= size) {
        metaData->set_requested_size(size);
        return oldp;
    }
    
    // If not, allocate memory using smalloc
    else {
        void* allocation_addr = smalloc(size);
        if (!allocation_addr) 
            return NULL;
        
        // Copy the data, then free the old memory using sfree
        memcpy(allocation_addr, oldp, size);
        sfree(oldp);
        return allocation_addr;
    }
}


size_t _num_free_blocks() {
    size_t free_blocks = 0;
    if (allocHistory) {
        for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
            if (iter->is_free()) {
                free_blocks++;
            }
        }
    }
    return free_blocks;
}

size_t _num_free_bytes() {
    size_t free_bytes = 0;
    if (allocHistory) {
        for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
            if (iter->is_free()) {
                free_bytes += iter->get_original_size();
            }
        }
    }
    return free_bytes;
}

size_t _num_allocated_blocks() {
    size_t allocated_blocks = 0;
    if (allocHistory) {
        for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
            allocated_blocks++;
        }
    }
    return allocated_blocks;
}

size_t _num_allocated_bytes() {
    size_t allocated_bytes = 0;
    if (allocHistory) {
        for (MetaData* iter = allocHistory; iter != nullptr; iter = iter->get_next()) {
            allocated_bytes += iter->get_original_size();
        }
    }
    return allocated_bytes;
}

size_t _size_meta_data() {
    return sizeof(MetaData);
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * _size_meta_data();
}