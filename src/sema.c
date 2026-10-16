/* sema.c - scopes, type checking, implicit conversions and constant folding */
#include "sema.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "mangle.h"
#include "parser.h"

#define MAIN_NAME "ಮುಖ್ಯ"

typedef struct Scope {
    struct Scope *parent;
    Vec syms;
} Scope;

static Scope g_global;
static Scope *scope = &g_global;
static FuncDecl *cur_fn;
static int loop_depth;
static int switch_depth;
static FuncDecl *g_main;

FuncDecl *sema_main_function(void) { return g_main; }

/* ------------------------------------------------------------------ */
/* scopes                                                              */
/* ------------------------------------------------------------------ */

static Symbol *scope_find(Scope *s, const char *name) {
    for (int i = s->syms.len - 1; i >= 0; i--) {
        Symbol *y = s->syms.items[i];
        if (strcmp(y->name, name) == 0) return y;
    }
    return NULL;
}

static Symbol *lookup(const char *name) {
    for (Scope *s = scope; s; s = s->parent) {
        Symbol *y = scope_find(s, name);
        if (y) return y;
    }
    return NULL;
}

static void push_scope(void) {
    Scope *s = xcalloc(1, sizeof(Scope));
    s->parent = scope;
    scope = s;
}

static void pop_scope(void) { scope = scope->parent; }

static void declare(Symbol *sym, Loc l) {
    if (scope_find(scope, sym->name)) {
        diag_error(l, "'%s' ಅನ್ನು ಈ ವ್ಯಾಪ್ತಿಯಲ್ಲಿ ಈಗಾಗಲೇ ಘೋಷಿಸಲಾಗಿದೆ", sym->name);
        return;
    }
    vec_push(&scope->syms, sym);
}

/* ------------------------------------------------------------------ */
/* constant evaluation                                                 */
/* ------------------------------------------------------------------ */

static ConstVal cv_none(void) {
    ConstVal c;
    memset(&c, 0, sizeof c);
    return c;
}

static ConstVal cv_int(long long v) {
    ConstVal c = cv_none();
    c.ok = true;
    c.i = v;
    return c;
}

static ConstVal cv_float(double v) {
    ConstVal c = cv_none();
    c.ok = true;
    c.is_float = true;
    c.f = v;
    return c;
}

static double cv_as_double(ConstVal c) { return c.is_float ? c.f : (double)c.i; }
static bool cv_truth(ConstVal c) { return c.is_float ? c.f != 0 : c.i != 0; }

static ConstVal cv_convert(ConstVal c, Type *t) {
    if (!c.ok) return c;
    if (c.is_float && t->kind != TY_DOUBLE && t->kind != TY_BOOL) {
        if (!(fabs(c.f) < 9.2e18)) return cv_none();
        c = cv_int((long long)c.f);
    }
    switch (t->kind) {
    case TY_DOUBLE: return cv_float(cv_as_double(c));
    case TY_BOOL: return cv_int(cv_truth(c));
    case TY_CHAR: return cv_int((signed char)(c.i & 0xFF));
    case TY_INT: return cv_int((int)(unsigned int)(c.i & 0xFFFFFFFFLL));
    case TY_LONG: return cv_int(c.i);
    default: return cv_none();
    }
}

ConstVal const_eval(Expr *e) {
    if (!e) return cv_none();
    if (e->folded) return e->cv;
    switch (e->kind) {
    case E_INT:
    case E_CHAR:
    case E_BOOL:
        return cv_int(e->ival);
    case E_FLOAT:
        return cv_float(strtod(e->text, NULL));
    case E_VAR: {
        Symbol *s = e->sym ? e->sym : lookup(e->name);
        if (!s || s->kind != SYM_VAR || !s->is_const || !s->var || !s->var->init || s->evaluating)
            return cv_none();
        if (!type_is_arith(s->type)) return cv_none();
        s->evaluating = 1;
        ConstVal c = const_eval(s->var->init);
        s->evaluating = 0;
        return cv_convert(c, s->type);
    }
    case E_UNARY: {
        ConstVal a = const_eval(e->a);
        if (!a.ok) return a;
        switch (e->op) {
        case P_MINUS:
            return a.is_float ? cv_float(-a.f) : cv_int((long long)(0ULL - (unsigned long long)a.i));
        case P_PLUS: return a;
        case P_BANG: return cv_int(!cv_truth(a));
        case P_TILDE: return a.is_float ? cv_none() : cv_int(~a.i);
        default: return cv_none();
        }
    }
    case E_BINARY: {
        if (e->str_compare) return cv_none();
        ConstVal a = const_eval(e->a);
        ConstVal b = const_eval(e->b);
        if (!a.ok || !b.ok) return cv_none();
        int op = e->op;
        if (op == P_ANDAND) return cv_int(cv_truth(a) && cv_truth(b));
        if (op == P_OROR) return cv_int(cv_truth(a) || cv_truth(b));
        if (a.is_float || b.is_float) {
            double x = cv_as_double(a), y = cv_as_double(b);
            switch (op) {
            case P_PLUS: return cv_float(x + y);
            case P_MINUS: return cv_float(x - y);
            case P_STAR: return cv_float(x * y);
            case P_SLASH: return cv_float(x / y); /* IEEE: x/0 gives inf or nan */
            case P_EQ: return cv_int(x == y);
            case P_NE: return cv_int(x != y);
            case P_LT: return cv_int(x < y);
            case P_GT: return cv_int(x > y);
            case P_LE: return cv_int(x <= y);
            case P_GE: return cv_int(x >= y);
            default: return cv_none();
            }
        }
        long long x = a.i, y = b.i;
        unsigned long long ux = (unsigned long long)x, uy = (unsigned long long)y;
        switch (op) {
        case P_PLUS: return cv_int((long long)(ux + uy));
        case P_MINUS: return cv_int((long long)(ux - uy));
        case P_STAR: return cv_int((long long)(ux * uy));
        case P_SLASH:
            if (y == 0 || (x == LLONG_MIN && y == -1)) return cv_none();
            return cv_int(x / y);
        case P_PERCENT:
            if (y == 0 || (x == LLONG_MIN && y == -1)) return cv_none();
            return cv_int(x % y);
        case P_SHL:
            if (y < 0 || y > 63) return cv_none();
            return cv_int((long long)(ux << y));
        case P_SHR:
            if (y < 0 || y > 63) return cv_none();
            return cv_int(x >> y);
        case P_AMP: return cv_int(x & y);
        case P_PIPE: return cv_int(x | y);
        case P_CARET: return cv_int(x ^ y);
        case P_EQ: return cv_int(x == y);
        case P_NE: return cv_int(x != y);
        case P_LT: return cv_int(x < y);
        case P_GT: return cv_int(x > y);
        case P_LE: return cv_int(x <= y);
        case P_GE: return cv_int(x >= y);
        default: return cv_none();
        }
    }
    case E_COND: {
        ConstVal c = const_eval(e->a);
        if (!c.ok) return c;
        return const_eval(cv_truth(c) ? e->b : e->c);
    }
    case E_CAST:
        if (!e->to_type || !type_is_arith(e->to_type)) return cv_none();
        return cv_convert(const_eval(e->a), e->to_type);
    case E_SIZEOF_TYPE:
        switch (e->to_type->kind) {
        case TY_CHAR:
        case TY_BOOL: return cv_int(1);
        case TY_INT: return cv_int(4);
        case TY_LONG:
        case TY_DOUBLE: return cv_int(8);
        default: return cv_none();
        }
    default:
        return cv_none();
    }
}

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

static bool is_complete(Type *t) {
    switch (t->kind) {
    case TY_VOID: return false;
    case TY_STRUCT: return t->sdef->complete;
    case TY_ARRAY: return t->len >= 0 && is_complete(t->base);
    default: return true;
    }
}

