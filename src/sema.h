/* sema.h - name resolution and type checking */
#ifndef KASTURI_SEMA_H
#define KASTURI_SEMA_H

#include "ast.h"

/* Checks the whole program. Returns true when no errors were found.
 * When require_main is set the program must define 'ಮುಖ್ಯ'. */
bool sema_check(Program *prog, bool require_main);

/* The 'ಮುಖ್ಯ' function, or NULL. */
FuncDecl *sema_main_function(void);

/* Compile-time evaluation of arithmetic constant expressions. */
ConstVal const_eval(Expr *e);

#endif
