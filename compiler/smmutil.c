#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "smmutil.h"
#include "smmmsgs.h"

// Disable warning that we are using variable length arrays in structs
#pragma warning(disable : 4200)

struct SmmPrivAllocator {
	struct SmmAllocator allocator;
	size_t used;
	size_t size;
	size_t free;
	unsigned char memory[0];
};
typedef struct SmmPrivAllocator* PSmmPrivAllocator;

struct SmmPrivDict {
	struct SmmDict dict;
	PSmmAllocator allocator;
	size_t size;
	PSmmDictEntry entries[0];
};
typedef struct SmmPrivDict* PSmmPrivDict;

struct SmmAllocator SMM_STDLIB_ALLOCATOR = { "STDLIB", smmStdLibAlloc, smmStdLibAlloc, smmStdLibCAlloc, smmStdLibFree };

/********************************************************
Private Functions
*********************************************************/

void smmAbortWithAllocError(PSmmAllocator allocator, size_t size, int line) {
	char ainfo[64] = {0};
	sprintf(ainfo, "; Allocator: %s, Requested size: %lld", allocator->name, size);
	smmAbortWithMessage(errSmmMemoryAllocationFailed, ainfo, __FILE__, line);
}

void* smmStdLibAlloc(PSmmAllocator allocator, size_t size) {
	void* result = malloc(size);
	if (result == NULL) {
		smmAbortWithAllocError(allocator, size, __LINE__ - 2);
	}
	return result;
}
void* smmStdLibCAlloc(PSmmAllocator allocator, size_t count, size_t size) {
	void* result = calloc(count, size);
	if (result == NULL) {
		smmAbortWithAllocError(allocator, size, __LINE__ - 2);
	}
	return result;
}

void smmStdLibFree(PSmmAllocator allocator, void* ptr) {
	free(ptr);
}

void* smmGlobalAlloc(PSmmAllocator allocator, size_t size) {
	PSmmPrivAllocator privAllocator = (PSmmPrivAllocator) allocator;
	if (size > privAllocator->free) {
		smmAbortWithAllocError(allocator, size, __LINE__ - 2);
	}
	size_t pos = privAllocator->size - privAllocator->free;
	void* location = &privAllocator->memory[pos];
	privAllocator->used += size;
	size = (size + 0xf) & (~0xf); //We align memory on 16 bytes
	privAllocator->free -= size;
	return location;
}

void* smmGlobalCAlloc(PSmmAllocator allocator, size_t count, size_t size) {
	return smmGlobalAlloc(allocator, count * size);
}

void smmGlobalFree(PSmmAllocator allocator, void* ptr) {
	// Does nothing
}

/********************************************************
API Functions
*********************************************************/

uint32_t smmUpdateHash(uint32_t hash, char val) {
	uint32_t result = hash + val;
	result = result + (result << 10);
	return result ^ (result >> 6);
}

uint32_t smmCompleteHash(uint32_t hash) {
	uint32_t result = hash + (hash << 3);
	result = result ^ (result >> 11);
	return result + (result << 15);
}

uint32_t smmHashString(char* value) {
	uint32_t hash = 0;
	char* cc = value;
	while (*cc != 0) {
		hash = smmUpdateHash(hash, *cc);
		cc++;
	}
	return smmCompleteHash(hash);
}

PSmmDict smmCreateDict(PSmmAllocator allocator, size_t size, void* elemCreateFuncContext, SmmElementCreateFunc createFunc) {
	assert((size && !(size & (size - 1))) == 0); // Size has only one bit set
	PSmmPrivDict privDict = allocator->alloc(allocator, sizeof(PSmmPrivDict) + size * sizeof(PSmmDictEntry));
	privDict->allocator = allocator;
	privDict->size = size;
	privDict->dict.elemCreateFuncContext = elemCreateFuncContext;
	privDict->dict.elemCreateFunc = createFunc;
	return &privDict->dict;
}

