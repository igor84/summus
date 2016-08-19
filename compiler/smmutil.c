#include "smmutil.h"
#include "smmmsgs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
	size_t stackSize;
	unsigned char* memory;
	unsigned char* stack;
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

//We align memory on 8 bytes and it mustn't be less then pointer size
#define MEM_ALIGN 0x7

static void abortWithAllocError(const char* msg, const char* allocatorName, size_t size, const int line) {
	char wholeMsg[500] = {0};
	snprintf(wholeMsg, 500, "ERROR: %s %s; Requested size: %zu", msg, allocatorName, size);
	smmAbortWithMessage(wholeMsg, __FILE__, line);
}

static void* globalAlloc(PSmmAllocator allocator, size_t size) {
	PPrivAllocator privAllocator = (PPrivAllocator) allocator;
	privAllocator->used += size;
	size = (size + MEM_ALIGN) & ~MEM_ALIGN;
	if (size > privAllocator->free) {
		smmPrintAllocatorInfo(allocator);
		abortWithAllocError("Failed allocating memory in allocator", allocator->name, size, __LINE__);
	}
	size_t pos = privAllocator->size - privAllocator->free;
	void* location = &privAllocator->memory[pos];
	privAllocator->free -= size;
	return location;
}

static void* globalCAlloc(PSmmAllocator allocator, size_t count, size_t size) {
	return globalAlloc(allocator, count * size);
}

static void* globalAllocA(PSmmAllocator allocator, size_t size) {
	PPrivAllocator privAllocator = (PPrivAllocator)allocator;
	void* loc = privAllocator->stack;
	size = (size + MEM_ALIGN + MEM_ALIGN) & ~MEM_ALIGN;
	if (size > privAllocator->stackSize) {
		smmPrintAllocatorInfo(allocator);
		abortWithAllocError("Stack overflow in allocator", allocator->name, size, __LINE__);
	}
	privAllocator->stack += size;
	privAllocator->stackSize -= size;
	*((size_t *)(privAllocator->stack - MEM_ALIGN - 1)) = size;
	return loc;
}

static void globalFree(PSmmAllocator allocator, void* ptr) {
	// Does nothing
}

static void globalFreeA(PSmmAllocator allocator, void* ptr) {
	if (!ptr) return;
	PPrivAllocator privAllocator = (PPrivAllocator)allocator;
	size_t lastSize = *((size_t *)(privAllocator->stack - MEM_ALIGN - 1));
	assert(lastSize);
	privAllocator->stack -= lastSize;
	privAllocator->stackSize += lastSize;
	assert(privAllocator->stack == ptr);
	memset(privAllocator->stack, 0, lastSize);
}

// This is used as a temp dict create elem func in smmAddDictValue
static void* getValueToAdd(const char* key, PSmmAllocator a, void* context) {
	return context;
}

static PSmmDictEntry createNewEntry(PPrivDict privDict, const char* origKey, const char* keyPart) {
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
	newElem->values = a->alloc(a, sizeof(struct SmmDictEntryValue));
	newElem->values->value = privDict->dict.elemCreateFunc(origKey, a, privDict->dict.elemCreateFuncContext);
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
				if (!entry->values && createIfMissing) {
					if (dict->storeKeyCopy) {
						size_t len = strlen(origKey);
						char* newKey = a->alloc(a, len + 1);
						strncpy(newKey, origKey, len);
						origKey = newKey;
					}
					entry->values = a->alloc(a, sizeof(struct SmmDictEntryValue));
					entry->values->value = dict->elemCreateFunc(origKey, privDict->allocator, dict->elemCreateFuncContext);
				}
				return entry;
			}
			if (!createIfMissing) return NULL;
			// We got a key that is a part of existing key so we need to split existing into parts
			PSmmDictEntry newElem = a->alloc(a, sizeof(struct SmmDictEntry));
			newElem->keyPart = &entry->keyPart[i];
			newElem->keyPartLength = entry->keyPartLength - i;
			newElem->values = entry->values;
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
				entry->values = a->alloc(a, sizeof(struct SmmDictEntryValue));
				entry->values->value = dict->elemCreateFunc(origKey, privDict->allocator, dict->elemCreateFuncContext);
				return entry;
			}
			entry->values = NULL;
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
	assert(!createIfMissing || (entry != NULL && entry->values != NULL));
	if (entry == NULL || entry->values == NULL) return NULL;
	return entry->values->value;
}

