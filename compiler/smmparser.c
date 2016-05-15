#include <stdint.h>
#include <stdio.h>

#include "smmutil.h"
#include "smmmsgs.h"
#include "smmlexer.h"
#include "smmparser.h"

// Disable warning that we are using anonimous unions in structs
#pragma warning(disable : 4201)

#define SMM_PARSER_IDENTS_DICT_SIZE 8 * 1024

/********************************************************
Type Definitions
*********************************************************/

static char* nodeKindToString[] = {
	"error", "Program", ":= ", "Symbol ", "", "", "", "",
	"+ ", "+. ",
	"- ", "-. ",
	"* ", "*. ",
	"udiv ", "sdiv ", "/ ",
	"umod ", "smod ", "% ",
	"-", "int ", "float "
};

static struct SmmAstNode errorNode = { nkSmmError };

/********************************************************
Private Functions
*********************************************************/

#define newSmmAstNode() parser->allocator->alloc(parser->allocator, sizeof(struct SmmAstNode))

PSmmAstNode parseExpression(PSmmParser parser);

void getNextToken(PSmmParser parser) {
	parser->lastToken = parser->curToken;
	parser->curToken = smmGetNextToken(parser->lex);
}

bool findToken(PSmmParser parser, SmmTokenType tokenType) {
	PSmmToken curToken = parser->curToken;
	while (curToken->type != tokenType && curToken->type != ';' && curToken->type != ttSmmEof) {
		parser->lastToken = curToken;
		curToken = smmGetNextToken(parser->lex);
	}
	bool found = curToken->type == tokenType;
	parser->curToken = curToken;
	if (found) getNextToken(parser);
	return found;
}

PSmmToken expect(PSmmParser parser, SmmTokenType type) {
	PSmmToken token = parser->curToken;
	if (token->type != type) {
		if (token->type != ttSmmErr) {
			// If it is smmErr lexer already reported the error
			char expBuf[4], gotBuf[4];
			char* expected = smmTokenTypeToString(type, expBuf);
			char* got = smmTokenTypeToString(token->type, gotBuf);
			if (token->isFirstOnLine && parser->lastToken) {
				smmPostMessage(errSmmNoExpectedToken, parser->lex->fileName, parser->lastToken->filePos, expected);
				parser->lastErrorLine = parser->lastToken->filePos.lineNumber;
			} else {
				smmPostMessage(errSmmGotUnexpectedToken, parser->lex->fileName, token->filePos, expected, got);
				parser->lastErrorLine = token->filePos.lineNumber;
			}
		}
		return NULL;
	}
	getNextToken(parser);
	return token;
}

PSmmAstNode parseFactor(PSmmParser parser) {
	PSmmAstNode res = &errorNode;
	if (parser->curToken->type == '(') {
		getNextToken(parser);
		res = parseExpression(parser);
		expect(parser, ')');
	} else {
		bool reportedError = parser->lastErrorLine == parser->curToken->filePos.lineNumber;
		SmmAstNodeKind kind;
		do {
			kind = nkSmmError;
			switch (parser->curToken->type) {
			case ttSmmIdent:   kind = nkSmmSymbol; break;
			case ttSmmInteger: kind = nkSmmInt;    break;
			case ttSmmFloat:   kind = nkSmmFloat;  break;
			default: break;
			}
			if (kind != nkSmmError) {
				res = newSmmAstNode();
				res->kind = kind;
				res->token = parser->curToken;
			} else if (!reportedError) {
				char gotBuf[4];
				char* got = smmTokenTypeToString(parser->curToken->type, gotBuf);
				smmPostMessage(errSmmGotUnexpectedToken, parser->lex->fileName, parser->curToken->filePos, "symbol or literal", got);
			}
			getNextToken(parser);
		} while (kind == nkSmmError && parser->curToken->type != ';' && parser->curToken->type != ttSmmEof);
	}
	return res;
}

PSmmAstNode parseTerm(PSmmParser parser) {
	PSmmAstNode term1 = parseFactor(parser);
	while (parser->curToken->type == '*' || parser->curToken->type == '/' || parser->curToken->type == ttSmmIntDiv) {
		SmmAstNodeKind kind = nkSmmMul;
		if (parser->curToken->type == '/') {
			kind = nkSmmFDiv;
		} else if (parser->curToken->type == ttSmmIntDiv) {
			kind = nkSmmSDiv;
		}
		getNextToken(parser);
		PSmmAstNode term2 = parseFactor(parser);
		PSmmAstNode res = newSmmAstNode();
		res->kind = kind;
		res->left = term1;
		res->right = term2;
		term1 = res;
	};
	return term1;
}

PSmmAstNode parseExpression(PSmmParser parser) {
	bool doNeg = false;
	if (parser->curToken->type == '-') {
		doNeg = true;
		getNextToken(parser);
	} else if (parser->curToken->type == '+') {
		getNextToken(parser);
	}
	PSmmAstNode term1 = parseTerm(parser);
	if (doNeg) {
		PSmmAstNode neg = newSmmAstNode();
		neg->kind = nkSmmNeg;
		neg->left = term1;
		term1 = neg;
	}
	while (parser->curToken->type == '-' || parser->curToken->type == '+') {
		SmmAstNodeKind kind = nkSmmAdd;
		if (parser->curToken->type == '-') {
			kind = nkSmmSub;
		}
		getNextToken(parser);
		PSmmAstNode term2 = parseTerm(parser);
		PSmmAstNode res = newSmmAstNode();
		res->kind = kind;
		res->left = term1;
		res->right = term2;
		term1 = res;
	};
	return term1;
}

PSmmAstNode parseStatement(PSmmParser parser) {
	PSmmAstNode lval = parseExpression(parser);
	if (parser->curToken->type == '=') {
		if (lval->kind != nkSmmSymbol) {
			lval = &errorNode;
			if (parser->lastErrorLine != parser->curToken->filePos.lineNumber) {
				smmPostMessage(errSmmOperandMustBeLVal, parser->lex->fileName, parser->curToken->filePos);
			}
		}
		getNextToken(parser);
		PSmmAstNode val = parseExpression(parser);
		if (lval == &errorNode || val == &errorNode) return &errorNode;
		PSmmAstNode assignment = newSmmAstNode();
		assignment->kind = nkSmmDeclAssignment;
		assignment->left = lval;
		assignment->right = val;
		return assignment;
	}
	
	return lval;
}

void printNode(PSmmAstNode node) {
	fputs(nodeKindToString[node->kind], stdout);
	if (node->left) printNode(node->left);
	if (node->right) printNode(node->right);
	if (node->next) {
		puts("");
		printNode(node->next);
	}
}

/********************************************************
API Functions
*********************************************************/

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator) {
	PSmmParser parser = (PSmmParser)allocator->alloc(allocator, sizeof(struct SmmParser));
	parser->lex = lex;
	parser->curToken = smmGetNextToken(lex);
	parser->idents = smmCreateDict(allocator, SMM_PARSER_IDENTS_DICT_SIZE, NULL, NULL);
	parser->allocator = allocator;
	return parser;
}


void smmParse(PSmmParser parser) {
	PSmmAstNode program = newSmmAstNode();
	program->kind = nkSmmProgram;
	PSmmAstNode lastStmt = program;
	
	while (parser->curToken->type != ttSmmEof) {
		PSmmAstNode curStmt = parseStatement(parser);
		lastStmt->next = curStmt;
		expect(parser, (SmmTokenType)';');
		lastStmt = curStmt;
	}

	printNode(program);
	puts("");
}