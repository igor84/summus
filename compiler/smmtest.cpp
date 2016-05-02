#include "smmlexer.h"

static char filebuf[1024];

int main() {
	FILE* f = fopen("test.smm", "rb");
	if (!f) {
		printf("Failed opening file\n");
		return -1;
	}
	fread(filebuf, sizeof(char), 1023, f);
	fclose(f);
	PSmmLexer lex = smmInitLexer(filebuf);
	PSmmToken token = smmGetNextToken(lex);
	printf("\n");
	while (token->tokenType != smmEof) {
		printf("Got token %d with repr %.100s and intval=%lld\n", token->tokenType, token->repr, token->intVal);
		token = smmGetNextToken(lex);
	}

	printf("\nScanned %lld chars\n", lex->scanCount);

	return 0;
}