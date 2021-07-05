#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include "os_malloc.h"

#define MIN_SIZE 0
#define MAX_SIZE 100000000
#define SPLIT_MIN 128
#define KB 1024
#define LARGE_ALLOC 128 * KB
#define MD_SIZE _size_meta_data()

using std::memset;
using std::memmove;

typedef struct MetaData_t { 
    void* alloc_address;
    size_t alloc_size;
    bool is_free;
    struct MetaData_t* next;
    struct MetaData_t* prev;
    struct MetaData_t* next_free;
    struct MetaData_t* prev_free;
} *MetaData;

MetaData memory_list = nullptr;
MetaData mmap_list = nullptr;
MetaData histogram[128];

/* ================= Helper Functions ================== */

int histIndex (size_t size) {
    int i = 0;
    while (i < 128) {
        if (i*KB <= size && size < (1+i)*KB){
            return i ;
        }
        i++;
    }
    return i-1;
}

void histRemove(Metadata md){
    if (md->next_free != nullptr) {
        md->next_free->prev_free = md->prev_free;
    }
    if (md->prev_free != nullptr) {
        md->prev_free->next_free = md->next_free;
    } else {
        int index = histIndex(md->size);
        histogram[index] = md->next_free;
    }
    md->next_free = md->prev_free = nullptr;
}

void histInsert (Metadata md) {
    int index = histIndex(md->size);
    MetaData slot = histogram[index];
    
    if (slot == nullptr) {
        histogram[index] = md;
        md->next_free = md->prev_free = nullptr;
    }
    else {
        bool is_inserted = false;
        MetaData current = slot;
        while (slot != nullptr) {
            if (slot->size >= md->size) {
                if (slot->prev_free == nullptr) {
                    histogram[index] = md;
                    slot->prev_free = md;
                    md->next_free = slot;
                    md->prev_free = nullptr;
                    is_inserted = true ;
                    break;
                }
                else {
                    slot->prev_free->next_free = md;
                    md->prev_free = slot->prev_free;
                    slot->prev_free = md;
                    md->next_free = slot ;
                    is_inserted = true;
                    break;
                }
            }
            current = slot ;
            slot = slot->next_free;
        }
        if (!is_inserted) {
            current->next_free = md;
            md->prev_free = current ;
            md->next_free = nullptr;
        }
    }
}

void split(MetaData metaData, size_t requested_size) {
    size_t split_size = metaData->alloc_size - requested_size - MD_SIZE;
    if (split_size < SPLIT_MIN) {
        return;
    }

    MetaData newMataData = (MetaData)((size_t)metaData + MD_SIZE + requested_size);
    newMataData->alloc_address = (void*)((size_t)newMataData + MD_SIZE);
    newMataData->alloc_size = split_size;
    newMataData->is_free = true;

    histInsert(newMataData);

    newMataData->prev = metaData;
    newMataData->next = metaData->next;
    if (newMataData->next) {
        newMataData->next->prev = newMataData;
    }
    metaData->next = newMataData;
    metaData->alloc_size = requested_size;
}

void merge(MetaData metaData) {
    // Merge with next block if it's free
    MetaData next_block = metaData->next;
    if (next_block != nullptr && next_block->is_free) {
        histRemove(metaData);
        histRemove(next_block);
        metaData->alloc_size += next_block->alloc_size + MD_SIZE;
        metaData->next = next_block->next;
        if (next_block->next != nullptr) {
            next_block->next->prev = metaData;
        }
        histInsert(metaData);
    }

    // Merge with previous block if it's free
    MetaData prev_block = metaData->prev;
    if (prev_block != nullptr && prev_block->is_free) {
        histRemove(prev_block);
        histRemove(metaData);
        prev_block->alloc_size += metaData->alloc_size + MD_SIZE;
        prev_block->next = metaData->next;
        if (metaData->next != nullptr) {
            metaData->next->prev = prev_block;
        }
        histInsert(prev_block);
    }
}

void* mmap_smalloc(size_t size) {
    void* mm_block = mmap(NULL, size + MD_SIZE, PROT_READ | PROT_WRITE, 
                            MAP_POPULATE | MAP_ANON | MAP_PRIVATE, -1, 0);
    if (mm_block == MAP_FAILED) 
        return nullptr;
    
    MetaData metaData = (MetaData)mm_block;
    metaData->alloc_address = mm_block + MD_SIZE;
    metaData->alloc_size = size;
    metaData->is_free = false;

    if (!mmap_list) {
        mmap_list = metaData;
        metaData->next = metaData->prev = nullptr;
    }
    else {
        MetaData md = mmap_list;
        while (md->next) {
            md = md->next;
        }
        metaData->prev = md;
        md->next = metaData;
    }

    return metaData->alloc_address;
}

void* mmap_srealloc(void* oldp, size_t size) {

}

/* ================ Upgraded Functions ================= */

void* smalloc(size_t size) {
    if (size <= MIN_SIZE || size > MAX_SIZE) 
        return nullptr;

    if (size >= LARGE_ALLOC)
        return mmap_smalloc(size);

    // First, search for free space in memory list
    if (memory_list) {
        // Check if histogram has a free block with enough space
        int index = histIndex(size);
        for (int i = index; i < 128; i++) {
            MetaData md = histogram[i];
            while (md != nullptr) {
                if (md->alloc_size >= size) {
                    md->is_free = false;
                    histRemove(md);
                    split(md, size);
                    return md->alloc_address;
                }
                md = md->next;
            }       
        }

        // Check if wilderness chunck is free
        MetaData md = memory_list;
        while (md->next) {
            md = md->next;
        }
        if (md->is_free) {
            void* enlarge = sbrk(size - md->alloc_size);
            if (enlarge == (void*)(-1))
                return nullptr;
            
            md->alloc_size = size;
            md->is_free = false;
            histRemove(md);
            return md->alloc_address;
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
                histInsert(md);
                merge(md);
                md->is_free = true;
                return;
            }
        }
    }

    // Search for p in mmap list
    for (MetaData md = mmap_list; md != nullptr; md = md->next) {
        if (md->alloc_address == p) {
            // If 'p' has already been freed, don't do anything
            if (md->is_free) {
                return;
            }
            // Else, free the allocated block using munmap
            else {
                if (md->next != nullptr) {
                    md->next->prev = md->prev;
                }
                if (md->prev != nullptr) {
                    md->prev->next = md->next;
                } else {
                    mmap_list = md->next;
                }
                munmap(p, md->size + MD_SIZE);
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
        memmove(alloc_addr, oldp, size);
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
