#include "smmlexer.h"

int main() {
	PSmmLexer lex = smmInitLexer(NULL);
	while (*lex->curChar != 0) {
		smmNextChar(lex);
		smmSkipWhitespaceFromStdIn(lex);
	}

	printf("\nScanned %lld\n", lex->scanCount);
}