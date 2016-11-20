#include "ibscommon.h"
#include "ibsallocator.h"
#include "ibsdictionary.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
	for (int i = 1; i < argc; i++) {
		printf("%s\n", argv[i]);
	}

	PIbsAllocator a = ibsSimpleAllocatorCreate("test", 1024 * 1024);
	fputs("\nAllocator info after create: ", stdout);
	ibsSimpleAllocatorPrintInfo(a);
	
	void* testData = ibsAlloc(a, 4 * 1024 + 50);
	assert(testData != NULL);

	PIbsDict dict = ibsDictCreate(a);
	ibsDictPut(dict, "asda", "fsdfsa");
	ibsDictPut(dict, "adsfssda", "fsdfsa");
	ibsDictPut(dict, "asgdsda", "fsdfsa");
	ibsDictPut(dict, "sdfgsasda", "fsdfsa");
	ibsDictPut(dict, "asdgfdsfa", "also found");
	ibsDictPut(dict, "dfgajgfsda", "fsdfsa");
	ibsDictPut(dict, "xcvbxcasda", "fsdfsa");
	ibsDictPut(dict, "jkhdfgasda", "found");
	ibsDictPut(dict, "jhgfcfasda", "fsdfsa");
	ibsDictPut(dict, "mnsbjasda", "fsdfsa");
	ibsDictPut(dict, "psdjhdfasda", "fsdfsa");
	ibsDictPut(dict, "uksflasda", "fsdfsa");

	ibsDictPush(dict, "PUSH", "first");
	ibsDictPush(dict, "PUSH", "second");

	char* firstPop = ibsDictPop(dict, "PUSH");
	char* secondPop = ibsDictPop(dict, "PUSH");
	char* elem1 = ibsDictGet(dict, "jkhdfgasda");
	char* elem2 = ibsDictGet(dict, "asdgfdsfa");

	printf("Pop first = %s\n", firstPop);
	printf("Pop second = %s\n", secondPop);
	printf("Get elem1 = %s\n", elem1);
	printf("Get elem2 = %s\n", elem2);

	fputs("\nAllocator info after dict allocs: ", stdout);
	ibsSimpleAllocatorPrintInfo(a);

	ibsSimpleAllocatorReset(a);
	fputs("\nAllocator info after reset: ", stdout);
	ibsSimpleAllocatorPrintInfo(a);

	ibsSimpleAllocatorFree(a);

	return EXIT_SUCCESS;
}
