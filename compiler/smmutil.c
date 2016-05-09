#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "smmutil.h"

// Disable warning that we are using variable length arrays in structs
#pragma warning(disable : 4200)

struct SmmPrivAllocator {
	struct SmmAllocator allocator;
	size_t used;
	size_t size;
	size_t free;
	char* name;
	unsigned char memory[0];
};
typedef struct SmmPrivAllocator* PSmmPrivAllocator;

struct SmmPrivDict {
	struct SmmDict dict;
	PSmmAllocator allocator;
	size_t size;
	PSmmDictEntry entries[];
};
typedef struct SmmPrivDict* PSmmPrivDict;

void* smmCreateAllocator(char* key, PSmmAllocator a, void* context);

#define SMM_ALLOCATOR_DICT_SIZE 8 * 1024

static struct {
	struct SmmDict dict;
	PSmmAllocator allocator;
	size_t size;
	PSmmDictEntry entries[SMM_ALLOCATOR_DICT_SIZE];
} SmmAllocatorDict = { { NULL, smmCreateAllocator }, &SMM_DEFAULT_ALLOCATOR, SMM_ALLOCATOR_DICT_SIZE };

struct SmmAllocator SMM_DEFAULT_ALLOCATOR = { smmStdLibAlloc, smmStdLibAlloc, smmStdLibCAlloc, smmStdLibFree };

/********************************************************
Private Functions
*********************************************************/

void* smmStdLibAlloc(PSmmAllocator allocator, size_t size) {
	void* result = malloc(size);
	assert(result != NULL);
	return result;
}
void* smmStdLibCAlloc(PSmmAllocator allocator, size_t count, size_t size) {
	void* result = calloc(count, size);
	assert(result != NULL);
	return result;
}

void smmStdLibFree(PSmmAllocator allocator, void* ptr) {
	free(ptr);
}

void* smmGlobalAlloc(PSmmAllocator allocator, size_t size) {
	PSmmPrivAllocator privAllocator = (PSmmPrivAllocator) allocator;
	assert(size < privAllocator->free);
	size_t pos = privAllocator->size - privAllocator->free;
	void* location = &privAllocator->memory[pos];
	privAllocator->used += size;
	size = (size + 15) & (~0xf); //We align memory on 16 bytes
	privAllocator->free -= size;
	return location;
}

void* smmGlobalCAlloc(PSmmAllocator allocator, size_t count, size_t size) {
	return smmGlobalAlloc(allocator, count * size);
}

void smmGlobalFree(PSmmAllocator allocator, void* ptr) {
	// Does nothing
}

void* smmCreateAllocator(char* key, PSmmAllocator a, void* context) {
	size_t* size = (size_t *)context;
	PSmmPrivAllocator smmAllocator = (PSmmPrivAllocator)calloc(1, *size);
	assert(sizeof(struct SmmPrivAllocator) & 0xf == 0); // So we maintain 16 byte alignment
	smmAllocator->allocator.alloc = smmGlobalAlloc;
	smmAllocator->allocator.malloc = smmGlobalAlloc;
	smmAllocator->allocator.calloc = smmGlobalCAlloc;
	smmAllocator->allocator.free = smmGlobalFree;
	smmAllocator->name = key;
	smmAllocator->size = *size - sizeof(struct SmmPrivAllocator);
	smmAllocator->free = smmAllocator->size;
	return smmAllocator;
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
	PSmmPrivDict privDict = (PSmmPrivDict)dict;
	hash = hash & (privDict->size - 1);
	privDict->entries[hash] = entry->next;
	PSmmAllocator a = ((PSmmPrivDict)dict)->allocator;
	a->free(a, entry->key);
	a->free(a, entry->value);
	a->free(a, entry);
}

PSmmAllocator smmGetGlobalAllocator(char* name, size_t size) {
	size = (size + 0xfff) & (~0xfff); // We take memory in chunks of 4KB
	SmmAllocatorDict.dict.elemCreateFuncContext = &size;
	return (PSmmAllocator)smmGetDictValue(&SmmAllocatorDict.dict, name, smmHashString(name), true);
}

void smmFreeGlobalAllocator(PSmmAllocator allocator) {
	PSmmPrivAllocator a = (PSmmPrivAllocator)allocator;
	smmFreeDictValue(&SmmAllocatorDict.dict, a->name, smmHashString(a->name));
}

void smmPrintAllocatorInfo(const PSmmAllocator allocator) {
	PSmmPrivAllocator a = (PSmmPrivAllocator)allocator;
	printf("Allocator %s Size=%lldMB Used=%lldKB Allocated=%lldKB Free=%lldKB\n",
		a->name, a->size >> 20, a->used >> 10, (a->size - a->free) >> 10, a->free >> 10);
}