PSmmDictEntry smmGetDictEntry(PSmmDict dict, char* key, uint32_t hash, bool createIfMissing) {
	PSmmPrivDict privDict = (PSmmPrivDict)dict;
	hash = hash & (privDict->size - 1);
	PSmmDictEntry* entries = ((PSmmPrivDict)dict)->entries;
	PSmmDictEntry result = entries[hash];
	PSmmDictEntry last = NULL;

	while (result) {
		if (strcmp(key, result->key) == 0) {
			// Put the found element on start of the list so next search is faster
			if (last) {
				last->next = result->next;
				result->next = entries[hash];
				entries[hash] = result;
			}
			return result;
		}
		last = result;
		result = result->next;
	}

	if (createIfMissing && dict->elemCreateFunc) {
		smmAddDictValue(dict, key, hash, dict->elemCreateFunc(key, privDict->allocator, dict->elemCreateFuncContext));
		return entries[hash];
	}

	return NULL;
}

void* smmGetDictValue(PSmmDict dict, char* key, uint32_t hash, bool createIfMissing) {
	PSmmDictEntry entry = smmGetDictEntry(dict, key, hash, createIfMissing);
	assert(entry != NULL);
	return entry->value;
}

void smmAddDictValue(PSmmDict dict, char* key, uint32_t hash, void* value) {
	PSmmPrivDict privDict = (PSmmPrivDict)dict;
	PSmmAllocator a = privDict->allocator;
	PSmmDictEntry result = (PSmmDictEntry)a->alloc(a, sizeof(struct SmmDictEntry));
	hash = hash & (privDict->size - 1);
	int keyLength = strlen(key) + 1;
	char* keyVal = (char*)a->alloc(a, keyLength);
	result->key = keyVal;
	strcpy(result->key, key);
	result->value = value;
	result->next = privDict->entries[hash];
	privDict->entries[hash] = result;
}

void smmFreeDictValue(PSmmDict dict, char* key, uint32_t hash) {
	PSmmDictEntry entry = smmGetDictEntry(dict, key, hash, false);
	if (entry == NULL) return;
	PSmmPrivDict privDict = (PSmmPrivDict)dict;
	hash = hash & (privDict->size - 1);
	privDict->entries[hash] = entry->next;
	PSmmAllocator a = ((PSmmPrivDict)dict)->allocator;
	a->free(a, entry->key);
	a->free(a, entry->value);
	a->free(a, entry);
}

PSmmAllocator smmCreatePermanentAllocator(char* name, size_t size) {
	assert(sizeof(struct SmmPrivAllocator) & 0xf == 0); // So we maintain 16 byte alignment
	size = (size + 0xfff) & (~0xfff); // We take memory in chunks of 4KB
	PSmmPrivAllocator smmAllocator = (PSmmPrivAllocator)calloc(1, size);
	if (smmAllocator == NULL) {
		char ainfo[100] = { 0 };
		sprintf(ainfo, "; Creating allocator %s with size %lld", name, size);
		smmAbortWithMessage(errSmmMemoryAllocationFailed, ainfo, __FILE__, __LINE__ - 4);
	}
	size_t skipBytes = sizeof(struct SmmPrivAllocator);
	skipBytes = ((skipBytes + 0xf) & (~0xf)) - skipBytes;
	smmAllocator->allocator.name = (char*)&smmAllocator->memory[skipBytes];
	strcpy(smmAllocator->allocator.name, name);
	smmAllocator->allocator.alloc = smmGlobalAlloc;
	smmAllocator->allocator.malloc = smmGlobalAlloc;
	smmAllocator->allocator.calloc = smmGlobalCAlloc;
	smmAllocator->allocator.free = smmGlobalFree;
	smmAllocator->size = size;
	skipBytes += (strlen(name) + 1 + 0xf) & (~0xf);
	smmAllocator->free = smmAllocator->size - skipBytes;
	//We make sure we did setup everything so next mem alloc starts from 16 bytes aligned address
	assert(((int)&smmAllocator->memory[smmAllocator->size - smmAllocator->free] & 0xf) == 0);
	return &smmAllocator->allocator;
}

void smmFreePermanentAllocator(PSmmAllocator allocator) {
	free(allocator);
}

void smmPrintAllocatorInfo(const PSmmAllocator allocator) {
	PSmmPrivAllocator a = (PSmmPrivAllocator)allocator;
	printf("Allocator %s Size=%lldMB Used=%lldKB Allocated=%lldKB Free=%lldKB\n",
		allocator->name, a->size >> 20, a->used >> 10, (a->size - a->free) >> 10, a->free >> 10);
}