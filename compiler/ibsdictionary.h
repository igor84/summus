#pragma once

/**
 * Defines structures and methods for working with dictionary.
 *
 * Dictionary is implemented as a variation on Trie (https://en.wikipedia.org/wiki/Trie)
 */

#include "ibsallocator.h"
#include <stdlib.h>

typedef struct IbsDictEntryValue* PIbsDictEntryValue;
struct IbsDictEntryValue {
	void* value;
	PIbsDictEntryValue next;
};

typedef struct IbsDictEntry* PIbsDictEntry;
struct IbsDictEntry {
	const char* keyPart;
	size_t keyPartLength;
	PIbsDictEntryValue values;
	PIbsDictEntry children;
	PIbsDictEntry next;
};

/* All fields must be treated as readonly in order for allocator functions to work */
struct IbsDict {
	PIbsAllocator a;
	PIbsDictEntry entries;
	const char* lastKey;
	PIbsDictEntry lastEntry;
};
typedef struct IbsDict* PIbsDict;

/**
 * Creates a new dictinary that will use given allocator for all allocations.
 * Note that if keys and values are coming from a different allocator and that
 * allocator is cleaned then this dictinary will be full of dead pointers.
 */
PIbsDict ibsDictCreate(PIbsAllocator allocator);
PIbsDictEntry ibsDictGetEntry(PIbsDict dict, const char* key);
void* ibsDictGet(PIbsDict dict, const char* key);
void ibsDictPut(PIbsDict dict, const char* key, void* value);

/**
 * if the given key exists it adds a new value to it and makes it current.
 * Otherwise just adds a new key value pair.
 */
void ibsDictPush(PIbsDict dict, const char* key, void* value);

/**
 * Returns the current value from the given key and removes it from that key
 * making the previous value it had current. If there is no value under the
 * key or no key it will just return NULL.
 */
void* ibsDictPop(PIbsDict dict, const char* key);
