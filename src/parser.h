/* parser.h - recursive descent parser */
#ifndef KASTURI_PARSER_H
#define KASTURI_PARSER_H

#include <setjmp.h>

#include "ast.h"
#include "lexer.h"

typedef struct Parser {
    Token *toks;
    int ntok;
    int pos;
    jmp_buf recover;
} Parser;

void parser_init(Parser *p, Token *toks, int ntok);

/* Records every name that follows 'ರಚನೆ' so that struct names can be used as
 * types before (and without) the 'ರಚನೆ' keyword. */
void parser_register_structs(Token *toks, int ntok);

StructDef *struct_lookup(const char *name);
Vec *struct_list(void);

/* Parses the leading 'ಆಮದು "..." ;' statements. Pushes Token* of the
 * string literals into 'imports'. */
void parse_imports(Parser *p, Vec *imports);

/* Parses the rest of the file and appends declarations to prog. */
void parse_file_body(Parser *p, Program *prog);

#endif
