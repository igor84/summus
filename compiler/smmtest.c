#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmsempass.h"
#include "smmllvmcodegen.h"
#include "smmdebugprinter.h"

#include <stdio.h>
#include <stdlib.h>

static PSmmAstNode loadModule(bool isFile, const char* filename, PSmmAllocator a) {
	char* buf = NULL;
	char filebuf[64 * 1024] = { 0 };
	if (isFile) {
		FILE* f = fopen(filename, "rb");
		if (!f) {
			printf("Can't find %s in the current folder!\n", filename);
			exit(EXIT_FAILURE);
		}
		fread(filebuf, 1, 64 * 1024, f);
		fclose(f);
		buf = filebuf;
	}
	PSmmLexer lex = smmCreateLexer(buf, filename, a);

	PSmmParser parser = smmCreateParser(lex, a);

	return smmParse(parser);
}

int main(int argc, char **argv) {
	const char* filename = "console";
	if (argc > 1) {
		filename = argv[1];
	}
	PSmmAllocator allocator = smmCreatePermanentAllocator(filename, 64 * 1024);
	PSmmAstNode module = loadModule(argc > 1, filename, allocator);

	puts("\n");
	smmExecuteDebugPrintPass(module, allocator);
	puts("\n");

	smmExecuteSemPass(module, allocator);

	puts("\n");
	smmExecuteDebugPrintPass(module, allocator);
	puts("\n");

	bool hadErrors = smmHadErrors();
	if (!hadErrors) {
		smmExecuteLLVMCodeGenPass(module, allocator);
	}
	
	smmPrintAllocatorInfo(allocator);
	
	return hadErrors;
}
