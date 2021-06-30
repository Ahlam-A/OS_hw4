#include <stdlib.h>
#include <unistd.h>

#define MIN_SIZE 0
#define MAX_SIZE 100000000

void* smalloc(size_t size) {
   void* address;
   if (size <= MIN_SIZE || size > MAX_SIZE)
      return NULL;
   
   address = sbrk(size);
   if (address == (void*)(-1))
      return NULL;
   
   return address;
}

