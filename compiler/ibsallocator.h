#pragma once

/**
 * Defines structures and methods for working with custom memory allocators.
 *
 * One implementation of allocator given here is Simple Allocator. It doesn't support
 * free so it only needs to save where is the next free location in memory for next
 * allocation. Only the entire allocator can be freed or it be reset if we just want
 * to reuse it from scratch.
 * This seems useful because we need a lot of small allocations for the entire
 * duration of the program and malloc is known to be slow for such use case. We also
 * get the benefit of quickly zeroing all that memory at once.
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* All fields must be treated as readonly in order for allocator functions to work */
struct IbsAllocator {
	char* name;
	size_t size;
	size_t free;
	size_t used;
	uint8_t* memory;
};
typedef struct IbsAllocator* PIbsAllocator;

/**
 * Creates a new allocator with requested size rounded up to 4KB chunks.
 * A certain number of starting bytes is occupied to keep allocator metadata.
 */
PIbsAllocator ibsSimpleAllocatorCreate(const char* name, size_t size);
void ibsSimpleAllocatorFree(PIbsAllocator a);
void ibsSimpleAllocatorReset(PIbsAllocator a);
void ibsSimpleAllocatorPrintInfo(const PIbsAllocator allocator);

/**
 * Allocates the requested number of bytes from the given allocator
 */
void* ibsAlloc(PIbsAllocator a, size_t size);
