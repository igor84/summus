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
 * Dictinary stores arbitrary values with coresponding keys and allows fast
 * lookups. It is implemented as a variant of trie of SmmDictEntry structs. It
 * is best explained on an example. Empty dictionary has no entries so when we
 * add key "abort" we have only one entry whose keyPart="abort", keyLength=5
 * and value is set to val1. If we now want to add a key "about" dict is
 * restructured to look like this:
 *
 * keyPart="abo", keyLength = 3, value=NULL, children pointer points to a new node:
 * keyPart="rt", keyLength = 2, value=val1, next pointer points to a new node:
 * keyPart="ut", keyLength = 2, value=val2
 *
 * So each node contains part of the key and its children contain the rest of the
 * key and so on until a node that contains the end part of the key is ecountered
 * and that node needs to contain a value that is associated with that key. This
 * way we waste much less memory than with a hash table and lookup mostly depends
 * only on key length which also holds for hash table approach so we gain less
 * memory usage with no speed loss. For example on 64bit build with a hashtable
 * of 8K buckets compiler at one point used 130KB of memory and when changed to
 * trie it used only 12KB.

 * Retrieving a value from a dictionary can return NULL if entry under the given
 * key doesn't exist but you can also setup a dictionary to create and return a
 * newly created value in case it doesn't already exist. This is done by setting
 * elemCreateFunc pointer of a dictionary to point to a function that will create
 * and return a new value that will be stored under the given key in the dictionary.
 * There is also elemCreateFuncContext field on dictionary that can be set to any
 * additional context data that should be passed to elemCreateFunc.
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
	void* (*alloca)(PSmmAllocator allocator, size_t size); // Get zeroed memory if available from simulated stack
	void  (*freea)(PSmmAllocator allocator, void* ptr); // Free ptr if it is last entry from the simulated stack, otherwise assert(false) 
};

typedef struct SmmDictEntryValue* PSmmDictEntryValue;
struct SmmDictEntryValue {
	void* value;
	PSmmDictEntryValue next;
};

typedef struct SmmDictEntry* PSmmDictEntry;
struct SmmDictEntry {
	const char* keyPart;
	size_t keyPartLength;
	PSmmDictEntryValue values;
	PSmmDictEntry children;
	PSmmDictEntry next;
};

typedef void* (*SmmElementCreateFunc)(const char* key, PSmmAllocator a, void* context);

struct SmmDict {
	bool storeKeyCopy; // True by default
	void* elemCreateFuncContext;
	SmmElementCreateFunc elemCreateFunc;
};
typedef struct SmmDict* PSmmDict;

PSmmDict smmCreateDict(PSmmAllocator allocator, void* elemCreateFuncContext, SmmElementCreateFunc createFunc);
PSmmDictEntry smmGetDictEntry(PSmmDict dict, const char* key, bool createIfMissing);
void* smmGetDictValue(PSmmDict dict, const char* key, bool createIfMissing);
void smmAddDictValue(PSmmDict dict, const char* key, void* value);
void smmPushDictValue(PSmmDict dict, const char* key, void* value);
void* smmPopDictValue(PSmmDict dict, const char* key);

PSmmAllocator smmCreatePermanentAllocator(const char* name, size_t size);
void smmFreePermanentAllocator(PSmmAllocator a);
void smmPrintAllocatorInfo(const PSmmAllocator allocator);

#endif