void smmAddDictValue(PSmmDict dict, const char* key, void* value) {
	SmmElementCreateFunc oldFunc = dict->elemCreateFunc;
	void* oldContext = dict->elemCreateFuncContext;
	dict->elemCreateFuncContext = value;
	dict->elemCreateFunc = getValueToAdd;
	PSmmDictEntry entry = smmGetDictEntry(dict, key, true);
	if (entry->values->value != value) entry->values->value = value;
	dict->elemCreateFuncContext = oldContext;
	dict->elemCreateFunc = oldFunc;
}

void smmPushDictValue(PSmmDict dict, const char* key, void* value) {
	SmmElementCreateFunc oldFunc = dict->elemCreateFunc;
	void* oldContext = dict->elemCreateFuncContext;
	dict->elemCreateFuncContext = value;
	dict->elemCreateFunc = getValueToAdd;
	PSmmDictEntry entry = smmGetDictEntry(dict, key, true);
	if (entry->values->value != value) {
		PSmmAllocator a = ((PPrivDict)dict)->allocator;
		PSmmDictEntryValue newValue = a->alloc(a, sizeof(struct SmmDictEntryValue));
		newValue->next = entry->values;
		newValue->value = value;
		entry->values = newValue;
	}
	dict->elemCreateFuncContext = oldContext;
	dict->elemCreateFunc = oldFunc;
}

void* smmPopDictValue(PSmmDict dict, const char* key) {
	PSmmDictEntry entry = smmGetDictEntry(dict, key, false);
	if (!entry || !entry->values) return NULL;
	PSmmDictEntryValue value = entry->values;
	entry->values = value->next;
	return value->value;
}

PSmmAllocator smmCreatePermanentAllocator(const char* name, size_t size) {
	size = (size + MEM_ALIGN) & ~MEM_ALIGN;
	size_t sizeWithStack = (size + 0x2fff) & ~0xfff; // Stack size is 8KB + round up to 4KB
	PPrivAllocator smmAllocator = (PPrivAllocator)calloc(1, sizeWithStack);
	if (!smmAllocator) {
		abortWithAllocError("Failed creating allocator", name, sizeWithStack, __LINE__);
	}
	size_t skipBytes = sizeof(struct PrivAllocator);
	skipBytes = (skipBytes + MEM_ALIGN) & ~MEM_ALIGN;
	smmAllocator->allocator.name = (char*)smmAllocator + skipBytes;
	strcpy(smmAllocator->allocator.name, name);
	smmAllocator->allocator.alloc = globalAlloc;
	smmAllocator->allocator.malloc = globalAlloc;
	smmAllocator->allocator.calloc = globalCAlloc;
	smmAllocator->allocator.free = globalFree;
	smmAllocator->allocator.alloca = globalAllocA;
	smmAllocator->allocator.freea = globalFreeA;
	skipBytes += (strlen(name) + 1 + MEM_ALIGN) & ~MEM_ALIGN;
	smmAllocator->size = size - skipBytes;
	smmAllocator->memory = (unsigned char*)smmAllocator + skipBytes;
	smmAllocator->stack = (unsigned char*)smmAllocator + size;
	smmAllocator->free = smmAllocator->size;
	smmAllocator->stackSize = sizeWithStack - size;
	//We make sure we did setup everything so next mem alloc starts from aligned address
	assert(((uintptr_t)smmAllocator->memory & MEM_ALIGN) == 0);
	return &smmAllocator->allocator;
}

void smmFreePermanentAllocator(PSmmAllocator allocator) {
	free(allocator);
}

void smmPrintAllocatorInfo(const PSmmAllocator allocator) {
	PPrivAllocator a = (PPrivAllocator)allocator;
	printf("\nAllocator %s Size=%zuKB Used=%zuKB Allocated=%zuKB Free=%zuKB\n",
		allocator->name, a->size >> 10, a->used >> 10, (a->size - a->free) >> 10, a->free >> 10);
}
