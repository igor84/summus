#include "ibscommon.h"
#include "ibsallocator.h"
#include "ibsdictionary.h"
#include "smmmsgs.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmtypeinference.h"
#include "smmsempass.h"
#include "smmllvmcodegen.h"
#include "../utility/smmgvpass.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static PSmmAstNode loadModule(const char* filename, PSmmMsgs msgs, PIbsAllocator a) {
	char* buf = NULL;
	char filebuf[64 * 1024] = { 0 };
	FILE* f = fopen(filename, "rb");
	if (!f) {
		printf("Can't find %s !\n", filename);
		exit(EXIT_FAILURE);
	}
	fread(filebuf, 1, 64 * 1024, f);
	fclose(f);
	buf = filebuf;
	PSmmLexer lex = smmCreateLexer(buf, filename, msgs, a);

	PSmmParser parser = smmCreateParser(lex, msgs, a);

	return smmParse(parser);
}

int main(int argc, char* argv[]) {
	bool pp[3] = { false };
	const char* inFile = NULL;
	const char* outFile = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp("-pp1", argv[i]) == 0) pp[0] = true;
		else if (strcmp("-pp2", argv[i]) == 0) pp[1] = true;
		else if (strcmp("-pp3", argv[i]) == 0) pp[2] = true;
		else if (strcmp("-o", argv[i]) == 0) {
			i++;
			if (i < argc) outFile = argv[i];
		} else if (argv[i][0] == '-') {
			printf("ERROR: Got unknown parameter %s\n", argv[i]);
			return EXIT_FAILURE;
		} else if (!inFile) {
			inFile = argv[i];
		} else {
			printf("ERROR: Got extra parameter %s\n", argv[i]);
		}
	}
	if (inFile == NULL) {
		printf("ERROR: File to compile not given\n");
		return EXIT_FAILURE;
	}
	PIbsAllocator a = ibsSimpleAllocatorCreate("main", 1024 * 1024);
	
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;

	PSmmAstNode module = loadModule(inFile, &msgs, a);

	FILE* out = stdout;
	if (outFile) {
		out = fopen(outFile, "wb");
		if (!out) {
			printf("ERROR: Failed to open %s for writing!\n", outFile);
		}
	}

	if (pp[0]) {
		smmExecuteGVPass(module, out);
		return EXIT_SUCCESS;
	}
	smmExecuteTypeInferencePass(module, &msgs, a);
	if (pp[1]) {
		smmExecuteGVPass(module, out);
		return EXIT_SUCCESS;
	}

	smmExecuteSemPass(module, &msgs, a);
	if (pp[2]) {
		smmExecuteGVPass(module, out);
		return EXIT_SUCCESS;
	}

	smmFlushMessages(&msgs);

	if (smmHadErrors(&msgs)) {
		return EXIT_FAILURE;
	}

	if (smmExecuteLLVMCodeGenPass(module, out, a)) {
		if (outFile) printf("\nModule saved to %s\n", outFile);
		return EXIT_SUCCESS;
	}

	printf("\nERROR: Module compilation failed!\n");
	return EXIT_FAILURE;
}
