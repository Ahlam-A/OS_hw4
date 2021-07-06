#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sys/mman.h>

#define MIN_SIZE 0
#define MAX_SIZE 100000000

using std::memset;
using std::memmove;

struct MetaData { 
    size_t size;
    bool is_free;
    MetaData* next;
    MetaData* prev;
};

MetaData* memory_list = nullptr;

void* smalloc(size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return nullptr;

    // First, search for free space in memory list
    if (memory_list) {
        for (MetaData* md = memory_list; md != nullptr; md = md->next) {
            if (md->is_free && md->size >= size) {
                md->is_free = false;
                return md + 1;
            }
        }
    }

    // If not enough free space was found, allocate new memory
    MetaData* metaData = (MetaData*)sbrk(size + sizeof(MetaData));
    if (metaData == (void*)(-1)) {
        return nullptr;
    }

    metaData->size = size;
    metaData->is_free = false;
    metaData->next = metaData->prev = nullptr;

    // Add the allocated meta-data to memory list
    if (!memory_list) {
        memory_list = metaData;
    }
    else {
        MetaData* md = memory_list;
        while (md->next) {
            md = md->next;
        }
        metaData->prev = md;
        md->next = metaData;
    }

    return metaData + 1;
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
    if (!p) return;

    MetaData* md = (MetaData*)p - 1;
    
    if (md->is_free) return;
    else md->is_free = true;
}

void* srealloc(void* oldp, size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return nullptr;

    // If oldp is null, allocate memory for 'size' bytes and return a pointer to it
    if (oldp == nullptr) return smalloc(size);

    // If not, assume oldp is points to a previously allocated block
    MetaData* old_md = (MetaData*) oldp - 1;

    // Check if allocation has enough memory to support the new block size
    if (old_md->size >= size) {
        old_md->is_free = false;
        return oldp;
    }
    
    // If not, allocate memory using smalloc
    else {
        void* alloc_addr = smalloc(size);
        if (!alloc_addr) 
            return nullptr;
        
        // Copy the data, then free the old memory using sfree
        memmove(alloc_addr, oldp, old_md->size);
        sfree(oldp);
        return alloc_addr;
    }
}


size_t _num_free_blocks() {
    size_t free_blocks = 0;
    if (memory_list) {
        for (MetaData* md = memory_list; md != nullptr; md = md->next) {
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
        for (MetaData* md = memory_list; md != nullptr; md = md->next) {
            if (md->is_free) {
                free_bytes += md->size;
            }
        }
    }
    return free_bytes;
}

size_t _num_allocated_blocks() {
    size_t allocated_blocks = 0;
    if (memory_list) {
        for (MetaData* md = memory_list; md != nullptr; md = md->next) {
            allocated_blocks++;
        }
    }
    return allocated_blocks;
}

size_t _num_allocated_bytes() {
    size_t allocated_bytes = 0;
    if (memory_list) {
        for (MetaData* md = memory_list; md != nullptr; md = md->next) {
            allocated_bytes += md->size;
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
