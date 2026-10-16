/* ast.h - abstract syntax tree and types */
#ifndef KASTURI_AST_H
#define KASTURI_AST_H

#include <stdbool.h>

#include "diag.h"
#include "util.h"

typedef enum TypeKind {
    TY_ERROR,   /* result of an earlier error; compatible with everything */
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_INT,
    TY_LONG,
    TY_DOUBLE,
    TY_PTR,
    TY_ARRAY,
    TY_STRUCT,
    TY_VALIST,
    TY_NULLPTR /* type of 'ಖಾಲಿ' */
} TypeKind;

typedef struct Type Type;
typedef struct StructDef StructDef;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Symbol Symbol;

struct Type {
    TypeKind kind;
    Type *base;      /* TY_PTR, TY_ARRAY */
    long long len;   /* TY_ARRAY: element count, -1 = not yet known */
    Expr *len_expr;  /* TY_ARRAY: size expression written in the source */
    StructDef *sdef; /* TY_STRUCT */
};

typedef struct Field {
    char *name;
    char *cname;
    Type *type;
    Loc loc;
} Field;

struct StructDef {
    char *name;
    char *cname;
    Loc loc;
    Vec fields;      /* Field* */
    bool declared;   /* a 'ರಚನೆ X ...;' declaration was seen */
    bool complete;   /* has a body */
    int mark;        /* used for cycle detection / ordering */
};

typedef struct ConstVal {
    bool ok;
    bool is_float;
    long long i;
    double f;
} ConstVal;

typedef enum ExprKind {
    E_INT,
    E_FLOAT,
    E_STR,
    E_CHAR,
    E_BOOL,
    E_NULL,
    E_VAR,
    E_UNARY,
    E_BINARY,
    E_COND,
    E_CALL,
    E_INDEX,
    E_MEMBER,
    E_CAST,
    E_SIZEOF_TYPE,
    E_SIZEOF_EXPR,
    E_ADDR,
    E_DEREF,
    E_VA_START,
    E_VA_ARG,
    E_VA_END,
    E_VA_COPY,
    E_INITLIST
} ExprKind;

struct Expr {
    ExprKind kind;
    Loc loc;
    Type *type;          /* set by semantic analysis */
    long long ival;      /* E_INT, E_CHAR, E_BOOL */
    char *text;          /* E_FLOAT spelling, E_STR bytes */
    size_t len;          /* E_STR length */
    bool wide;           /* E_CHAR with a non-ASCII code point */
    int op;              /* operator (TokKind) for unary / binary */
    Expr *a, *b, *c;
    Vec args;            /* E_CALL arguments, E_INITLIST elements */
    char *name;          /* E_VAR, E_MEMBER, E_CALL callee name */
    Symbol *sym;
    Field *field;
    bool arrow;          /* E_MEMBER uses '->' */
    bool implicit;       /* E_CAST inserted by the compiler */
    bool str_compare;    /* E_BINARY comparing string contents */
    Type *to_type;       /* E_CAST, E_SIZEOF_TYPE, E_VA_ARG */
    char *va_last;       /* E_VA_START: C name of the last named parameter */
    bool folded;         /* replaced by a compile-time constant */
    ConstVal cv;
};

typedef enum StmtKind {
    S_BLOCK,
    S_DECL,
    S_ASSIGN,
    S_INCDEC,
    S_EXPR,
    S_IF,
    S_WHILE,
    S_DOWHILE,
    S_FOR,
    S_SWITCH,
    S_BREAK,
    S_CONTINUE,
    S_RETURN,
    S_PRINT,
    S_READ,
    S_EMPTY
} StmtKind;

typedef struct VarDecl {
    char *name;
    Loc loc;
    Type *type;
    Expr *init;
    bool is_const;
    bool is_global;
    bool is_extern;
    char *cname;
    Symbol *sym;
} VarDecl;

typedef struct SwitchCase {
    Loc loc;
    Expr *value; /* NULL for 'ಪೂರ್ವನಿಯೋಜಿತ' */
    long long cval;
    Vec stmts;   /* Stmt* */
} SwitchCase;

struct Stmt {
    StmtKind kind;
    Loc loc;
    Vec stmts;          /* S_BLOCK */
    Vec decls;          /* S_DECL: VarDecl* */
    Expr *lhs, *rhs;    /* S_ASSIGN, S_INCDEC (lhs) */
    Expr *cond;         /* S_IF, S_WHILE, S_DOWHILE, S_FOR, S_SWITCH */
    Expr *expr;         /* S_EXPR, S_RETURN */
    int op;             /* assignment / inc-dec operator */
    bool prefix;        /* S_INCDEC */
    bool newline;       /* S_PRINT: ಸಾಲುಮುದ್ರಿಸು */
    Stmt *then, *els;   /* S_IF */
    Stmt *body;         /* loops */
    Vec init, update;   /* S_FOR: Stmt* */
    Vec cases;          /* S_SWITCH: SwitchCase* */
    Vec args;           /* S_PRINT, S_READ: Expr* */
};

typedef struct Param {
    char *name;
    Loc loc;
    Type *type;
    bool is_const;
    Symbol *sym;
} Param;

typedef struct FuncDecl {
    char *name;
    Loc loc;
    Vec params;      /* Param* */
    Type *ret;
    bool variadic;
    bool is_extern;
    char *cname;
    Stmt *body;
    Symbol *sym;
} FuncDecl;

typedef enum DeclKind { D_STRUCT, D_FUNC, D_VAR } DeclKind;

typedef struct TopDecl {
    DeclKind kind;
    StructDef *sdef;
    FuncDecl *func;
    VarDecl *var;
} TopDecl;

typedef struct Program {
    Vec decls; /* TopDecl* in source order (imports first) */
} Program;

typedef enum SymKind { SYM_VAR, SYM_FUNC } SymKind;

struct Symbol {
    SymKind kind;
    char *name;
    char *cname;
    Type *type;
    bool is_const;
    bool is_global;
    VarDecl *var;
    FuncDecl *func;
    int evaluating; /* guards recursive constant evaluation */
};

/* types */
Type *type_error(void);
Type *type_void(void);
Type *type_bool(void);
Type *type_char(void);
Type *type_int(void);
Type *type_long(void);
Type *type_double(void);
Type *type_valist(void);
Type *type_nullptr(void);
Type *type_new(TypeKind kind);
Type *type_ptr(Type *base);
Type *type_array(Type *base, long long len, Expr *len_expr);
Type *type_struct(StructDef *s);
Type *type_copy(Type *t);

bool type_equal(Type *a, Type *b);
bool type_is_integer(Type *t);
bool type_is_arith(Type *t);
bool type_is_scalar(Type *t);
bool type_is_string(Type *t); /* ಪದ == ಅಕ್ಷರ* */
Type *type_decay(Type *t);
char *type_str(Type *t);       /* readable Kannada name */

Expr *expr_new(ExprKind kind, Loc loc);
Stmt *stmt_new(StmtKind kind, Loc loc);

#endif
