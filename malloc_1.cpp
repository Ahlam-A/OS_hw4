#include <stdlib.h>
#include <unistd.h>
#include "os_malloc.h"

#define MIN_SIZE 0
#define MAX_SIZE 100000000

void* smalloc(size_t size) {
   if (size <= MIN_SIZE || size > MAX_SIZE)
      return NULL;
   
   void* address = sbrk(size);
   if (address == (void*)(-1))
      return NULL;
   
   return address;
}
