#include "ibscommon.h"
#include "ibsallocator.h"

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
	
	void* testData = ibsAlloc(a, 4 * 1024 + 1023);
	assert(testData != NULL);

	fputs("\nAllocator info after 4 * 1024 + 1023 bytes alloc: ", stdout);
	ibsSimpleAllocatorPrintInfo(a);

	ibsSimpleAllocatorReset(a);
	fputs("\nAllocator info after reset: ", stdout);
	ibsSimpleAllocatorPrintInfo(a);

	ibsSimpleAllocatorFree(a);

	return EXIT_SUCCESS;
}
