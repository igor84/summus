#include "ibscommon.h"
#include "ibsallocator.h"
#include "ibsdictionary.h"
#include "smmmsgs.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmtypeinference.h"
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
		printf("Can't find %s in the current folder!\n", filename);
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
	bool pp1 = false;
	bool pp2 = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp("-pp1", argv[i]) == 0) pp1 = true;
		else if (strcmp("-pp2", argv[i]) == 0) pp2 = true;
	}
	PIbsAllocator a = ibsSimpleAllocatorCreate("test", 1024 * 1024);
	
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;

	PSmmAstNode module = loadModule("test.smm", &msgs, a);
	if (pp1) {
		smmExecuteGVPass(module, stdout);
	}
	smmExecuteTypeInferencePass(module, &msgs, a);
	if (pp2) {
		smmExecuteGVPass(module, stdout);
	}

	if (!pp1 && !pp2) {
		smmFlushMessages(&msgs);
	}
	if (smmHadErrors(&msgs)) {
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
