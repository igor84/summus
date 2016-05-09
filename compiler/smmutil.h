#pragma once

#ifndef SMM_UTIL_H
#define SMM_UTIL_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define SMM_DEFAULT_GLOBAL_MEMORY_SIZE 64 * 1024 * 1024

typedef struct SmmAllocator* PSmmAllocator;
struct SmmAllocator {
	void* (*alloc)(PSmmAllocator allocator, size_t size); // Get zeroed memory if available
	void* (*malloc)(PSmmAllocator allocator, size_t size); // Just get the memory
	void* (*calloc)(PSmmAllocator allocator, size_t count, size_t size); // Get and zero memory
	void  (*free)(PSmmAllocator allocator, void* ptr);
};

void* smmStdLibAlloc(PSmmAllocator allocator, size_t size);
void* smmStdLibCAlloc(PSmmAllocator allocator, size_t count, size_t size);
void smmStdLibFree(PSmmAllocator allocator, void* ptr);
struct SmmAllocator SMM_DEFAULT_ALLOCATOR;

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
void smmFreeDictValue(PSmmDict dict, char* key, uint32_t hash);
PSmmAllocator smmGetGlobalAllocator(char* name, size_t size);
void smmFreeGlobalAllocator(PSmmAllocator a);
void smmPrintAllocatorInfo(const PSmmAllocator allocator);

#endif