/* ast.c - type helpers and node constructors */
#include "ast.h"

#include <stdlib.h>
#include <string.h>

static Type t_error = {TY_ERROR, NULL, 0, NULL, NULL};
static Type t_void = {TY_VOID, NULL, 0, NULL, NULL};
static Type t_bool = {TY_BOOL, NULL, 0, NULL, NULL};
static Type t_char = {TY_CHAR, NULL, 0, NULL, NULL};
static Type t_int = {TY_INT, NULL, 0, NULL, NULL};
static Type t_long = {TY_LONG, NULL, 0, NULL, NULL};
static Type t_double = {TY_DOUBLE, NULL, 0, NULL, NULL};
static Type t_valist = {TY_VALIST, NULL, 0, NULL, NULL};
static Type t_nullptr = {TY_NULLPTR, NULL, 0, NULL, NULL};

Type *type_error(void) { return &t_error; }
Type *type_void(void) { return &t_void; }
Type *type_bool(void) { return &t_bool; }
Type *type_char(void) { return &t_char; }
Type *type_int(void) { return &t_int; }
Type *type_long(void) { return &t_long; }
Type *type_double(void) { return &t_double; }
Type *type_valist(void) { return &t_valist; }
Type *type_nullptr(void) { return &t_nullptr; }

Type *type_new(TypeKind kind) {
    switch (kind) {
    case TY_ERROR: return &t_error;
    case TY_VOID: return &t_void;
    case TY_BOOL: return &t_bool;
    case TY_CHAR: return &t_char;
    case TY_INT: return &t_int;
    case TY_LONG: return &t_long;
    case TY_DOUBLE: return &t_double;
    case TY_VALIST: return &t_valist;
    case TY_NULLPTR: return &t_nullptr;
    default: break;
    }
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = kind;
    return t;
}

Type *type_ptr(Type *base) {
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = TY_PTR;
    t->base = base;
    return t;
}

Type *type_array(Type *base, long long len, Expr *len_expr) {
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = TY_ARRAY;
    t->base = base;
    t->len = len;
    t->len_expr = len_expr;
    return t;
}

Type *type_struct(StructDef *s) {
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = TY_STRUCT;
    t->sdef = s;
    return t;
}

Type *type_copy(Type *t) {
    if (t->kind == TY_PTR) return type_ptr(type_copy(t->base));
    if (t->kind == TY_ARRAY) return type_array(type_copy(t->base), t->len, t->len_expr);
    return t;
}

bool type_equal(Type *a, Type *b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case TY_PTR: return type_equal(a->base, b->base);
    case TY_ARRAY: return a->len == b->len && type_equal(a->base, b->base);
    case TY_STRUCT: return a->sdef == b->sdef;
    default: return true;
    }
}

bool type_is_integer(Type *t) {
    return t->kind == TY_BOOL || t->kind == TY_CHAR || t->kind == TY_INT || t->kind == TY_LONG;
}

bool type_is_arith(Type *t) { return type_is_integer(t) || t->kind == TY_DOUBLE; }

bool type_is_scalar(Type *t) {
    return type_is_arith(t) || t->kind == TY_PTR || t->kind == TY_NULLPTR;
}

bool type_is_string(Type *t) {
    t = type_decay(t);
    return t->kind == TY_PTR && t->base->kind == TY_CHAR;
}

Type *type_decay(Type *t) {
    if (t->kind == TY_ARRAY) return type_ptr(t->base);
    return t;
}

static void type_str_rec(StrBuf *sb, Type *t) {
    switch (t->kind) {
    case TY_ERROR: sb_puts(sb, "<ದೋಷ>"); break;
    case TY_VOID: sb_puts(sb, "ಶೂನ್ಯ"); break;
    case TY_BOOL: sb_puts(sb, "ತಾರ್ಕಿಕ"); break;
    case TY_CHAR: sb_puts(sb, "ಅಕ್ಷರ"); break;
    case TY_INT: sb_puts(sb, "ಸಂಖ್ಯೆ"); break;
    case TY_LONG: sb_puts(sb, "ದೀರ್ಘ"); break;
    case TY_DOUBLE: sb_puts(sb, "ದಶಾಂಶ"); break;
    case TY_VALIST: sb_puts(sb, "ಚರಪಟ್ಟಿ"); break;
    case TY_NULLPTR: sb_puts(sb, "ಖಾಲಿ"); break;
    case TY_STRUCT: sb_puts(sb, t->sdef->name); break;
    case TY_PTR:
        if (t->base->kind == TY_CHAR) {
            sb_puts(sb, "ಪದ");
        } else {
            type_str_rec(sb, t->base);
            sb_putc(sb, '*');
        }
        break;
    case TY_ARRAY:
        type_str_rec(sb, t->base);
        if (t->len >= 0)
            sb_printf(sb, "[%lld]", t->len);
        else
            sb_puts(sb, "[]");
        break;
    }
}

char *type_str(Type *t) {
    StrBuf sb;
    sb_init(&sb);
    type_str_rec(&sb, t);
    return sb.data;
}

Expr *expr_new(ExprKind kind, Loc loc) {
    Expr *e = xcalloc(1, sizeof(Expr));
    e->kind = kind;
    e->loc = loc;
    return e;
}

Stmt *stmt_new(StmtKind kind, Loc loc) {
    Stmt *s = xcalloc(1, sizeof(Stmt));
    s->kind = kind;
    s->loc = loc;
    return s;
}
