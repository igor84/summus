#include "smmutil.h"
#include "smmmsgs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MSG_BUFFER_LENGTH 100

/**
 * We create a private Allocator struct with additional data that has public struct
 * as the first member so &PrivAllocatorVar == &PrivAllocatorVar.allocator which
 * allows us to cast between PSmmAllocator and PPrivAllocator. We use this technique
 * for Dictinary as well.
 */
struct PrivAllocator {
	struct SmmAllocator allocator;
	size_t used;
	size_t size;
	size_t free;
	unsigned char* memory;
};
typedef struct PrivAllocator* PPrivAllocator;

struct PrivDict {
	struct SmmDict dict;
	PSmmAllocator allocator;
	size_t size;
	PSmmDictEntry* entries;
};
typedef struct PrivDict* PPrivDict;

/********************************************************
Private Functions
*********************************************************/

void abortWithAllocError(PSmmAllocator allocator, size_t size, int line) {
	char ainfo[MSG_BUFFER_LENGTH] = {0};
	snprintf(ainfo, MSG_BUFFER_LENGTH, "; Allocator: %s, Requested size: %zu", allocator->name, size);
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

uint32_t smmHashString(const char* value) {
	uint32_t hash = 0;
	const char* cc = value;
	while (*cc != 0) {
		hash = smmUpdateHash(hash, *cc);
		cc++;
	}
	return smmCompleteHash(hash);
}

PSmmDict smmCreateDict(PSmmAllocator allocator, size_t size, void* elemCreateFuncContext, SmmElementCreateFunc createFunc) {
	assert(size && !(size & (size - 1))); // Size must have only one bit set
	PPrivDict privDict = allocator->alloc(allocator, sizeof(struct PrivDict) + size * sizeof(PSmmDictEntry));
	privDict->allocator = allocator;
	privDict->size = size;
	privDict->dict.storeKeyCopy = true;
	privDict->dict.elemCreateFuncContext = elemCreateFuncContext;
	privDict->dict.elemCreateFunc = createFunc;
	privDict->entries = (PSmmDictEntry*)(privDict + 1);
	return &privDict->dict;
}

PSmmDictEntry smmGetDictEntry(PSmmDict dict, const char* key, uint32_t hash, bool createIfMissing) {
	PPrivDict privDict = (PPrivDict)dict;
	hash = hash & (privDict->size - 1);
	PSmmDictEntry* entries = privDict->entries;
	PSmmDictEntry result = entries[hash];
	PSmmDictEntry last = NULL;

	while (result) {
		if (key == result->key || strcmp(key, result->key) == 0) {
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
		if (dict->storeKeyCopy) {
			size_t keyLength = strlen(key) + 1;
			char* keyVal = (char*)privDict->allocator->alloc(privDict->allocator, keyLength);
			strcpy(keyVal, key);
			key = keyVal;
		}
		smmAddDictValue(dict, key , hash, dict->elemCreateFunc(key, privDict->allocator, dict->elemCreateFuncContext));
		return entries[hash];
	}

	return NULL;
}

void* smmGetDictValue(PSmmDict dict, const char* key, uint32_t hash, bool createIfMissing) {
	PSmmDictEntry entry = smmGetDictEntry(dict, key, hash, createIfMissing);
	assert(!createIfMissing || (entry != NULL));
	if (entry == NULL) return NULL;
	return entry->value;
}

void smmAddDictValue(PSmmDict dict, const char* key, uint32_t hash, void* value) {
	PPrivDict privDict = (PPrivDict)dict;
	PSmmAllocator a = privDict->allocator;
	PSmmDictEntry result = (PSmmDictEntry)a->alloc(a, sizeof(struct SmmDictEntry));
	hash = hash & (privDict->size - 1);
	result->key = key;
	result->value = value;
	result->next = privDict->entries[hash];
	privDict->entries[hash] = result;
}

void smmFreeDictEntry(PSmmDict dict, const char* key, uint32_t hash) {
	PPrivDict privDict;
	PSmmDictEntry entry = smmGetDictEntry(dict, key, hash, false);
	if (entry == NULL) return;
	privDict = (PPrivDict)dict;
	hash = hash & (privDict->size - 1);
	privDict->entries[hash] = entry->next;
	PSmmAllocator a = ((PPrivDict)dict)->allocator;
	// Even though new key is created for new entry it is not freed here because
	// it is usually later used in different places
	a->free(a, entry->value);
	a->free(a, entry);
}

PSmmAllocator smmCreatePermanentAllocator(const char* name, size_t size) {
	size = (size + 0xfff) & (~0xfff); // We take memory in chunks of 4KB
	PPrivAllocator smmAllocator = (PPrivAllocator)calloc(1, size);
	if (smmAllocator == NULL) {
		char ainfo[MSG_BUFFER_LENGTH] = { 0 };
		snprintf(ainfo, MSG_BUFFER_LENGTH, "; Creating allocator %s with size %zu", name, size);
		smmAbortWithMessage(errSmmMemoryAllocationFailed, ainfo, __FILE__, __LINE__ - 4);
	}
	size_t skipBytes = sizeof(struct PrivAllocator);
	skipBytes = (skipBytes + 0xf) & (~0xf);
	smmAllocator->memory = (unsigned char*)smmAllocator + skipBytes;
	smmAllocator->allocator.name = (char*)smmAllocator->memory;
	strcpy(smmAllocator->allocator.name, name);
	smmAllocator->allocator.alloc = globalAlloc;
	smmAllocator->allocator.malloc = globalAlloc;
	smmAllocator->allocator.calloc = globalCAlloc;
	smmAllocator->allocator.free = globalFree;
	skipBytes += (strlen(name) + 1 + 0xf) & (~0xf);
	smmAllocator->size = size - skipBytes;
	smmAllocator->memory = (unsigned char*)smmAllocator + skipBytes;
	smmAllocator->free = smmAllocator->size;
	//We make sure we did setup everything so next mem alloc starts from 16 bytes aligned address
	assert(((uintptr_t)smmAllocator->memory & 0xf) == 0);
	return &smmAllocator->allocator;
}

void smmFreePermanentAllocator(PSmmAllocator allocator) {
	free(allocator);
}

void smmPrintAllocatorInfo(const PSmmAllocator allocator) {
	PPrivAllocator a = (PPrivAllocator)allocator;
	printf("\nAllocator %s Size=%zuMB Used=%zuKB Allocated=%zuKB Free=%zuKB\n",
		allocator->name, a->size >> 20, a->used >> 10, (a->size - a->free) >> 10, a->free >> 10);
}
