#include "ibscommon.h"
#include "ibsallocator.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/********************************************************
Private
*********************************************************/

// Align memory on 8 bytes and it mustn't be less then pointer size
#define MEM_ALIGN 0x7
// Round allocator size to 4KB multipliers
#define ALLOCATOR_ALING 0xfff

static void abortWithAllocError(const char* msg, const char* allocatorName, size_t size, const int line) {
	printf("Compiler Error (at %s:%d): %s %s; requested size %zu\n", __FILE__, line, msg, allocatorName, size);
	exit(EXIT_FAILURE);
}

static void printSize(const char* name, size_t size) {
	static const char* units[] = { "B", "KB", "MB" };
	int unit = 0;
	while (size > 5 * 1024 && unit < 2) {
		size = size >> 10;
		unit++;
	}
	printf("%s=%u%s ", name, (uint16_t)size, units[unit]);

}

/********************************************************
Public
*********************************************************/

PIbsAllocator ibsSimpleAllocatorCreate(const char * name, size_t size) {
	size = (size + ALLOCATOR_ALING) & ~ALLOCATOR_ALING;
	PIbsAllocator ibsAllocator = (PIbsAllocator)calloc(1, size);
	if (!ibsAllocator) {
		abortWithAllocError("Failed creating allocator", name, size, __LINE__);
	}
	size_t skipBytes = sizeof(struct IbsAllocator);
	skipBytes = (skipBytes + MEM_ALIGN) & ~MEM_ALIGN;
	ibsAllocator->name = (char*)ibsAllocator + skipBytes;
	strcpy(ibsAllocator->name, name);
	skipBytes += (strlen(name) + 1 + MEM_ALIGN) & ~MEM_ALIGN;
	ibsAllocator->size = size - skipBytes;
	ibsAllocator->memory = (uint8_t*)ibsAllocator + skipBytes;
	ibsAllocator->free = ibsAllocator->size;
	//We make sure we did setup everything so next mem alloc starts from aligned address
	assert(((uintptr_t)ibsAllocator->memory & MEM_ALIGN) == 0);
	return ibsAllocator;
}

void ibsSimpleAllocatorFree(PIbsAllocator a) {
	free(a);
}

void ibsSimpleAllocatorReset(PIbsAllocator a) {
	memset(a->memory, 0, a->size);
	a->free = a->size;
	a->used = 0;
}

void ibsSimpleAllocatorPrintInfo(const PIbsAllocator a) {
	printf("\nAllocator %s ", a->name);
	printSize("Size", a->size);
	printSize("Used", a->used);
	printSize("Wasted", a->size - a->free - a->used);
	printSize("Free", a->free);
	puts("");
}

void* ibsAlloc(PIbsAllocator a, size_t size) {
	if (size == 0) return NULL;
	a->used += size;
	size = (size + MEM_ALIGN) & ~MEM_ALIGN;
	if (size > a->free) {
		ibsSimpleAllocatorPrintInfo(a);
		abortWithAllocError("Failed allocating memory in allocator", a->name, size, __LINE__);
	}
	size_t pos = a->size - a->free;
	void* location = &a->memory[pos];
	a->free -= size;
	return location;
}

void* ibsStartAlloc(PIbsAllocator a) {
	assert(a->free > 0);
	a->reserved = a->free;
	a->free = 0;
	return &a->memory[a->size - a->reserved];
}

void ibsEndAlloc(PIbsAllocator a, size_t size) {
	a->free = a->reserved;
	a->reserved = 0;
	ibsAlloc(a, size);
}