static void resolve_type(Type *t, Loc l) {
    switch (t->kind) {
    case TY_PTR:
        resolve_type(t->base, l);
        break;
    case TY_ARRAY:
        resolve_type(t->base, l);
        if (t->len_expr && t->len < 0) {
            ConstVal c = const_eval(t->len_expr);
            if (!c.ok || c.is_float) {
                diag_error(t->len_expr->loc, "ಸರಣಿಯ ಗಾತ್ರವು ಸ್ಥಿರ ಪೂರ್ಣಾಂಕವಾಗಿರಬೇಕು");
                t->len = 1;
            } else if (c.i <= 0) {
                diag_error(t->len_expr->loc, "ಸರಣಿಯ ಗಾತ್ರವು ಧನಾತ್ಮಕವಾಗಿರಬೇಕು");
                t->len = 1;
            } else {
                t->len = c.i;
            }
        }
        if (t->base->kind == TY_VOID || t->base->kind == TY_VALIST) {
            diag_error(l, "'%s' ಅಂಶಗಳ ಸರಣಿ ಸಾಧ್ಯವಿಲ್ಲ", type_str(t->base));
            t->base = type_int();
        } else if (t->base->kind == TY_ARRAY && t->base->len < 0) {
            diag_error(l, "ಒಳಗಿನ ಸರಣಿಯ ಗಾತ್ರ ಬೇಕು");
            t->base->len = 1;
        }
        break;
    case TY_STRUCT:
        if (!t->sdef->declared) {
            diag_error(l, "ರಚನೆ '%s' ಅನ್ನು ಘೋಷಿಸಲಾಗಿಲ್ಲ", t->sdef->name);
            t->sdef->declared = true;
        }
        break;
    default:
        break;
    }
}

static Type *promote(Type *t) {
    if (t->kind == TY_BOOL || t->kind == TY_CHAR) return type_int();
    return t;
}

static Type *arith_result(Type *a, Type *b) {
    if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE) return type_double();
    if (a->kind == TY_LONG || b->kind == TY_LONG) return type_long();
    return type_int();
}

static Expr *make_cast(Expr *e, Type *to) {
    Expr *c = expr_new(E_CAST, e->loc);
    c->a = e;
    c->to_type = to;
    c->type = to;
    c->implicit = true;
    return c;
}

