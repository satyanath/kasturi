/* codegen.h - translates the checked AST to portable C99 */
#ifndef KASTURI_CODEGEN_H
#define KASTURI_CODEGEN_H

#include "ast.h"

/* Returns malloc'd C source. When main_fn is not NULL a C 'main' that
 * initialises the runtime and calls it is emitted. */
char *codegen_program(Program *prog, FuncDecl *main_fn, const char *source_name);

#endif
