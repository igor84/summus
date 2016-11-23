#include "ibscommon.h"
#include "ibsdictionary.h"
#include <string.h>

/********************************************************
Private
*********************************************************/

PIbsDictEntry createNewEntry(PIbsAllocator a, const char* key, void* value) {
	PIbsDictEntry newElem = ibsAlloc(a, sizeof(struct IbsDictEntry));
	newElem->keyPart = key;
	newElem->keyPartLength = strlen(key);
	newElem->values = ibsAlloc(a, sizeof(struct IbsDictEntryValue));
	newElem->values->value = value;
	return newElem;
}

/********************************************************
Public
*********************************************************/

PIbsDict ibsDictCreate(PIbsAllocator a) {
	PIbsDict dict = ibsAlloc(a, sizeof(struct IbsDict));
	dict->a = a;
	return dict;
}

PIbsDictEntry ibsDictGetEntry(PIbsDict dict, const char * key) {
	if (key == NULL || key[0] == 0) return NULL;
	if (dict->lastKey && (key == dict->lastKey || strcmp(key, dict->lastKey) == 0)) {
		return dict->lastEntry;
	}

	dict->lastKey = key;
	dict->lastEntry = NULL;

	PIbsDictEntry* el = &dict->entries;
	PIbsDictEntry entry;

	while (*el) {
		entry = *el;
		size_t i = 0;
		while (key[i] == entry->keyPart[i] && key[i] != 0 && entry->keyPartLength > i) {
			i++;
		}

		if (key[i] == 0) {
			if (entry->keyPartLength == i) {
				dict->lastEntry = entry;
				return entry;
			}
			return NULL;
		}
		if (i > 0 && i < entry->keyPartLength) {
			return NULL;
		}

		if (i == 0) {
			el = &entry->next;
		} else {
			PIbsDictEntry* nextField = &entry->children;
			while (*nextField && (*nextField)->keyPart[0] != key[i]) nextField = &(*nextField)->next;
			if (!*nextField) {
				return NULL;
			}
			key = &key[i];
			el = nextField;
		}
	}

	return NULL;
}

void* ibsDictGet(PIbsDict dict, const char * key) {
	PIbsDictEntry entry = ibsDictGetEntry(dict, key);
	if (entry == NULL || entry->values == NULL) return NULL;
	return entry->values->value;
}

void ibsDictPut(PIbsDict dict, const char* key, void* value) {
	if (key == NULL || key[0] == 0) return;

	PIbsDictEntry* el = &dict->entries;
	PIbsDictEntry entry;
	dict->lastKey = key;

	while (*el) {
		entry = *el;
		size_t i = 0;
		while (key[i] == entry->keyPart[i] && key[i] != 0 && entry->keyPartLength > i) {
			i++;
		}

		if (key[i] == 0 || (i > 0 && i < entry->keyPartLength)) {
			if (entry->keyPartLength == i) {
				// Existing key so create a value if it doesn't exist and return it
				if (!entry->values) {
					entry->values = ibsAlloc(dict->a, sizeof(struct IbsDictEntryValue));
				}
				entry->values->value = value;
				dict->lastEntry = entry;
				return;
			}
			// We got a key that is a part of existing key so we need to split existing into parts
			PIbsDictEntry newElem = ibsAlloc(dict->a, sizeof(struct IbsDictEntry));
			newElem->keyPart = &entry->keyPart[i];
			newElem->keyPartLength = entry->keyPartLength - i;
			newElem->values = entry->values;
			newElem->children = entry->children;
			entry->children = newElem;
			entry->keyPartLength = i;
			if (key[i] == 0) {
				entry->values = ibsAlloc(dict->a, sizeof(struct IbsDictEntryValue));
				entry->values->value = value;
				dict->lastEntry = entry;
				return;
			}
			entry->values = NULL;
			newElem = createNewEntry(dict->a, &key[i], value);
			newElem->next = entry->children;
			dict->lastEntry = newElem;
			entry->children = newElem;
			return;
		}
		if (entry->keyPartLength == i) {
			PIbsDictEntry* nextField = &entry->children;
			while (*nextField && (*nextField)->keyPart[0] != key[i]) nextField = &(*nextField)->next;
			if (!*nextField) {
				PIbsDictEntry newElem = createNewEntry(dict->a, &key[i], value);
				*nextField = newElem;
				dict->lastEntry = newElem;
				return;
			}
			key = &key[i];
			el = nextField;
		} else {
			el = &entry->next;
		}
	}

	entry = createNewEntry(dict->a, dict->lastKey, value);
	dict->lastEntry = entry;
	*el = entry;
}

void ibsDictPush(PIbsDict dict, const char* key, void* value) {
	PIbsDictEntry entry = ibsDictGetEntry(dict, key);
	if (!entry) {
		ibsDictPut(dict, key, value);
		return;
	}

	PIbsDictEntryValue newVal = ibsAlloc(dict->a, sizeof(struct IbsDictEntryValue));
	newVal->value = value;
	newVal->next = entry->values;
	entry->values = newVal;
}

void* ibsDictPop(PIbsDict dict, const char* key) {
	PIbsDictEntry entry = ibsDictGetEntry(dict, key);
	if (!entry || !entry->values) return NULL;

	PIbsDictEntryValue val = entry->values;
	entry->values = val->next;
	return val->value;
}