/* Implicit conversion of an already checked expression to 'to'. */
static Expr *coerce(Expr *e, Type *to, const char *context) {
    Type *from = type_decay(e->type);
    if (from->kind == TY_ERROR || to->kind == TY_ERROR) return e;
    if (type_equal(from, to)) return e;
    if (to->kind == TY_BOOL && type_is_scalar(from)) return make_cast(e, to);
    if (type_is_arith(to) && type_is_arith(from)) return make_cast(e, to);
    if (to->kind == TY_PTR) {
        if (from->kind == TY_NULLPTR) return make_cast(e, to);
        if (from->kind == TY_PTR && (to->base->kind == TY_VOID || from->base->kind == TY_VOID))
            return make_cast(e, to);
    }
    char *a = type_str(from), *b = type_str(to);
    diag_error(e->loc, "%s: '%s' ಪ್ರಕಾರವನ್ನು '%s' ಪ್ರಕಾರಕ್ಕೆ ಪರಿವರ್ತಿಸಲು ಸಾಧ್ಯವಿಲ್ಲ", context, a, b);
    free(a);
    free(b);
    return e;
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static Type *check_expr(Expr *e);
static Expr *check_initializer(Type *t, Expr *init);

static Type *set_type(Expr *e, Type *t) {
    e->type = t;
    return t;
}

static Type *rvalue(Expr *e) {
    Type *t = check_expr(e);
    if (t->kind == TY_VOID) {
        diag_error(e->loc, "ಶೂನ್ಯ (void) ಫಲಿತಾಂಶವನ್ನು ಮೌಲ್ಯವಾಗಿ ಬಳಸಲಾಗದು");
        return set_type(e, type_error());
    }
    return type_decay(t);
}

static bool is_lvalue(Expr *e) {
    switch (e->kind) {
    case E_VAR: return e->sym && e->sym->kind == SYM_VAR;
    case E_INDEX:
    case E_DEREF: return true;
    case E_MEMBER: return e->arrow || is_lvalue(e->a);
    default: return false;
    }
}

static Symbol *const_root(Expr *e) {
    switch (e->kind) {
    case E_VAR: return (e->sym && e->sym->is_const) ? e->sym : NULL;
    case E_INDEX: return e->a->type && e->a->type->kind == TY_ARRAY ? const_root(e->a) : NULL;
    case E_MEMBER: return e->arrow ? NULL : const_root(e->a);
    default: return NULL;
    }
}

static bool require_lvalue(Expr *e, const char *what) {
    if (e->type && e->type->kind == TY_ERROR) return false;
    if (!is_lvalue(e)) {
        diag_error(e->loc, "%s ಚರ (ಬದಲಾಯಿಸಬಹುದಾದ ಸ್ಥಳ) ಆಗಿರಬೇಕು", what);
        return false;
    }
    Symbol *c = const_root(e);
    if (c) {
        diag_error(e->loc, "ಸ್ಥಿರ '%s' ಅನ್ನು ಬದಲಾಯಿಸಲಾಗದು", c->name);
        return false;
    }
    if (e->type->kind == TY_ARRAY) {
        diag_error(e->loc, "ಸರಣಿಯನ್ನು ಒಟ್ಟಾಗಿ ನಿಯೋಜಿಸಲಾಗದು; ಅಂಶಗಳನ್ನು ಒಂದೊಂದಾಗಿ ನಕಲಿಸಿ");
        return false;
    }
    return true;
}

static bool ptr_compatible(Type *a, Type *b) {
    if (a->kind == TY_NULLPTR || b->kind == TY_NULLPTR) return true;
    if (a->kind != TY_PTR || b->kind != TY_PTR) return false;
    return type_equal(a, b) || a->base->kind == TY_VOID || b->base->kind == TY_VOID;
}

static const char *op_text(int op) { return tok_spelling((TokKind)op); }

static Type *check_binary(Expr *e) {
    Type *a = rvalue(e->a);
    Type *b = rvalue(e->b);
    if (a->kind == TY_ERROR || b->kind == TY_ERROR) return set_type(e, type_error());
    int op = e->op;
    switch (op) {
    case P_ANDAND:
    case P_OROR:
        if (!type_is_scalar(a) || !type_is_scalar(b)) break;
        return set_type(e, type_bool());
    case P_EQ:
    case P_NE:
    case P_LT:
    case P_GT:
    case P_LE:
    case P_GE:
        if (type_is_arith(a) && type_is_arith(b)) return set_type(e, type_bool());
        if (type_is_string(a) && type_is_string(b)) {
            e->str_compare = true;
            return set_type(e, type_bool());
        }
        if ((op == P_EQ || op == P_NE) && ptr_compatible(a, b)) return set_type(e, type_bool());
        if (a->kind == TY_PTR && type_equal(a, b)) return set_type(e, type_bool());
        break;
    case P_PLUS:
        if (type_is_arith(a) && type_is_arith(b)) return set_type(e, arith_result(a, b));
        if (a->kind == TY_PTR && type_is_integer(b) && a->base->kind != TY_VOID) return set_type(e, a);
        if (b->kind == TY_PTR && type_is_integer(a) && b->base->kind != TY_VOID) return set_type(e, b);
        break;
    case P_MINUS:
        if (type_is_arith(a) && type_is_arith(b)) return set_type(e, arith_result(a, b));
        if (a->kind == TY_PTR && type_is_integer(b) && a->base->kind != TY_VOID) return set_type(e, a);
        if (a->kind == TY_PTR && type_equal(a, b) && a->base->kind != TY_VOID) return set_type(e, type_long());
        break;
    case P_STAR:
    case P_SLASH:
        if (type_is_arith(a) && type_is_arith(b)) {
            Type *r = arith_result(a, b);
            if (op == P_SLASH && type_is_integer(r)) {
                ConstVal d = const_eval(e->b);
                if (d.ok && !d.is_float && d.i == 0) diag_error(e->b->loc, "ಸೊನ್ನೆಯಿಂದ ಭಾಗಿಸಲಾಗದು");
            }
            return set_type(e, r);
        }
        break;
    case P_PERCENT:
        if (type_is_integer(a) && type_is_integer(b)) {
            ConstVal d = const_eval(e->b);
            if (d.ok && !d.is_float && d.i == 0) diag_error(e->b->loc, "ಸೊನ್ನೆಯಿಂದ ಶೇಷ ಪಡೆಯಲಾಗದು");
        }
        /* fall through */
    case P_AMP:
    case P_PIPE:
    case P_CARET:
        if (type_is_integer(a) && type_is_integer(b)) return set_type(e, arith_result(a, b));
        break;
    case P_SHL:
    case P_SHR:
        if (type_is_integer(a) && type_is_integer(b)) return set_type(e, promote(a));
        break;
    default:
        break;
    }
    char *sa = type_str(a), *sb = type_str(b);
    diag_error(e->loc, "'%s' ಮತ್ತು '%s' ಪ್ರಕಾರಗಳ ಮೇಲೆ '%s' ಕಾರ್ಯಾಚರಣೆ ಸಾಧ್ಯವಿಲ್ಲ", sa, sb, op_text(op));
    free(sa);
    free(sb);
    return set_type(e, type_error());
}

static Type *check_call(Expr *e) {
    for (int i = 0; i < e->args.len; i++) rvalue(e->args.items[i]);
    if (e->a->kind != E_VAR) {
        diag_error(e->loc, "ಕ್ರಿಯೆಯ ಹೆಸರನ್ನು ಮಾತ್ರ ಕರೆಯಬಹುದು");
        return set_type(e, type_error());
    }
    e->name = e->a->name;
    Symbol *s = lookup(e->name);
    if (!s) {
        diag_error(e->loc, "'%s' ಎಂಬ ಕ್ರಿಯೆಯನ್ನು ಘೋಷಿಸಲಾಗಿಲ್ಲ", e->name);
        return set_type(e, type_error());
    }
    if (s->kind != SYM_FUNC) {
        diag_error(e->loc, "'%s' ಒಂದು ಕ್ರಿಯೆಯಲ್ಲ; ಅದನ್ನು ಕರೆಯಲಾಗದು", e->name);
        return set_type(e, type_error());
    }
    e->sym = s;
    e->a->sym = s;
    FuncDecl *f = s->func;
    int n = f->params.len;
    if (e->args.len < n || (e->args.len > n && !f->variadic)) {
        diag_error(e->loc, "'%s' ಕ್ರಿಯೆಗೆ %s%d ವಾದ(ಗಳು) ಬೇಕು, ಆದರೆ %d ನೀಡಲಾಗಿದೆ", e->name,
                   f->variadic ? "ಕನಿಷ್ಠ " : "", n, e->args.len);
        return set_type(e, f->ret);
    }
    for (int i = 0; i < e->args.len; i++) {
        Expr *arg = e->args.items[i];
        if (i < n) {
            Param *p = f->params.items[i];
            char *ctx = xformat("'%s' ಕ್ರಿಯೆಯ %d ನೇ ವಾದ", e->name, i + 1);
            e->args.items[i] = coerce(arg, p->type, ctx);
            free(ctx);
        } else {
            Type *t = type_decay(arg->type);
            if (t->kind == TY_BOOL || t->kind == TY_CHAR)
                e->args.items[i] = make_cast(arg, type_int());
            else if (t->kind == TY_NULLPTR)
                e->args.items[i] = make_cast(arg, type_ptr(type_void()));
        }
    }
    return set_type(e, f->ret);
}

static Field *find_field(StructDef *s, const char *name) {
    for (int i = 0; i < s->fields.len; i++) {
        Field *f = s->fields.items[i];
        if (strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

static Type *check_member(Expr *e) {
    Type *t = check_expr(e->a);
    if (t->kind == TY_ERROR) return set_type(e, t);
    StructDef *s = NULL;
    if (e->arrow) {
        t = type_decay(t);
        if (t->kind == TY_PTR && t->base->kind == TY_STRUCT) s = t->base->sdef;
        else {
            char *ts = type_str(t);
            diag_error(e->loc, "'->' ರಚನೆಯ ಸೂಚಕಕ್ಕೆ ಮಾತ್ರ ಬಳಸಬಹುದು ('%s' ಸಿಕ್ಕಿತು)", ts);
            free(ts);
            return set_type(e, type_error());
        }
    } else {
        if (t->kind == TY_STRUCT) s = t->sdef;
        else if (t->kind == TY_PTR && t->base->kind == TY_STRUCT) {
            diag_error(e->loc, "ಇದು ರಚನೆಯ ಸೂಚಕ; '.' ಬದಲಿಗೆ '->' ಬಳಸಿ");
            return set_type(e, type_error());
        } else {
            char *ts = type_str(t);
            diag_error(e->loc, "'.' ರಚನೆಗೆ ಮಾತ್ರ ಬಳಸಬಹುದು ('%s' ಸಿಕ್ಕಿತು)", ts);
            free(ts);
            return set_type(e, type_error());
        }
    }
    if (!s->complete) {
        diag_error(e->loc, "ರಚನೆ '%s' ಅಪೂರ್ಣವಾಗಿದೆ (ಸದಸ್ಯರನ್ನು ವ್ಯಾಖ್ಯಾನಿಸಲಾಗಿಲ್ಲ)", s->name);
        return set_type(e, type_error());
    }
    Field *f = find_field(s, e->name);
    if (!f) {
        diag_error(e->loc, "ರಚನೆ '%s' ನಲ್ಲಿ '%s' ಎಂಬ ಸದಸ್ಯ ಇಲ್ಲ", s->name, e->name);
        return set_type(e, type_error());
    }
    e->field = f;
    return set_type(e, f->type);
}

static Type *check_cast(Expr *e) {
    resolve_type(e->to_type, e->loc);
    Type *to = e->to_type;
    Type *from = rvalue(e->a);
    if (from->kind == TY_ERROR) return set_type(e, type_error());
    bool ok = false;
    if (to->kind == TY_VOID) ok = true;
    else if (type_is_arith(to) && type_is_arith(from)) ok = true;
    else if (to->kind == TY_PTR && (from->kind == TY_PTR || from->kind == TY_NULLPTR || type_is_integer(from)))
        ok = true;
    else if (type_is_integer(to) && from->kind == TY_PTR) ok = true;
    else if (type_equal(to, from)) ok = true;
    if (!ok) {
        char *a = type_str(from), *b = type_str(to);
        diag_error(e->loc, "'%s' ಅನ್ನು '%s' ಗೆ ರೂಪಾಂತರಿಸಲಾಗದು", a, b);
        free(a);
        free(b);
        return set_type(e, type_error());
    }
    return set_type(e, to);
}

static Type *check_expr(Expr *e) {
    switch (e->kind) {
    case E_INT:
        return set_type(e, (e->ival > INT_MAX) ? type_long() : type_int());
    case E_FLOAT:
        return set_type(e, type_double());
    case E_STR:
        return set_type(e, type_ptr(type_char()));
    case E_CHAR:
        return set_type(e, e->wide ? type_int() : type_char());
    case E_BOOL:
        return set_type(e, type_bool());
    case E_NULL:
        return set_type(e, type_nullptr());
    case E_VAR: {
        Symbol *s = lookup(e->name);
        if (!s) {
            diag_error(e->loc, "'%s' ಅನ್ನು ಘೋಷಿಸಲಾಗಿಲ್ಲ", e->name);
            /* avoid repeating the same error */
            Symbol *dummy = xcalloc(1, sizeof(Symbol));
            dummy->kind = SYM_VAR;
            dummy->name = xstrdup(e->name);
            dummy->cname = "0";
            dummy->type = type_error();
            vec_push(&scope->syms, dummy);
            e->sym = dummy;
            return set_type(e, type_error());
        }
        if (s->kind == SYM_FUNC) {
            diag_error(e->loc, "ಕ್ರಿಯೆ '%s' ಅನ್ನು ಕರೆಯದೆ ಮೌಲ್ಯವಾಗಿ ಬಳಸಲಾಗದು ('%s(...)' ಎಂದು ಬರೆಯಿರಿ)",
                       e->name, e->name);
            return set_type(e, type_error());
        }
        e->sym = s;
        return set_type(e, s->type);
    }
    case E_UNARY: {
        Type *t = rvalue(e->a);
        if (t->kind == TY_ERROR) return set_type(e, t);
        switch (e->op) {
        case P_MINUS:
        case P_PLUS:
            if (type_is_arith(t)) return set_type(e, promote(t));
            break;
        case P_BANG:
            if (type_is_scalar(t)) return set_type(e, type_bool());
            break;
        case P_TILDE:
            if (type_is_integer(t)) return set_type(e, promote(t));
            break;
        default:
            break;
        }
        char *ts = type_str(t);
        diag_error(e->loc, "'%s' ಪ್ರಕಾರದ ಮೇಲೆ '%s' ಕಾರ್ಯಾಚರಣೆ ಸಾಧ್ಯವಿಲ್ಲ", ts, op_text(e->op));
        free(ts);
        return set_type(e, type_error());
    }
    case E_BINARY:
        return check_binary(e);
    case E_COND: {
        Type *c = rvalue(e->a);
        Type *a = rvalue(e->b);
        Type *b = rvalue(e->c);
        if (c->kind == TY_ERROR || a->kind == TY_ERROR || b->kind == TY_ERROR)
            return set_type(e, type_error());
        if (!type_is_scalar(c)) {
            diag_error(e->a->loc, "'?' ಷರತ್ತು ತಾರ್ಕಿಕ/ಸಂಖ್ಯಾ ಮೌಲ್ಯವಾಗಿರಬೇಕು");
            return set_type(e, type_error());
        }
        Type *r = NULL;
        if (type_equal(a, b)) r = a;
        else if (type_is_arith(a) && type_is_arith(b)) r = arith_result(a, b);
        else if (a->kind == TY_PTR && b->kind == TY_NULLPTR) r = a;
        else if (b->kind == TY_PTR && a->kind == TY_NULLPTR) r = b;
        else if (ptr_compatible(a, b)) r = type_ptr(type_void());
        if (!r) {
            char *sa = type_str(a), *sb = type_str(b);
            diag_error(e->loc, "'?:' ನ ಎರಡು ಶಾಖೆಗಳ ಪ್ರಕಾರಗಳು ('%s', '%s') ಹೊಂದುವುದಿಲ್ಲ", sa, sb);
            free(sa);
            free(sb);
            return set_type(e, type_error());
        }
        e->b = coerce(e->b, r, "'?:'");
        e->c = coerce(e->c, r, "'?:'");
        return set_type(e, r);
    }
    case E_CALL:
        return check_call(e);
    case E_INDEX: {
        Type *base = check_expr(e->a);
        Type *ix = rvalue(e->b);
        if (base->kind == TY_ERROR || ix->kind == TY_ERROR) return set_type(e, type_error());
        if (!type_is_integer(ix)) {
            diag_error(e->b->loc, "ಸೂಚ್ಯಂಕ (index) ಪೂರ್ಣಾಂಕವಾಗಿರಬೇಕು");
            return set_type(e, type_error());
        }
        if (base->kind == TY_ARRAY) return set_type(e, base->base);
        if (base->kind == TY_PTR && base->base->kind != TY_VOID) return set_type(e, base->base);
        char *ts = type_str(base);
        diag_error(e->loc, "'[ ]' ಸರಣಿ ಅಥವಾ ಸೂಚಕಕ್ಕೆ ಮಾತ್ರ ಬಳಸಬಹುದು ('%s' ಸಿಕ್ಕಿತು)", ts);
        free(ts);
        return set_type(e, type_error());
    }
    case E_MEMBER:
        return check_member(e);
    case E_CAST:
        return check_cast(e);
    case E_SIZEOF_TYPE:
        resolve_type(e->to_type, e->loc);
        if (!is_complete(e->to_type)) {
            char *ts = type_str(e->to_type);
            diag_error(e->loc, "'%s' ಪ್ರಕಾರದ ಗಾತ್ರ ತಿಳಿದಿಲ್ಲ", ts);
            free(ts);
        }
        return set_type(e, type_long());
    case E_SIZEOF_EXPR: {
        Type *t = check_expr(e->a);
        if (t->kind != TY_ERROR && !is_complete(t)) {
            char *ts = type_str(t);
            diag_error(e->loc, "'%s' ಪ್ರಕಾರದ ಗಾತ್ರ ತಿಳಿದಿಲ್ಲ", ts);
            free(ts);
        }
        return set_type(e, type_long());
    }
    case E_ADDR: {
        Type *t = check_expr(e->a);
        if (t->kind == TY_ERROR) return set_type(e, t);
        if (!is_lvalue(e->a)) {
            diag_error(e->loc, "'&' ಅನ್ನು ಚರ, ಸರಣಿಯ ಅಂಶ ಅಥವಾ ಸದಸ್ಯನಿಗೆ ಮಾತ್ರ ಬಳಸಬಹುದು");
            return set_type(e, type_error());
        }
        if (t->kind == TY_ARRAY) return set_type(e, type_ptr(t->base));
        return set_type(e, type_ptr(t));
    }
    case E_DEREF: {
        Type *t = rvalue(e->a);
        if (t->kind == TY_ERROR) return set_type(e, t);
        if (t->kind == TY_PTR && t->base->kind != TY_VOID) return set_type(e, t->base);
        char *ts = type_str(t);
        diag_error(e->loc, "'*' ಸೂಚಕಕ್ಕೆ ಮಾತ್ರ ಬಳಸಬಹುದು ('%s' ಸಿಕ್ಕಿತು)", ts);
        free(ts);
        return set_type(e, type_error());
    }
    case E_VA_START: {
        Type *t = check_expr(e->a);
        if (!cur_fn || !cur_fn->variadic) {
            diag_error(e->loc, "'ಚರ_ಆರಂಭ' ಅನ್ನು '...' ಹೊಂದಿರುವ ಕ್ರಿಯೆಯಲ್ಲಿ ಮಾತ್ರ ಬಳಸಬಹುದು");
        } else {
            Param *last = cur_fn->params.items[cur_fn->params.len - 1];
            e->va_last = last->sym ? last->sym->cname : (char *)mangle("k_", last->name);
        }
        if (t->kind != TY_ERROR && (t->kind != TY_VALIST || !is_lvalue(e->a)))
            diag_error(e->a->loc, "'ಚರ_ಆರಂಭ' ಗೆ 'ಚರಪಟ್ಟಿ' ಚರ ಬೇಕು");
        return set_type(e, type_void());
    }
    case E_VA_ARG: {
        Type *t = check_expr(e->a);
        resolve_type(e->to_type, e->loc);
        if (t->kind != TY_ERROR && t->kind != TY_VALIST)
            diag_error(e->a->loc, "'ಚರ_ಮೌಲ್ಯ' ಗೆ 'ಚರಪಟ್ಟಿ' ಬೇಕು");
        if (!is_complete(e->to_type) || e->to_type->kind == TY_ARRAY) {
            diag_error(e->loc, "'ಚರ_ಮೌಲ್ಯ' ಗೆ ಮಾನ್ಯ ಪ್ರಕಾರ ಬೇಕು");
            return set_type(e, type_error());
        }
        return set_type(e, e->to_type);
    }
    case E_VA_END: {
        Type *t = check_expr(e->a);
        if (t->kind != TY_ERROR && t->kind != TY_VALIST) diag_error(e->a->loc, "'ಚರ_ಅಂತ್ಯ' ಗೆ 'ಚರಪಟ್ಟಿ' ಬೇಕು");
        return set_type(e, type_void());
    }
    case E_VA_COPY: {
        Type *a = check_expr(e->a);
        Type *b = check_expr(e->b);
        if ((a->kind != TY_ERROR && (a->kind != TY_VALIST || !is_lvalue(e->a))) ||
            (b->kind != TY_ERROR && b->kind != TY_VALIST))
            diag_error(e->loc, "'ಚರ_ನಕಲು' ಗೆ ಎರಡು 'ಚರಪಟ್ಟಿ' ಚರಗಳು ಬೇಕು");
        return set_type(e, type_void());
    }
    case E_INITLIST:
        diag_error(e->loc, "'{ ... }' ಪಟ್ಟಿಯನ್ನು ಚರದ ಆರಂಭಿಕ ಮೌಲ್ಯವಾಗಿ ಮಾತ್ರ ಬಳಸಬಹುದು");
        return set_type(e, type_error());
    }
    return set_type(e, type_error());
}

/* ------------------------------------------------------------------ */
/* initializers                                                        */
/* ------------------------------------------------------------------ */

static Expr *check_initializer(Type *t, Expr *init) {
    if (t->kind == TY_ARRAY) {
        if (init->kind == E_STR && t->base->kind == TY_CHAR) {
            if (t->len < 0) t->len = (long long)init->len + 1;
            else if ((long long)init->len > t->len)
                diag_error(init->loc, "ಪದವು ಸರಣಿಗೆ ತುಂಬಾ ಉದ್ದವಾಗಿದೆ (%lld ಸ್ಥಳಗಳು, %zu ಬೈಟ್‌ಗಳು ಬೇಕು)",
                           t->len, init->len + 1);
            init->type = t;
            return init;
        }
        if (init->kind != E_INITLIST) {
            diag_error(init->loc, "ಸರಣಿಯ ಆರಂಭಿಕ ಮೌಲ್ಯ '{ ... }' ಪಟ್ಟಿಯಾಗಿರಬೇಕು");
            check_expr(init);
            return init;
        }
        if (t->len < 0) {
            if (init->args.len == 0) diag_error(init->loc, "ಖಾಲಿ ಪಟ್ಟಿಯಿಂದ ಸರಣಿಯ ಗಾತ್ರ ತಿಳಿಯದು");
            t->len = init->args.len ? init->args.len : 1;
        } else if (init->args.len > t->len) {
            diag_error(init->loc, "ಹೆಚ್ಚು ಆರಂಭಿಕ ಮೌಲ್ಯಗಳು (%d), ಸರಣಿಯ ಗಾತ್ರ %lld", init->args.len, t->len);
        }
        for (int i = 0; i < init->args.len; i++)
            init->args.items[i] = check_initializer(t->base, init->args.items[i]);
        init->type = t;
        return init;
    }
    if (t->kind == TY_STRUCT && init->kind == E_INITLIST) {
        StructDef *s = t->sdef;
        if (!s->complete) {
            diag_error(init->loc, "ಅಪೂರ್ಣ ರಚನೆ '%s'", s->name);
            return init;
        }
        if (init->args.len > s->fields.len)
            diag_error(init->loc, "ಹೆಚ್ಚು ಆರಂಭಿಕ ಮೌಲ್ಯಗಳು: ರಚನೆ '%s' ನಲ್ಲಿ %d ಸದಸ್ಯರು ಮಾತ್ರ ಇದ್ದಾರೆ", s->name,
                       s->fields.len);
        for (int i = 0; i < init->args.len && i < s->fields.len; i++) {
            Field *f = s->fields.items[i];
            init->args.items[i] = check_initializer(f->type, init->args.items[i]);
        }
        init->type = t;
        return init;
    }
    if (init->kind == E_INITLIST) {
        if (init->args.len != 1) {
            diag_error(init->loc, "ಏಕ ಮೌಲ್ಯದ ಚರಕ್ಕೆ '{ }' ಪಟ್ಟಿಯಲ್ಲಿ ಒಂದೇ ಮೌಲ್ಯ ಇರಬೇಕು");
            return init;
        }
        init = init->args.items[0];
    }
    rvalue(init);
    return coerce(init, t, "ಆರಂಭಿಕ ಮೌಲ್ಯ");
}

static bool static_init(Expr *e) {
    switch (e->kind) {
    case E_INITLIST:
        for (int i = 0; i < e->args.len; i++)
            if (!static_init(e->args.items[i])) return false;
        return true;
    case E_STR:
    case E_NULL:
        return true;
    case E_ADDR:
        return e->a->kind == E_VAR && e->a->sym && e->a->sym->is_global;
    case E_VAR:
        if (e->sym && e->sym->kind == SYM_VAR && e->sym->is_global && e->sym->type->kind == TY_ARRAY)
            return true;
        break;
    case E_CAST:
        if (e->to_type->kind == TY_PTR) return static_init(e->a);
        break;
    default:
        break;
    }
    if (e->type && type_is_arith(e->type)) {
        ConstVal c = const_eval(e);
        if (c.ok) {
            e->cv = cv_convert(c, e->type);
            e->folded = true;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* variables                                                           */
/* ------------------------------------------------------------------ */

static void check_var_type(VarDecl *v) {
    Type *t = v->type;
    if (t->kind == TY_VOID) {
        diag_error(v->loc, "'%s': 'ಶೂನ್ಯ' ಪ್ರಕಾರದ ಚರ ಸಾಧ್ಯವಿಲ್ಲ (ಸೂಚಕಕ್ಕೆ 'ಶೂನ್ಯ*' ಬಳಸಿ)", v->name);
        v->type = type_error();
        return;
    }
    if (t->kind == TY_ARRAY && t->len < 0 && !v->init) {
        diag_error(v->loc, "'%s' ಸರಣಿಯ ಗಾತ್ರ ಬೇಕು", v->name);
        t->len = 1;
    }
}

static void check_var_complete(VarDecl *v) {
    if (v->type->kind == TY_ERROR) return;
    if (!is_complete(v->type)) {
        char *ts = type_str(v->type);
        diag_error(v->loc, "'%s' ಚರದ ಪ್ರಕಾರ '%s' ಅಪೂರ್ಣವಾಗಿದೆ", v->name, ts);
        free(ts);
    }
}

static Symbol *new_var_symbol(VarDecl *v) {
    Symbol *s = xcalloc(1, sizeof(Symbol));
    s->kind = SYM_VAR;
    s->name = v->name;
    s->type = v->type;
    s->is_const = v->is_const;
    s->is_global = v->is_global;
    s->var = v;
    v->sym = s;
    return s;
}

static void check_local_var(VarDecl *v) {
    resolve_type(v->type, v->loc);
    check_var_type(v);
    if (v->type->kind == TY_VALIST && v->init) diag_error(v->loc, "'ಚರಪಟ್ಟಿ' ಗೆ ಆರಂಭಿಕ ಮೌಲ್ಯ ಕೊಡಲಾಗದು");
    if (v->init) v->init = check_initializer(v->type, v->init);
    else if (v->is_const) diag_error(v->loc, "ಸ್ಥಿರ '%s' ಗೆ ಆರಂಭಿಕ ಮೌಲ್ಯ ಬೇಕು", v->name);
    check_var_complete(v);
    Symbol *s = new_var_symbol(v);
    s->type = v->type;
    s->cname = (char *)mangle("k_", v->name);
    v->cname = s->cname;
    declare(s, v->loc);
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static void check_stmt(Stmt *s);

static void check_cond(Expr *e, const char *what) {
    Type *t = rvalue(e);
    if (t->kind != TY_ERROR && !type_is_scalar(t)) {
        char *ts = type_str(t);
        diag_error(e->loc, "%s ತಾರ್ಕಿಕ ಅಥವಾ ಸಂಖ್ಯಾ ಮೌಲ್ಯವಾಗಿರಬೇಕು ('%s' ಸಿಕ್ಕಿತು)", what, ts);
        free(ts);
    }
}

static void check_scoped(Stmt *s) {
    push_scope();
    check_stmt(s);
    pop_scope();
}

static void check_assign(Stmt *s) {
    Type *lt = check_expr(s->lhs);
    Type *rt = rvalue(s->rhs);
    if (lt->kind == TY_ERROR || rt->kind == TY_ERROR) return;
    if (!require_lvalue(s->lhs, "ನಿಯೋಜನೆಯ ಎಡಭಾಗ")) return;
    if (lt->kind == TY_VALIST) {
        diag_error(s->loc, "'ಚರಪಟ್ಟಿ' ಅನ್ನು ನಿಯೋಜಿಸಲಾಗದು; 'ಚರ_ನಕಲು' ಬಳಸಿ");
        return;
    }
    int op = s->op;
    if (op == P_ASSIGN) {
        s->rhs = coerce(s->rhs, lt, "ನಿಯೋಜನೆ");
        return;
    }
    bool ok;
    if ((op == P_PLUSEQ || op == P_MINUSEQ) && lt->kind == TY_PTR)
        ok = type_is_integer(rt) && lt->base->kind != TY_VOID;
    else if (lt->kind == TY_BOOL)
        ok = false;
    else if (op == P_PERCENTEQ || op == P_AMPEQ || op == P_PIPEEQ || op == P_CARETEQ || op == P_SHLEQ ||
             op == P_SHREQ)
        ok = type_is_integer(lt) && type_is_integer(rt);
    else
        ok = type_is_arith(lt) && type_is_arith(rt);
    if (!ok) {
        char *a = type_str(lt), *b = type_str(rt);
        diag_error(s->loc, "'%s' ಮತ್ತು '%s' ಪ್ರಕಾರಗಳೊಂದಿಗೆ '%s' ಸಾಧ್ಯವಿಲ್ಲ", a, b, op_text(op));
        free(a);
        free(b);
    }
}

static void check_simple(Stmt *s) {
    switch (s->kind) {
    case S_ASSIGN:
        check_assign(s);
        break;
    case S_INCDEC: {
        Type *t = check_expr(s->lhs);
        if (t->kind == TY_ERROR) break;
        if (!require_lvalue(s->lhs, "'++'/'--' ನ ಗುರಿ")) break;
        if (!((type_is_arith(t) && t->kind != TY_BOOL) || (t->kind == TY_PTR && t->base->kind != TY_VOID))) {
            char *ts = type_str(t);
            diag_error(s->loc, "'%s' ಪ್ರಕಾರದ ಮೇಲೆ '%s' ಸಾಧ್ಯವಿಲ್ಲ", ts, op_text(s->op));
            free(ts);
        }
        break;
    }
    case S_EXPR: {
        check_expr(s->expr);
        ExprKind k = s->expr->kind;
        if (k != E_CALL && k != E_VA_START && k != E_VA_END && k != E_VA_COPY && s->expr->type->kind != TY_ERROR)
            diag_warning(s->loc, "ಈ ಅಭಿವ್ಯಕ್ತಿಯ ಫಲಿತಾಂಶವನ್ನು ಬಳಸಲಾಗಿಲ್ಲ (ನಿಯೋಜನೆಗೆ 'ಗೆ ... ನಿಗದಿ' ಅಥವಾ '=' ಬಳಸಿ)");
        break;
    }
    default:
        check_stmt(s);
        break;
    }
}

static bool printable(Type *t) {
    if (type_is_arith(t) || t->kind == TY_PTR || t->kind == TY_NULLPTR) return true;
    if (t->kind == TY_ARRAY && t->base->kind == TY_CHAR) return true;
    return false;
}

static bool readable(Type *t) {
    switch (t->kind) {
    case TY_INT:
    case TY_LONG:
    case TY_DOUBLE:
    case TY_CHAR:
    case TY_BOOL:
        return true;
    case TY_PTR:
        return t->base->kind == TY_CHAR;
    case TY_ARRAY:
        return t->base->kind == TY_CHAR;
    default:
        return false;
    }
}

static void check_switch(Stmt *s) {
    Type *t = rvalue(s->cond);
    if (t->kind != TY_ERROR && !type_is_integer(t))
        diag_error(s->cond->loc, "'ಆಯ್ಕೆ' ಮೌಲ್ಯವು ಪೂರ್ಣಾಂಕ ಅಥವಾ ಅಕ್ಷರವಾಗಿರಬೇಕು");
    bool has_default = false;
    switch_depth++;
    for (int i = 0; i < s->cases.len; i++) {
        SwitchCase *c = s->cases.items[i];
        if (!c->value) {
            if (has_default) diag_error(c->loc, "ಒಂದಕ್ಕಿಂತ ಹೆಚ್ಚು 'ಪೂರ್ವನಿಯೋಜಿತ' ವಿಭಾಗಗಳು");
            has_default = true;
        } else {
            Type *ct = rvalue(c->value);
            ConstVal v = const_eval(c->value);
            if (ct->kind != TY_ERROR && (!v.ok || v.is_float)) {
                diag_error(c->value->loc, "'ಸಂದರ್ಭ' ಮೌಲ್ಯವು ಸ್ಥಿರ ಪೂರ್ಣಾಂಕವಾಗಿರಬೇಕು");
            } else if (v.ok) {
                c->cval = v.i;
                for (int j = 0; j < i; j++) {
                    SwitchCase *o = s->cases.items[j];
                    if (o->value && o->cval == c->cval) {
                        diag_error(c->loc, "'ಸಂದರ್ಭ %lld' ಪುನರಾವರ್ತಿತವಾಗಿದೆ", c->cval);
                        break;
                    }
                }
            }
        }
        push_scope();
        for (int k = 0; k < c->stmts.len; k++) check_stmt(c->stmts.items[k]);
        pop_scope();
    }
    switch_depth--;
}

static void check_stmt(Stmt *s) {
    switch (s->kind) {
    case S_BLOCK:
        push_scope();
        for (int i = 0; i < s->stmts.len; i++) check_stmt(s->stmts.items[i]);
        pop_scope();
        break;
    case S_DECL:
        for (int i = 0; i < s->decls.len; i++) check_local_var(s->decls.items[i]);
        break;
    case S_ASSIGN:
    case S_INCDEC:
    case S_EXPR:
        check_simple(s);
        break;
    case S_IF:
        check_cond(s->cond, "'ಆದರೆ' ಷರತ್ತು");
        check_scoped(s->then);
        if (s->els) check_scoped(s->els);
        break;
    case S_WHILE:
        check_cond(s->cond, "'ಸಮಯವರೆಗೆ' ಷರತ್ತು");
        loop_depth++;
        check_scoped(s->body);
        loop_depth--;
        break;
    case S_DOWHILE:
        loop_depth++;
        check_scoped(s->body);
        loop_depth--;
        check_cond(s->cond, "'ಮಾಡು ... ಸಮಯವರೆಗೆ' ಷರತ್ತು");
        break;
    case S_FOR:
        push_scope();
        for (int i = 0; i < s->init.len; i++) check_simple(s->init.items[i]);
        if (s->cond) check_cond(s->cond, "'ಎಣಿಕೆ' ಷರತ್ತು");
        for (int i = 0; i < s->update.len; i++) check_simple(s->update.items[i]);
        loop_depth++;
        check_scoped(s->body);
        loop_depth--;
        pop_scope();
        break;
    case S_SWITCH:
        check_switch(s);
        break;
    case S_BREAK:
        if (loop_depth == 0 && switch_depth == 0)
            diag_error(s->loc, "'ನಿಲ್ಲಿಸು' ಆವರ್ತನ (loop) ಅಥವಾ 'ಆಯ್ಕೆ' ಒಳಗೆ ಮಾತ್ರ ಬಳಸಬಹುದು");
        break;
    case S_CONTINUE:
        if (loop_depth == 0) diag_error(s->loc, "'ಮುಂದುವರಿಸು' ಆವರ್ತನ (loop) ಒಳಗೆ ಮಾತ್ರ ಬಳಸಬಹುದು");
        break;
    case S_RETURN: {
        Type *ret = cur_fn->ret;
        if (s->expr) {
            Type *t = rvalue(s->expr);
            if (ret->kind == TY_VOID) {
                if (t->kind != TY_ERROR)
                    diag_error(s->loc, "'%s' ಕ್ರಿಯೆ 'ಶೂನ್ಯ'; 'ಮರಳಿ' ಮೌಲ್ಯವನ್ನು ಹಿಂದಿರುಗಿಸಬಾರದು", cur_fn->name);
            } else {
                s->expr = coerce(s->expr, ret, "'ಮರಳಿ' ಮೌಲ್ಯ");
            }
        } else if (ret->kind != TY_VOID) {
            char *ts = type_str(ret);
            diag_error(s->loc, "'%s' ಕ್ರಿಯೆಯು '%s' ಮೌಲ್ಯವನ್ನು ಹಿಂದಿರುಗಿಸಬೇಕು", cur_fn->name, ts);
            free(ts);
        }
        break;
    }
    case S_PRINT:
        for (int i = 0; i < s->args.len; i++) {
            Expr *a = s->args.items[i];
            Type *t = check_expr(a);
            if (t->kind == TY_ERROR) continue;
            if (!printable(t)) {
                char *ts = type_str(t);
                diag_error(a->loc, "'%s' ಪ್ರಕಾರದ ಮೌಲ್ಯವನ್ನು ನೇರವಾಗಿ ಮುದ್ರಿಸಲಾಗದು", ts);
                free(ts);
            }
        }
        break;
    case S_READ:
        for (int i = 0; i < s->args.len; i++) {
            Expr *a = s->args.items[i];
            Type *t = check_expr(a);
            if (t->kind == TY_ERROR) continue;
            if (t->kind == TY_ARRAY && t->base->kind == TY_CHAR) {
                if (!is_lvalue(a)) diag_error(a->loc, "'ಓದು' ಗೆ ಚರ ಬೇಕು");
                continue;
            }
            if (!require_lvalue(a, "'ಓದು' ವಾದ")) continue;
            if (!readable(t)) {
                char *ts = type_str(t);
                diag_error(a->loc, "'%s' ಪ್ರಕಾರವನ್ನು 'ಓದು' ಮೂಲಕ ಓದಲಾಗದು", ts);
                free(ts);
            }
        }
        break;
    case S_EMPTY:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* return-path analysis                                                */
/* ------------------------------------------------------------------ */

static bool contains_break(Stmt *s) {
    if (!s) return false;
    switch (s->kind) {
    case S_BREAK: return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.len; i++)
            if (contains_break(s->stmts.items[i])) return true;
        return false;
    case S_IF: return contains_break(s->then) || contains_break(s->els);
    default: return false; /* nested loops / switches own their breaks */
    }
}

static bool always_true(Expr *e) {
    if (!e) return true;
    ConstVal c = const_eval(e);
    return c.ok && cv_truth(c);
}

static bool always_returns(Stmt *s) {
    if (!s) return false;
    switch (s->kind) {
    case S_RETURN: return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.len; i++)
            if (always_returns(s->stmts.items[i])) return true;
        return false;
    case S_IF: return s->els && always_returns(s->then) && always_returns(s->els);
    case S_WHILE:
    case S_FOR: return always_true(s->cond) && !contains_break(s->body);
    case S_DOWHILE: return always_returns(s->body) || (always_true(s->cond) && !contains_break(s->body));
    case S_SWITCH: {
        bool has_default = false;
        for (int i = 0; i < s->cases.len; i++) {
            SwitchCase *c = s->cases.items[i];
            if (!c->value) has_default = true;
            Stmt tmp;
            memset(&tmp, 0, sizeof tmp);
            tmp.kind = S_BLOCK;
            tmp.stmts = c->stmts;
            if (contains_break(&tmp)) return false;
            if (c->stmts.len > 0 && !always_returns(&tmp)) return false;
            if (i == s->cases.len - 1 && !always_returns(&tmp)) return false;
        }
        return has_default;
    }
    default: return false;
    }
}

/* ------------------------------------------------------------------ */
/* declarations                                                        */
/* ------------------------------------------------------------------ */

static bool valid_c_identifier(const char *s) {
    if (!*s || !(isalpha((unsigned char)*s) || *s == '_')) return false;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_')) return false;
    return true;
}

static char *extern_cname(const char *given, const char *name, Loc l) {
    const char *c = given ? given : name;
    if (!valid_c_identifier(c)) {
        if (given)
            diag_error(l, "'%s' ಮಾನ್ಯ C ಹೆಸರಲ್ಲ", given);
        else
            diag_error(l, "'ಬಾಹ್ಯ' ಘೋಷಣೆಗೆ C ಹೆಸರು ಬೇಕು, ಉದಾ: ಬಾಹ್ಯ \"c_hesaru\" ಕ್ರಿಯೆ %s(...)", name);
        return xstrdup("kst_invalid_name");
    }
    return xstrdup(c);
}

static void declare_function(FuncDecl *f) {
    Symbol *s = xcalloc(1, sizeof(Symbol));
    s->kind = SYM_FUNC;
    s->name = f->name;
    s->func = f;
    s->is_global = true;
    s->cname = f->is_extern ? extern_cname(f->cname, f->name, f->loc) : (char *)mangle("k_", f->name);
    f->cname = s->cname;
    f->sym = s;
    declare(s, f->loc);
    if (!f->is_extern && strcmp(f->name, MAIN_NAME) == 0) g_main = f;
}

static void resolve_function(FuncDecl *f) {
    resolve_type(f->ret, f->loc);
    if (f->ret->kind == TY_ARRAY) {
        diag_error(f->loc, "ಕ್ರಿಯೆಯು ಸರಣಿಯನ್ನು ಹಿಂದಿರುಗಿಸಲಾಗದು (ಸೂಚಕ ಬಳಸಿ)");
        f->ret = type_error();
    } else if (f->ret->kind == TY_STRUCT && !f->ret->sdef->complete) {
        diag_error(f->loc, "ಅಪೂರ್ಣ ರಚನೆ '%s' ಅನ್ನು ಹಿಂದಿರುಗಿಸಲಾಗದು", f->ret->sdef->name);
    }
    f->sym->type = f->ret;
    for (int i = 0; i < f->params.len; i++) {
        Param *p = f->params.items[i];
        resolve_type(p->type, p->loc);
        if (p->type->kind == TY_ARRAY) p->type = type_ptr(p->type->base);
        if (p->type->kind == TY_VOID) {
            diag_error(p->loc, "'%s': 'ಶೂನ್ಯ' ಪ್ರಕಾರದ ನಿಯತಾಂಕ ಸಾಧ್ಯವಿಲ್ಲ", p->name);
            p->type = type_error();
        } else if (p->type->kind == TY_STRUCT && !p->type->sdef->complete) {
            diag_error(p->loc, "ಅಪೂರ್ಣ ರಚನೆ '%s' ನಿಯತಾಂಕ (ಸೂಚಕ ಬಳಸಿ)", p->type->sdef->name);
        }
        for (int j = 0; j < i; j++) {
            Param *q = f->params.items[j];
            if (strcmp(p->name, q->name) == 0) diag_error(p->loc, "'%s' ನಿಯತಾಂಕ ಪುನರಾವರ್ತಿತವಾಗಿದೆ", p->name);
        }
    }
    if (f->variadic && f->params.len == 0)
        diag_error(f->loc, "'...' ಹೊಂದಿರುವ ಕ್ರಿಯೆಗೆ ಕನಿಷ್ಠ ಒಂದು ಹೆಸರಿಸಿದ ನಿಯತಾಂಕ ಬೇಕು");
}

static void check_function_body(FuncDecl *f) {
    cur_fn = f;
    push_scope();
    for (int i = 0; i < f->params.len; i++) {
        Param *p = f->params.items[i];
        Symbol *s = xcalloc(1, sizeof(Symbol));
        s->kind = SYM_VAR;
        s->name = p->name;
        s->type = p->type;
        s->is_const = p->is_const;
        s->cname = (char *)mangle("k_", p->name);
        p->sym = s;
        declare(s, p->loc);
    }
    /* the body shares the parameter scope, like C */
    for (int i = 0; i < f->body->stmts.len; i++) check_stmt(f->body->stmts.items[i]);
    pop_scope();
    if (f->ret->kind != TY_VOID && f->ret->kind != TY_ERROR && !always_returns(f->body))
        diag_warning(f->loc, "'%s' ಕ್ರಿಯೆಯ ಎಲ್ಲಾ ಮಾರ್ಗಗಳಲ್ಲಿ 'ಮರಳಿ' ಇಲ್ಲ", f->name);
    cur_fn = NULL;
}

static void check_struct_cycles(StructDef *s) {
    if (s->mark == 2) return;
    if (s->mark == 1) {
        diag_error(s->loc, "ರಚನೆ '%s' ತನ್ನನ್ನೇ ಒಳಗೊಂಡಿದೆ (ಸ್ವ-ಉಲ್ಲೇಖಕ್ಕೆ ಸೂಚಕ '*' ಬಳಸಿ)", s->name);
        return;
    }
    s->mark = 1;
    for (int i = 0; i < s->fields.len; i++) {
        Field *f = s->fields.items[i];
        Type *t = f->type;
        while (t->kind == TY_ARRAY) t = t->base;
        if (t->kind == TY_STRUCT && t->sdef->complete) check_struct_cycles(t->sdef);
    }
    s->mark = 2;
}

static void check_structs(void) {
    Vec *all = struct_list();
    for (int i = 0; i < all->len; i++) {
        StructDef *s = all->items[i];
        s->cname = (char *)mangle("ks_", s->name);
        for (int j = 0; j < s->fields.len; j++) {
            Field *f = s->fields.items[j];
            f->cname = (char *)mangle("f_", f->name);
            resolve_type(f->type, f->loc);
            for (int k = 0; k < j; k++) {
                Field *g = s->fields.items[k];
                if (strcmp(f->name, g->name) == 0)
                    diag_error(f->loc, "'%s' ಸದಸ್ಯ ಪುನರಾವರ್ತಿತವಾಗಿದೆ", f->name);
            }
            if (f->type->kind == TY_VOID || f->type->kind == TY_VALIST) {
                diag_error(f->loc, "'%s' ಸದಸ್ಯನ ಪ್ರಕಾರ ಅಮಾನ್ಯ", f->name);
                f->type = type_int();
            }
        }
    }
    for (int i = 0; i < all->len; i++) {
        StructDef *s = all->items[i];
        if (s->complete) check_struct_cycles(s);
    }
    for (int i = 0; i < all->len; i++) {
        StructDef *s = all->items[i];
        for (int j = 0; j < s->fields.len; j++) {
            Field *f = s->fields.items[j];
            if (!is_complete(f->type) && f->type->kind != TY_ERROR)
                diag_error(f->loc, "'%s' ಸದಸ್ಯನ ಪ್ರಕಾರ ಅಪೂರ್ಣವಾಗಿದೆ", f->name);
        }
    }
}

bool sema_check(Program *prog, bool require_main) {
    /* 1. declare every global name so that order does not matter */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind == D_FUNC) {
            declare_function(d->func);
        } else if (d->kind == D_VAR) {
            VarDecl *v = d->var;
            Symbol *s = new_var_symbol(v);
            s->cname = v->is_extern ? extern_cname(v->cname, v->name, v->loc) : (char *)mangle("k_", v->name);
            v->cname = s->cname;
            declare(s, v->loc);
        }
    }

    /* structs after the globals: member array sizes may use global constants */
    check_structs();

    /* 2. resolve the types of functions and global variables */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind == D_FUNC) resolve_function(d->func);
        else if (d->kind == D_VAR) resolve_type(d->var->type, d->var->loc);
    }

    /* 3. global initialisers */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind != D_VAR) continue;
        VarDecl *v = d->var;
        check_var_type(v);
        if (v->type->kind == TY_VALIST) diag_error(v->loc, "'ಚರಪಟ್ಟಿ' ಜಾಗತಿಕ ಚರವಾಗಿರಲಾಗದು");
        if (v->init) {
            v->init = check_initializer(v->type, v->init);
            if (!static_init(v->init))
                diag_error(v->init->loc, "ಜಾಗತಿಕ ಚರ '%s' ನ ಆರಂಭಿಕ ಮೌಲ್ಯ ಸಂಕಲನ-ಸಮಯದ ಸ್ಥಿರವಾಗಿರಬೇಕು", v->name);
        } else if (v->is_const && !v->is_extern) {
            diag_error(v->loc, "ಸ್ಥಿರ '%s' ಗೆ ಆರಂಭಿಕ ಮೌಲ್ಯ ಬೇಕು", v->name);
        }
        v->sym->type = v->type;
        if (!v->is_extern || v->type->kind != TY_ARRAY) check_var_complete(v);
    }

    /* 4. function bodies */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind == D_FUNC && !d->func->is_extern) check_function_body(d->func);
    }

    if (require_main) {
        if (!g_main) {
            Loc none = {0};
            diag_error(none, "'ಮುಖ್ಯ' ಕ್ರಿಯೆ ಕಂಡುಬಂದಿಲ್ಲ. ಪ್ರೋಗ್ರಾಂ ಹೀಗೆ ಪ್ರಾರಂಭವಾಗಬೇಕು: "
                             "ಕ್ರಿಯೆ ಮುಖ್ಯ() ಫಲಿತಾಂಶ ಸಂಖ್ಯೆ { ... }");
        } else {
            FuncDecl *m = g_main;
            bool ret_ok = m->ret->kind == TY_INT || m->ret->kind == TY_VOID;
            bool params_ok = m->params.len == 0;
            if (m->params.len == 2) {
                Param *a = m->params.items[0], *b = m->params.items[1];
                Type *bt = b->type;
                params_ok = a->type->kind == TY_INT && bt->kind == TY_PTR && bt->base->kind == TY_PTR &&
                            bt->base->base->kind == TY_CHAR;
            }
            if (!ret_ok || !params_ok || m->variadic)
                diag_error(m->loc, "'ಮುಖ್ಯ' ಹೀಗಿರಬೇಕು: ಕ್ರಿಯೆ ಮುಖ್ಯ() ಫಲಿತಾಂಶ ಸಂಖ್ಯೆ  ಅಥವಾ  "
                                   "ಕ್ರಿಯೆ ಮುಖ್ಯ(ಸಂಖ್ಯೆ ಎಣಿಕೆ_, ಪದ* ವಾದಗಳು) ಫಲಿತಾಂಶ ಸಂಖ್ಯೆ");
        }
    }
    return diag_error_count() == 0;
}
