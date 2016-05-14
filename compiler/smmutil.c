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

struct PrivAllocator {
	struct SmmAllocator allocator;
	size_t used;
	size_t size;
	size_t free;
	unsigned char memory[0];
};
typedef struct PrivAllocator* PPrivAllocator;

struct PrivDict {
	struct SmmDict dict;
	PSmmAllocator allocator;
	size_t size;
	PSmmDictEntry entries[0];
};
typedef struct PrivDict* PPrivDict;

/********************************************************
Private Functions
*********************************************************/

void abortWithAllocError(PSmmAllocator allocator, size_t size, int line) {
	char ainfo[64] = {0};
	sprintf(ainfo, "; Allocator: %s, Requested size: %lld", allocator->name, size);
	smmAbortWithMessage(errSmmMemoryAllocationFailed, ainfo, __FILE__, line);
}

void* globalAlloc(PSmmAllocator allocator, size_t size) {
	PPrivAllocator privAllocator = (PPrivAllocator) allocator;
	if (size > privAllocator->free) {
		abortWithAllocError(allocator, size, __LINE__ - 2);
	}
	size_t pos = privAllocator->size - privAllocator->free;
	void* location = &privAllocator->memory[pos];
	privAllocator->used += size;
	size = (size + 0xf) & (~0xf); //We align memory on 16 bytes
	privAllocator->free -= size;
	return location;
}

void* globalCAlloc(PSmmAllocator allocator, size_t count, size_t size) {
	return globalAlloc(allocator, count * size);
}

void globalFree(PSmmAllocator allocator, void* ptr) {
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
	PPrivDict privDict = allocator->alloc(allocator, sizeof(PPrivDict) + size * sizeof(PSmmDictEntry));
	privDict->allocator = allocator;
	privDict->size = size;
	privDict->dict.elemCreateFuncContext = elemCreateFuncContext;
	privDict->dict.elemCreateFunc = createFunc;
	return &privDict->dict;
}

PSmmDictEntry smmGetDictEntry(PSmmDict dict, char* key, uint32_t hash, bool createIfMissing) {
	PPrivDict privDict = (PPrivDict)dict;
	hash = hash & (privDict->size - 1);
	PSmmDictEntry* entries = ((PPrivDict)dict)->entries;
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
		int keyLength = strlen(key) + 1;
		char* keyVal = (char*)privDict->allocator->alloc(privDict->allocator, keyLength);
		strcpy(keyVal, key);
		smmAddDictValue(dict, keyVal , hash, dict->elemCreateFunc(keyVal, privDict->allocator, dict->elemCreateFuncContext));
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
	PPrivDict privDict = (PPrivDict)dict;
	PSmmAllocator a = privDict->allocator;
	PSmmDictEntry result = (PSmmDictEntry)a->alloc(a, sizeof(struct SmmDictEntry));
	hash = hash & (privDict->size - 1);
	result->key = key;
	result->value = value;
	result->next = privDict->entries[hash];
	privDict->entries[hash] = result;
}

void smmFreeDictValue(PSmmDict dict, char* key, uint32_t hash) {
	PSmmDictEntry entry = smmGetDictEntry(dict, key, hash, false);
	if (entry == NULL) return;
	PPrivDict privDict = (PPrivDict)dict;
	hash = hash & (privDict->size - 1);
	privDict->entries[hash] = entry->next;
	PSmmAllocator a = ((PPrivDict)dict)->allocator;
	a->free(a, entry->key);
	a->free(a, entry->value);
	a->free(a, entry);
}

PSmmAllocator smmCreatePermanentAllocator(char* name, size_t size) {
	assert(sizeof(struct PrivAllocator) & 0xf == 0); // So we maintain 16 byte alignment
	size = (size + 0xfff) & (~0xfff); // We take memory in chunks of 4KB
	PPrivAllocator smmAllocator = (PPrivAllocator)calloc(1, size);
	if (smmAllocator == NULL) {
		char ainfo[100] = { 0 };
		sprintf(ainfo, "; Creating allocator %s with size %lld", name, size);
		smmAbortWithMessage(errSmmMemoryAllocationFailed, ainfo, __FILE__, __LINE__ - 4);
	}
	size_t skipBytes = sizeof(struct PrivAllocator);
	skipBytes = ((skipBytes + 0xf) & (~0xf)) - skipBytes;
	smmAllocator->allocator.name = (char*)&smmAllocator->memory[skipBytes];
	strcpy(smmAllocator->allocator.name, name);
	smmAllocator->allocator.alloc = globalAlloc;
	smmAllocator->allocator.malloc = globalAlloc;
	smmAllocator->allocator.calloc = globalCAlloc;
	smmAllocator->allocator.free = globalFree;
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
	PPrivAllocator a = (PPrivAllocator)allocator;
	printf("\nAllocator %s Size=%lldMB Used=%lldKB Allocated=%lldKB Free=%lldKB\n",
		allocator->name, a->size >> 20, a->used >> 10, (a->size - a->free) >> 10, a->free >> 10);
}