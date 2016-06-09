#ifndef SMM_UTIL_H
#define SMM_UTIL_H

/**
 * Defines structures and methods for working with custom memory allocators and
 * dictionaries.
 *
 * Allocator is a struct of function pointers that are set to a specific set of
 * functions that are responsible for allocating and deallocating memory.
 * If memory allocation fails these function should abort the program.
 *
 * One implementation of Allocator given here is Permanent Allocator. It is called
 * permanent because its free method does nothing so all the memory taken from it
 * is really freed only if the entire allocator is freed or when program ends.
 * This seems useful because we need a lot of small allocations for the entire
 * duration of the program and malloc is known to be slow for such use case. We also
 * get the benefit of quickly zeroing all that memory at once.
 *
 * Dictionary is implemented as a big array of pointers to SmmDictEntry structs.
 * Hash of the key is then directly used as an index in this array which is why
 * this array needs to be big to avoid hash collisions. Size must be a power of
 * two so we can clip the hash value by using & instead of % operation.
 * By default we use arrays of 8KiB (8192) elements. Each dictionary entry also
 * has a next pointer so we can store multiple entries under the same index
 * in case of hash collisions.
 *
 * Retrieving a value from a dictionary can return NULL if entry under the given
 * key doesn't exist but you can also setup a dictionary to create and return a
 * newly created value in case it doesn't already exist. This is done by setting
 * elemCreateFunc pointer of a dictionary to point to a function that will create
 * and return a new value that will be stored under the given key in the dictionary.
 * There is also elemCreateFuncContext field on dictionary that can be set to any
 * additional context data that should be passed to elemCreateFunc.
 *
 * Function smmFreeDictEntry frees from the memory both the entry and its value.
 */

#include "smmcommon.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SMM_DEFAULT_GLOBAL_MEMORY_SIZE 64 * 1024 * 1024

typedef struct SmmAllocator* PSmmAllocator;
struct SmmAllocator {
	char* name;
	void* (*alloc)(PSmmAllocator allocator, size_t size); // Get zeroed memory if available
	void* (*malloc)(PSmmAllocator allocator, size_t size); // Just get the memory
	void* (*calloc)(PSmmAllocator allocator, size_t count, size_t size); // Get and zero memory
	void  (*free)(PSmmAllocator allocator, void* ptr);
};

#define SMM_DICTINARY_ARRAY_SIZE 8192

typedef struct SmmDictEntry* PSmmDictEntry;
struct SmmDictEntry {
	char* key;
	void* value;
	PSmmDictEntry next;
};

typedef void* (*SmmElementCreateFunc)(char* key, PSmmAllocator a, void* context);

struct SmmDict {
	void* elemCreateFuncContext;
	SmmElementCreateFunc elemCreateFunc;
};
typedef struct SmmDict* PSmmDict;

uint32_t smmUpdateHash(uint32_t hash, char val);
uint32_t smmCompleteHash(uint32_t hash);
uint32_t smmHashString(char* value);

PSmmDict smmCreateDict(PSmmAllocator allocator, size_t size, void* elemCreateFuncContext, SmmElementCreateFunc createFunc);
PSmmDictEntry smmGetDictEntry(PSmmDict dict, char* key, uint32_t hash, bool createIfMissing);
void* smmGetDictValue(PSmmDict dict, char* key, uint32_t hash, bool createIfMissing);
void smmAddDictValue(PSmmDict dict, char* key, uint32_t hash, void* value);
void smmFreeDictEntry(PSmmDict dict, char* key, uint32_t hash);

PSmmAllocator smmCreatePermanentAllocator(char* name, size_t size);
void smmFreePermanentAllocator(PSmmAllocator a);
void smmPrintAllocatorInfo(const PSmmAllocator allocator);

#endif
