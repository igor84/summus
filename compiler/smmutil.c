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
	PSmmDictEntry entries;
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

// This is used as a temp dict create elem func in smmAddDictValue
void* getValueToAdd(const char* key, PSmmAllocator a, void* context) {
	return context;
}

PSmmDictEntry createNewEntry(PPrivDict privDict, const char* origKey, const char* keyPart) {
	PSmmAllocator a = privDict->allocator;
	PSmmDictEntry newElem = a->alloc(a, sizeof(struct SmmDictEntry));
	newElem->keyPartLength = strlen(keyPart);
	if (privDict->dict.storeKeyCopy) {
		size_t len = strlen(origKey);
		char* newKey = a->alloc(a, len + 1);
		strncpy(newKey, origKey, len);
		newElem->keyPart = &newKey[keyPart - origKey];
		origKey = newKey;
	} else {
		newElem->keyPart = keyPart;
	}
	newElem->value = privDict->dict.elemCreateFunc(origKey, privDict->allocator, privDict->dict.elemCreateFuncContext);
	return newElem;
}

/********************************************************
API Functions
*********************************************************/

PSmmDict smmCreateDict(PSmmAllocator allocator, void* elemCreateFuncContext, SmmElementCreateFunc createFunc) {
	PPrivDict privDict = allocator->alloc(allocator, sizeof(struct PrivDict));
	privDict->allocator = allocator;
	privDict->dict.storeKeyCopy = true;
	privDict->dict.elemCreateFuncContext = elemCreateFuncContext;
	privDict->dict.elemCreateFunc = createFunc;
	return &privDict->dict;
}

PSmmDictEntry smmGetDictEntry(PSmmDict dict, const char* key, bool createIfMissing) {
	if (key == NULL) return NULL;
	createIfMissing = createIfMissing && dict->elemCreateFunc;

	PPrivDict privDict = (PPrivDict)dict;
	PSmmAllocator a = privDict->allocator;
	
	PSmmDictEntry* el = &privDict->entries;
	PSmmDictEntry entry;
	const char* origKey = key;

	while (*el) {
		entry = *el;
		size_t i = 0;
		while (key[i] == entry->keyPart[i] && key[i] != 0 && entry->keyPartLength > i) {
			i++;
		}

		if (key[i] == 0 || (i > 0 && i < entry->keyPartLength)) {
			if (entry->keyPartLength == i) {
				// Existing key so create a value if it doesn't exist and return it
				if (!entry->value && createIfMissing) {
					if (dict->storeKeyCopy) {
						size_t len = strlen(origKey);
						char* newKey = a->alloc(a, len + 1);
						strncpy(newKey, origKey, len);
						origKey = newKey;
					}
					entry->value = dict->elemCreateFunc(origKey, privDict->allocator, dict->elemCreateFuncContext);
				}
				return entry;
			}
			if (!createIfMissing) return NULL;
			// We got a key that is a part of existing key so we need to split existing into parts
			PSmmDictEntry newElem = a->alloc(a, sizeof(struct SmmDictEntry));
			newElem->keyPart = &entry->keyPart[i];
			newElem->keyPartLength = entry->keyPartLength - i;
			newElem->value = entry->value;
			newElem->children = entry->children;
			entry->children = newElem;
			entry->keyPartLength = i;
			if (key[i] == 0) {
				if (privDict->dict.storeKeyCopy) {
					size_t len = strlen(origKey);
					char* newKey = a->alloc(a, len + 1);
					strncpy(newKey, origKey, len);
					origKey = newKey;
				}
				entry->value = dict->elemCreateFunc(origKey, privDict->allocator, dict->elemCreateFuncContext);
				return entry;
			}
			entry->value = NULL;
			newElem = createNewEntry(privDict, origKey, &key[i]);
			newElem->next = entry->children;
			entry->children = newElem;
			return newElem;
		}
		if (entry->keyPartLength == i) {
			PSmmDictEntry* nextField = &entry->children;
			while (*nextField && (*nextField)->keyPart[0] != key[i]) nextField = &(*nextField)->next;
			if (!*nextField) {
				if (!createIfMissing) return NULL;
				PSmmDictEntry newElem = createNewEntry(privDict, origKey, &key[i]);
				*nextField = newElem;
				return newElem;
			}
			key = &key[i];
			el = nextField;
		} else {
			el = &entry->next;
		}
	}

	if (!createIfMissing) return NULL;
	entry = createNewEntry(privDict, origKey, origKey);
	*el = entry;
	return entry;
}

void* smmGetDictValue(PSmmDict dict, const char* key, bool createIfMissing) {
	PSmmDictEntry entry = smmGetDictEntry(dict, key, createIfMissing);
	assert(!createIfMissing || (entry != NULL));
	if (entry == NULL) return NULL;
	return entry->value;
}

void smmAddDictValue(PSmmDict dict, const char* key, void* value) {
	SmmElementCreateFunc oldFunc = dict->elemCreateFunc;
	void* oldContext = dict->elemCreateFuncContext;
	dict->elemCreateFuncContext = value;
	dict->elemCreateFunc = getValueToAdd;
	PSmmDictEntry entry = smmGetDictEntry(dict, key, true);
	if (entry->value != value) entry->value = value;
	dict->elemCreateFuncContext = oldContext;
	dict->elemCreateFunc = oldFunc;
}

PSmmAllocator smmCreatePermanentAllocator(const char* name, size_t size) {
	size = (size + 0xfff) & (~0xfff); // We take memory in chunks of 4KB
	PPrivAllocator smmAllocator = (PPrivAllocator)calloc(1, size);
	if (!smmAllocator) {
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
