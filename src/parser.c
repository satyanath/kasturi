/* parser.c - builds the AST from tokens (grammar: see grammar.txt) */
#include "parser.h"

#include <stdlib.h>
#include <string.h>

static StrMap g_struct_map;
static Vec g_struct_list;

StructDef *struct_lookup(const char *name) { return map_get(&g_struct_map, name); }
Vec *struct_list(void) { return &g_struct_list; }

void parser_register_structs(Token *toks, int ntok) {
    for (int i = 0; i + 1 < ntok; i++) {
        if (toks[i].kind == K_STRUCT && toks[i + 1].kind == T_ID) {
            const char *name = toks[i + 1].text;
            if (!struct_lookup(name)) {
                StructDef *s = xcalloc(1, sizeof(StructDef));
                s->name = xstrdup(name);
                s->loc = toks[i + 1].loc;
                map_put(&g_struct_map, name, s);
                vec_push(&g_struct_list, s);
            }
        }
    }
}

void parser_init(Parser *p, Token *toks, int ntok) {
    memset(p, 0, sizeof *p);
    p->toks = toks;
    p->ntok = ntok;
}

/* ------------------------------------------------------------------ */
/* token helpers                                                       */
/* ------------------------------------------------------------------ */

static Token *peek_at(Parser *p, int k) {
    int i = p->pos + k;
    if (i >= p->ntok) i = p->ntok - 1;
    return &p->toks[i];
}

static Token *tok(Parser *p) { return peek_at(p, 0); }
static TokKind kind(Parser *p) { return tok(p)->kind; }
static Loc loc(Parser *p) { return tok(p)->loc; }

static Token *next(Parser *p) {
    Token *t = tok(p);
    if (p->pos < p->ntok - 1) p->pos++;
    return t;
}

static bool accept(Parser *p, TokKind k) {
    if (kind(p) == k) {
        next(p);
        return true;
    }
    return false;
}

static void syntax_error(Parser *p, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *msg = xvformat(fmt, ap);
    va_end(ap);
    diag_error(loc(p), "%s", msg);
    free(msg);
    longjmp(p->recover, 1);
}

static const char *describe(Token *t) {
    static char buf[256];
    switch (t->kind) {
    case T_ID: snprintf(buf, sizeof buf, "'%s'", t->text); return buf;
    case T_INT: snprintf(buf, sizeof buf, "%lld", t->ival); return buf;
    case T_EOF: return tok_spelling(T_EOF);
    case T_STR: return "ಪದ ಅಕ್ಷರಶಃ";
    case T_FLOAT: snprintf(buf, sizeof buf, "%s", t->text); return buf;
    default: snprintf(buf, sizeof buf, "'%s'", tok_spelling(t->kind)); return buf;
    }
}

static Token *expect(Parser *p, TokKind k) {
    if (kind(p) != k) {
        char *want = xstrdup(tok_spelling(k));
        syntax_error(p, "'%s' ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು, ಆದರೆ %s ಸಿಕ್ಕಿತು", want, describe(tok(p)));
    }
    return next(p);
}

static char *expect_ident(Parser *p, const char *what) {
    if (kind(p) != T_ID) {
        if (kind(p) >= K_INT && kind(p) < P_LPAREN)
            syntax_error(p, "%s ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು; '%s' ಒಂದು ಮೀಸಲು ಪದ (keyword), ಅದನ್ನು ಹೆಸರಾಗಿ ಬಳಸಲಾಗದು",
                         what, tok_spelling(kind(p)));
        syntax_error(p, "%s ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು, ಆದರೆ %s ಸಿಕ್ಕಿತು", what, describe(tok(p)));
    }
    return xstrdup(next(p)->text);
}

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

static bool is_base_type_kw(TokKind k) {
    return k == K_INT || k == K_LONG || k == K_DOUBLE || k == K_CHAR || k == K_STRING ||
           k == K_BOOL || k == K_VOID || k == K_VALIST;
}

static bool is_type_start_at(Parser *p, int k) {
    Token *t = peek_at(p, k);
    if (is_base_type_kw(t->kind) || t->kind == K_STRUCT) return true;
    return t->kind == T_ID && struct_lookup(t->text) != NULL;
}

static bool is_type_start(Parser *p) { return is_type_start_at(p, 0); }

static Type *parse_type(Parser *p) {
    Type *t = NULL;
    Token *tk = tok(p);
    switch (tk->kind) {
    case K_INT: t = type_int(); break;
    case K_LONG: t = type_long(); break;
    case K_DOUBLE: t = type_double(); break;
    case K_CHAR: t = type_char(); break;
    case K_STRING: t = type_ptr(type_char()); break;
    case K_BOOL: t = type_bool(); break;
    case K_VOID: t = type_void(); break;
    case K_VALIST: t = type_valist(); break;
    case K_STRUCT: {
        next(p);
        char *name = expect_ident(p, "ರಚನೆಯ ಹೆಸರು");
        StructDef *s = struct_lookup(name);
        t = type_struct(s);
        free(name);
        goto stars;
    }
    case T_ID: {
        StructDef *s = struct_lookup(tk->text);
        if (s) {
            t = type_struct(s);
            break;
        }
    } /* fall through */
    default: syntax_error(p, "ಪ್ರಕಾರ (type) ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು, ಆದರೆ %s ಸಿಕ್ಕಿತು", describe(tk));
    }
    next(p);
stars:
    while (kind(p) == P_STAR) {
        next(p);
        t = type_ptr(t);
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static Expr *parse_expr(Parser *p);
static Expr *parse_unary(Parser *p);

static Expr *parse_primary(Parser *p) {
    Token *t = tok(p);
    Loc l = t->loc;
    Expr *e;
    switch (t->kind) {
    case T_INT:
        next(p);
        e = expr_new(E_INT, l);
        e->ival = t->ival;
        return e;
    case T_FLOAT:
        next(p);
        e = expr_new(E_FLOAT, l);
        e->text = t->text;
        return e;
    case T_CHAR:
        next(p);
        e = expr_new(E_CHAR, l);
        e->ival = t->ival;
        e->wide = t->wide;
        return e;
    case T_STR: {
        StrBuf sb;
        sb_init(&sb);
        while (kind(p) == T_STR) {
            Token *s = next(p);
            sb_putn(&sb, s->text, s->len);
        }
        e = expr_new(E_STR, l);
        e->text = sb.data;
        e->len = sb.len;
        return e;
    }
    case K_TRUE:
    case K_FALSE:
        next(p);
        e = expr_new(E_BOOL, l);
        e->ival = t->kind == K_TRUE;
        return e;
    case K_NULL:
        next(p);
        return expr_new(E_NULL, l);
    case T_ID:
        if (struct_lookup(t->text))
            syntax_error(p, "'%s' ಒಂದು ರಚನೆಯ ಹೆಸರು; ಇಲ್ಲಿ ಮೌಲ್ಯ ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು", t->text);
        next(p);
        e = expr_new(E_VAR, l);
        e->name = xstrdup(t->text);
        return e;
    case P_LPAREN:
        next(p);
        e = parse_expr(p);
        expect(p, P_RPAREN);
        return e;
    case K_VA_START:
        next(p);
        expect(p, P_LPAREN);
        e = expr_new(E_VA_START, l);
        e->a = parse_expr(p);
        expect(p, P_RPAREN);
        return e;
    case K_VA_END:
        next(p);
        expect(p, P_LPAREN);
        e = expr_new(E_VA_END, l);
        e->a = parse_expr(p);
        expect(p, P_RPAREN);
        return e;
    case K_VA_ARG:
        next(p);
        expect(p, P_LPAREN);
        e = expr_new(E_VA_ARG, l);
        e->a = parse_expr(p);
        expect(p, P_COMMA);
        e->to_type = parse_type(p);
        expect(p, P_RPAREN);
        return e;
    case K_VA_COPY:
        next(p);
        expect(p, P_LPAREN);
        e = expr_new(E_VA_COPY, l);
        e->a = parse_expr(p);
        expect(p, P_COMMA);
        e->b = parse_expr(p);
        expect(p, P_RPAREN);
        return e;
    default:
        syntax_error(p, "ಅಭಿವ್ಯಕ್ತಿ (expression) ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು, ಆದರೆ %s ಸಿಕ್ಕಿತು", describe(t));
        return NULL;
    }
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        Loc l = loc(p);
        if (accept(p, P_LBRACKET)) {
            Expr *ix = expr_new(E_INDEX, l);
            ix->a = e;
            ix->b = parse_expr(p);
            expect(p, P_RBRACKET);
            e = ix;
        } else if (kind(p) == P_LPAREN) {
            next(p);
            Expr *call = expr_new(E_CALL, e->loc);
            call->a = e;
            if (kind(p) != P_RPAREN) {
                do {
                    vec_push(&call->args, parse_expr(p));
                } while (accept(p, P_COMMA));
            }
            expect(p, P_RPAREN);
            e = call;
        } else if (kind(p) == P_DOT || kind(p) == P_ARROW) {
            bool arrow = next(p)->kind == P_ARROW;
            Expr *m = expr_new(E_MEMBER, l);
            m->a = e;
            m->arrow = arrow;
            m->name = expect_ident(p, "ಸದಸ್ಯನ ಹೆಸರು");
            e = m;
        } else {
            return e;
        }
    }
}

static Expr *parse_unary(Parser *p) {
    Token *t = tok(p);
    Loc l = t->loc;
    switch (t->kind) {
    case K_NOT:
    case P_BANG:
    case P_MINUS:
    case P_PLUS:
    case P_TILDE: {
        next(p);
        Expr *e = expr_new(E_UNARY, l);
        e->op = (t->kind == K_NOT) ? P_BANG : t->kind;
        e->a = parse_unary(p);
        return e;
    }
    case P_AMP: {
        next(p);
        Expr *e = expr_new(E_ADDR, l);
        e->a = parse_unary(p);
        return e;
    }
    case P_STAR: {
        next(p);
        Expr *e = expr_new(E_DEREF, l);
        e->a = parse_unary(p);
        return e;
    }
    case K_SIZEOF: {
        next(p);
        if (kind(p) == P_LPAREN && is_type_start_at(p, 1)) {
            next(p);
            Expr *e = expr_new(E_SIZEOF_TYPE, l);
            e->to_type = parse_type(p);
            expect(p, P_RPAREN);
            return e;
        }
        Expr *e = expr_new(E_SIZEOF_EXPR, l);
        e->a = parse_unary(p);
        return e;
    }
    case P_INC:
    case P_DEC:
        syntax_error(p, "'%s' ಅನ್ನು ಹೇಳಿಕೆಯಾಗಿ ಮಾತ್ರ ಬಳಸಬಹುದು (ಉದಾ: x++;)", tok_spelling(t->kind));
        return NULL;
    case P_LPAREN:
        if (is_type_start_at(p, 1)) {
            next(p);
            Expr *e = expr_new(E_CAST, l);
            e->to_type = parse_type(p);
            expect(p, P_RPAREN);
            e->a = parse_unary(p);
            return e;
        }
        return parse_postfix(p);
    default:
        return parse_postfix(p);
    }
}

/* binary operator precedence; word operators map to the C symbol */
static int binop_info(TokKind k, int *op) {
    switch (k) {
    case K_OR: case P_OROR: *op = P_OROR; return 1;
    case K_AND: case P_ANDAND: *op = P_ANDAND; return 2;
    case P_PIPE: *op = P_PIPE; return 3;
    case P_CARET: *op = P_CARET; return 4;
    case P_AMP: *op = P_AMP; return 5;
    case K_EQ: case P_EQ: *op = P_EQ; return 6;
    case K_NE: case P_NE: *op = P_NE; return 6;
    case K_GT: case P_GT: *op = P_GT; return 7;
    case K_LT: case P_LT: *op = P_LT; return 7;
    case K_GE: case P_GE: *op = P_GE; return 7;
    case K_LE: case P_LE: *op = P_LE; return 7;
    case P_SHL: *op = P_SHL; return 8;
    case P_SHR: *op = P_SHR; return 8;
    case K_ADD: case P_PLUS: *op = P_PLUS; return 9;
    case K_SUB: case P_MINUS: *op = P_MINUS; return 9;
    case K_MUL: case P_STAR: *op = P_STAR; return 10;
    case K_DIV: case P_SLASH: *op = P_SLASH; return 10;
    case K_MOD: case P_PERCENT: *op = P_PERCENT; return 10;
    default: return 0;
    }
}

static Expr *parse_binary(Parser *p, int min_prec) {
    Expr *lhs = parse_unary(p);
    for (;;) {
        int op;
        int prec = binop_info(kind(p), &op);
        if (prec == 0 || prec < min_prec) return lhs;
        Loc l = loc(p);
        next(p);
        Expr *rhs = parse_binary(p, prec + 1);
        Expr *e = expr_new(E_BINARY, l);
        e->op = op;
        e->a = lhs;
        e->b = rhs;
        lhs = e;
    }
}

static Expr *parse_conditional(Parser *p) {
    Expr *c = parse_binary(p, 1);
    if (kind(p) == P_QUESTION) {
        Loc l = loc(p);
        next(p);
        Expr *e = expr_new(E_COND, l);
        e->a = c;
        e->b = parse_expr(p);
        expect(p, P_COLON);
        e->c = parse_conditional(p);
        return e;
    }
    return c;
}

static Expr *parse_expr(Parser *p) { return parse_conditional(p); }

static Expr *parse_initializer(Parser *p) {
    if (kind(p) == P_LBRACE) {
        Expr *e = expr_new(E_INITLIST, loc(p));
        next(p);
        while (kind(p) != P_RBRACE) {
            vec_push(&e->args, parse_initializer(p));
            if (!accept(p, P_COMMA)) break;
        }
        expect(p, P_RBRACE);
        return e;
    }
    return parse_expr(p);
}

/* ------------------------------------------------------------------ */
/* declarations                                                        */
/* ------------------------------------------------------------------ */

/* Array suffixes '[n]...' applied to 'base'. The outermost dimension is
 * written first, so a[2][3] is array(2) of array(3). */
static Type *parse_array_suffixes(Parser *p, Type *base, bool allow_empty_first) {
    Vec dims = {0};
    Vec locs = {0};
    while (kind(p) == P_LBRACKET) {
        Loc l = loc(p);
        next(p);
        Expr *len = NULL;
        if (kind(p) != P_RBRACKET) {
            len = parse_expr(p);
        } else if (!allow_empty_first || dims.len > 0) {
            syntax_error(p, "ಸರಣಿಯ ಗಾತ್ರ ಬೇಕು");
        }
        expect(p, P_RBRACKET);
        vec_push(&dims, len);
        Loc *pl = xmalloc(sizeof(Loc));
        *pl = l;
        vec_push(&locs, pl);
    }
    Type *t = base;
    for (int i = dims.len - 1; i >= 0; i--) t = type_array(t, -1, dims.items[i]);
    free(dims.items);
    for (int i = 0; i < locs.len; i++) free(locs.items[i]);
    free(locs.items);
    return t;
}

/* 'ಸ್ಥಿರ'? type declarator (',' declarator)*  (no trailing ';') */
static void parse_var_decls(Parser *p, Vec *out, bool is_global) {
    bool is_const = accept(p, K_CONST);
    Type *base = parse_type(p);
    do {
        VarDecl *v = xcalloc(1, sizeof(VarDecl));
        v->loc = loc(p);
        v->name = expect_ident(p, "ಚರದ ಹೆಸರು");
        v->type = parse_array_suffixes(p, base, true);
        v->is_const = is_const;
        v->is_global = is_global;
        if (accept(p, P_ASSIGN)) v->init = parse_initializer(p);
        vec_push(out, v);
    } while (accept(p, P_COMMA));
}

static void parse_params(Parser *p, FuncDecl *f) {
    expect(p, P_LPAREN);
    if (kind(p) == K_VOID && peek_at(p, 1)->kind == P_RPAREN) {
        next(p);
        next(p);
        return;
    }
    if (kind(p) != P_RPAREN) {
        for (;;) {
            if (accept(p, P_ELLIPSIS)) {
                f->variadic = true;
                break;
            }
            Param *pr = xcalloc(1, sizeof(Param));
            pr->is_const = accept(p, K_CONST);
            pr->type = parse_type(p);
            pr->loc = loc(p);
            pr->name = expect_ident(p, "ನಿಯತಾಂಕದ (parameter) ಹೆಸರು");
            pr->type = parse_array_suffixes(p, pr->type, true);
            vec_push(&f->params, pr);
            if (!accept(p, P_COMMA)) break;
        }
    }
    expect(p, P_RPAREN);
}

static Stmt *parse_block(Parser *p);

static FuncDecl *parse_function_header(Parser *p, bool is_extern) {
    expect(p, K_FUNC);
    FuncDecl *f = xcalloc(1, sizeof(FuncDecl));
    f->loc = loc(p);
    f->name = expect_ident(p, "ಕ್ರಿಯೆಯ ಹೆಸರು");
    f->is_extern = is_extern;
    parse_params(p, f);
    if (accept(p, K_RETURNS))
        f->ret = parse_type(p);
    else
        f->ret = type_void();
    return f;
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static Stmt *parse_statement(Parser *p);

static bool is_assign_op(TokKind k) {
    switch (k) {
    case P_ASSIGN: case P_PLUSEQ: case P_MINUSEQ: case P_STAREQ: case P_SLASHEQ:
    case P_PERCENTEQ: case P_AMPEQ: case P_PIPEEQ: case P_CARETEQ: case P_SHLEQ:
    case P_SHREQ:
        return true;
    default:
        return false;
    }
}

/* assignment | inc/dec | call (no ';') */
static Stmt *parse_simple(Parser *p) {
    Loc l = loc(p);
    if (kind(p) == P_INC || kind(p) == P_DEC) {
        Stmt *s = stmt_new(S_INCDEC, l);
        s->op = next(p)->kind;
        s->prefix = true;
        s->lhs = parse_unary(p);
        return s;
    }
    Expr *e = parse_expr(p);
    if (kind(p) == K_TO) {
        next(p);
        Stmt *s = stmt_new(S_ASSIGN, l);
        s->op = P_ASSIGN;
        s->lhs = e;
        s->rhs = parse_expr(p);
        if (kind(p) != K_SET)
            syntax_error(p, "'ಗೆ ... ನಿಗದಿ' ನಿಯೋಜನೆಯಲ್ಲಿ 'ನಿಗದಿ' ಕಾಣೆಯಾಗಿದೆ (%s ಸಿಕ್ಕಿತು)", describe(tok(p)));
        next(p);
        return s;
    }
    if (is_assign_op(kind(p))) {
        Stmt *s = stmt_new(S_ASSIGN, l);
        s->op = next(p)->kind;
        s->lhs = e;
        s->rhs = parse_expr(p);
        return s;
    }
    if (kind(p) == P_INC || kind(p) == P_DEC) {
        Stmt *s = stmt_new(S_INCDEC, l);
        s->op = next(p)->kind;
        s->lhs = e;
        return s;
    }
    Stmt *s = stmt_new(S_EXPR, l);
    s->expr = e;
    return s;
}

static void parse_simple_list(Parser *p, Vec *out) {
    do {
        vec_push(out, parse_simple(p));
    } while (accept(p, P_COMMA));
}

/* Index of the ')' matching the '(' at the current position, or -1. */
static int matching_paren(Parser *p) {
    int depth = 0;
    for (int i = p->pos; i < p->ntok; i++) {
        TokKind k = p->toks[i].kind;
        if (k == P_LPAREN) depth++;
        else if (k == P_RPAREN) {
            if (--depth == 0) return i;
        } else if (k == T_EOF || k == P_SEMI || k == P_LBRACE || k == P_RBRACE) {
            return -1;
        }
    }
    return -1;
}

static Stmt *parse_if_or_while(Parser *p) {
    Loc l = loc(p);
    expect(p, P_LPAREN);
    Expr *cond = parse_expr(p);
    expect(p, P_RPAREN);
    if (accept(p, K_IF)) {
        Stmt *s = stmt_new(S_IF, l);
        s->cond = cond;
        s->then = parse_statement(p);
        if (accept(p, K_ELSE)) s->els = parse_statement(p);
        return s;
    }
    expect(p, K_WHILE);
    Stmt *s = stmt_new(S_WHILE, l);
    s->cond = cond;
    s->body = parse_statement(p);
    return s;
}

static Stmt *parse_for(Parser *p) {
    Stmt *s = stmt_new(S_FOR, loc(p));
    expect(p, K_FOR);
    expect(p, P_LPAREN);
    if (kind(p) != P_SEMI) {
        if (kind(p) == K_CONST || is_type_start(p)) {
            Stmt *d = stmt_new(S_DECL, loc(p));
            parse_var_decls(p, &d->decls, false);
            vec_push(&s->init, d);
        } else {
            parse_simple_list(p, &s->init);
        }
    }
    expect(p, P_SEMI);
    if (kind(p) != P_SEMI) s->cond = parse_expr(p);
    expect(p, P_SEMI);
    if (kind(p) != P_RPAREN) parse_simple_list(p, &s->update);
    expect(p, P_RPAREN);
    s->body = parse_statement(p);
    return s;
}

static Stmt *parse_switch(Parser *p) {
    Stmt *s = stmt_new(S_SWITCH, loc(p));
    expect(p, K_SWITCH);
    expect(p, P_LPAREN);
    s->cond = parse_expr(p);
    expect(p, P_RPAREN);
    expect(p, P_LBRACE);
    while (kind(p) != P_RBRACE && kind(p) != T_EOF) {
        SwitchCase *c = xcalloc(1, sizeof(SwitchCase));
        c->loc = loc(p);
        if (accept(p, K_CASE)) {
            c->value = parse_expr(p);
        } else if (!accept(p, K_DEFAULT)) {
            syntax_error(p, "'ಸಂದರ್ಭ' ಅಥವಾ 'ಪೂರ್ವನಿಯೋಜಿತ' ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು, ಆದರೆ %s ಸಿಕ್ಕಿತು",
                         describe(tok(p)));
        }
        expect(p, P_COLON);
        while (kind(p) != K_CASE && kind(p) != K_DEFAULT && kind(p) != P_RBRACE && kind(p) != T_EOF)
            vec_push(&c->stmts, parse_statement(p));
        vec_push(&s->cases, c);
    }
    expect(p, P_RBRACE);
    return s;
}

static Stmt *parse_statement(Parser *p) {
    Loc l = loc(p);
    switch (kind(p)) {
    case P_LBRACE:
        return parse_block(p);
    case P_SEMI:
        next(p);
        return stmt_new(S_EMPTY, l);
    case K_FOR:
        return parse_for(p);
    case K_SWITCH:
        return parse_switch(p);
    case K_DO: {
        next(p);
        Stmt *s = stmt_new(S_DOWHILE, l);
        s->body = parse_statement(p);
        expect(p, P_LPAREN);
        s->cond = parse_expr(p);
        expect(p, P_RPAREN);
        expect(p, K_WHILE);
        expect(p, P_SEMI);
        return s;
    }
    case K_BREAK:
        next(p);
        expect(p, P_SEMI);
        return stmt_new(S_BREAK, l);
    case K_CONTINUE:
        next(p);
        expect(p, P_SEMI);
        return stmt_new(S_CONTINUE, l);
    case K_RETURN: {
        next(p);
        Stmt *s = stmt_new(S_RETURN, l);
        if (kind(p) != P_SEMI) s->expr = parse_expr(p);
        expect(p, P_SEMI);
        return s;
    }
    case K_PRINT:
    case K_PRINTLN: {
        Stmt *s = stmt_new(S_PRINT, l);
        s->newline = next(p)->kind == K_PRINTLN;
        expect(p, P_LPAREN);
        if (kind(p) != P_RPAREN) {
            do {
                vec_push(&s->args, parse_expr(p));
            } while (accept(p, P_COMMA));
        }
        expect(p, P_RPAREN);
        expect(p, P_SEMI);
        return s;
    }
    case K_READ: {
        next(p);
        Stmt *s = stmt_new(S_READ, l);
        expect(p, P_LPAREN);
        do {
            vec_push(&s->args, parse_unary(p));
        } while (accept(p, P_COMMA));
        expect(p, P_RPAREN);
        expect(p, P_SEMI);
        return s;
    }
    case K_IF:
        syntax_error(p, "'ಆದರೆ' ಷರತ್ತಿನ ನಂತರ ಬರಬೇಕು: (ಷರತ್ತು) ಆದರೆ { ... }");
        return NULL;
    case K_ELSE:
        syntax_error(p, "'ಇಲ್ಲವಾದರೆ' ಗೆ ಹೊಂದುವ '(ಷರತ್ತು) ಆದರೆ' ಇಲ್ಲ");
        return NULL;
    case K_WHILE:
        syntax_error(p, "'ಸಮಯವರೆಗೆ' ಷರತ್ತಿನ ನಂತರ ಬರಬೇಕು: (ಷರತ್ತು) ಸಮಯವರೆಗೆ { ... }");
        return NULL;
    case K_FUNC:
        syntax_error(p, "ಕ್ರಿಯೆಯ ಒಳಗೆ ಇನ್ನೊಂದು ಕ್ರಿಯೆಯನ್ನು ವ್ಯಾಖ್ಯಾನಿಸಲಾಗದು");
        return NULL;
    case P_LPAREN: {
        int close = matching_paren(p);
        if (close > 0 && close + 1 < p->ntok) {
            TokKind after = p->toks[close + 1].kind;
            if (after == K_IF || after == K_WHILE) return parse_if_or_while(p);
        }
        break;
    }
    default:
        break;
    }
    if (kind(p) == K_CONST || is_type_start(p)) {
        /* 'ರಚನೆ X {' inside a function is not allowed */
        if (kind(p) == K_STRUCT && peek_at(p, 2)->kind == P_LBRACE)
            syntax_error(p, "ರಚನೆಯನ್ನು ಕಡತದ ಮೇಲ್ಮಟ್ಟದಲ್ಲಿ ಮಾತ್ರ ವ್ಯಾಖ್ಯಾನಿಸಬಹುದು");
        Stmt *s = stmt_new(S_DECL, l);
        parse_var_decls(p, &s->decls, false);
        expect(p, P_SEMI);
        return s;
    }
    Stmt *s = parse_simple(p);
    expect(p, P_SEMI);
    return s;
}

/* skip to a likely statement boundary after a syntax error */
static void sync_statement(Parser *p) {
    int depth = 0;
    while (kind(p) != T_EOF) {
        TokKind k = kind(p);
        if (k == P_LBRACE) depth++;
        if (k == P_RBRACE) {
            if (depth == 0) return;
            depth--;
            next(p);
            if (depth == 0) return;
            continue;
        }
        next(p);
        if (k == P_SEMI && depth == 0) return;
    }
}

static Stmt *parse_block(Parser *p) {
    Stmt *b = stmt_new(S_BLOCK, loc(p));
    expect(p, P_LBRACE);
    while (kind(p) != P_RBRACE && kind(p) != T_EOF) {
        jmp_buf saved;
        memcpy(saved, p->recover, sizeof(jmp_buf));
        int start = p->pos;
        if (setjmp(p->recover) == 0) {
            vec_push(&b->stmts, parse_statement(p));
        } else {
            if (p->pos == start) next(p);
            sync_statement(p);
        }
        memcpy(p->recover, saved, sizeof(jmp_buf));
    }
    expect(p, P_RBRACE);
    return b;
}

/* ------------------------------------------------------------------ */
/* top level                                                           */
/* ------------------------------------------------------------------ */

static void parse_struct_decl(Parser *p, Program *prog) {
    expect(p, K_STRUCT);
    Loc l = loc(p);
    char *name = expect_ident(p, "ರಚನೆಯ ಹೆಸರು");
    StructDef *s = struct_lookup(name);
    if (!s->declared) s->loc = l;
    s->declared = true;
    if (accept(p, P_SEMI)) return; /* opaque declaration */
    if (s->complete) diag_error(l, "ರಚನೆ '%s' ಅನ್ನು ಈಗಾಗಲೇ ವ್ಯಾಖ್ಯಾನಿಸಲಾಗಿದೆ", name);
    s->loc = l;
    expect(p, P_LBRACE);
    Vec fields = {0};
    while (kind(p) != P_RBRACE && kind(p) != T_EOF) {
        Type *base = parse_type(p);
        do {
            Field *f = xcalloc(1, sizeof(Field));
            f->loc = loc(p);
            f->name = expect_ident(p, "ಸದಸ್ಯನ ಹೆಸರು");
            f->type = parse_array_suffixes(p, base, false);
            vec_push(&fields, f);
        } while (accept(p, P_COMMA));
        expect(p, P_SEMI);
    }
    expect(p, P_RBRACE);
    accept(p, P_SEMI);
    if (!s->complete) {
        s->fields = fields;
        s->complete = true;
        TopDecl *d = xcalloc(1, sizeof(TopDecl));
        d->kind = D_STRUCT;
        d->sdef = s;
        vec_push(&prog->decls, d);
    }
}

static void parse_extern(Parser *p, Program *prog) {
    expect(p, K_EXTERN);
    char *cname = NULL;
    if (kind(p) == T_STR) cname = xstrdup(next(p)->text);
    if (kind(p) == K_FUNC) {
        FuncDecl *f = parse_function_header(p, true);
        f->cname = cname;
        expect(p, P_SEMI);
        TopDecl *d = xcalloc(1, sizeof(TopDecl));
        d->kind = D_FUNC;
        d->func = f;
        vec_push(&prog->decls, d);
        return;
    }
    VarDecl *v = xcalloc(1, sizeof(VarDecl));
    v->is_const = accept(p, K_CONST);
    v->type = parse_type(p);
    v->loc = loc(p);
    v->name = expect_ident(p, "ಚರದ ಹೆಸರು");
    v->type = parse_array_suffixes(p, v->type, false);
    v->is_extern = true;
    v->is_global = true;
    v->cname = cname;
    expect(p, P_SEMI);
    TopDecl *d = xcalloc(1, sizeof(TopDecl));
    d->kind = D_VAR;
    d->var = v;
    vec_push(&prog->decls, d);
}

static void parse_top_level(Parser *p, Program *prog) {
    switch (kind(p)) {
    case K_IMPORT:
        syntax_error(p, "'ಆಮದು' ಕಡತದ ಆರಂಭದಲ್ಲಿ, ಇತರ ಘೋಷಣೆಗಳಿಗಿಂತ ಮೊದಲು ಬರಬೇಕು");
        return;
    case K_STRUCT:
        if (peek_at(p, 1)->kind == T_ID &&
            (peek_at(p, 2)->kind == P_LBRACE || peek_at(p, 2)->kind == P_SEMI)) {
            parse_struct_decl(p, prog);
            return;
        }
        break;
    case K_FUNC: {
        FuncDecl *f = parse_function_header(p, false);
        if (kind(p) == P_SEMI)
            syntax_error(p, "ಕ್ರಿಯೆಯ ದೇಹ { ... } ಬೇಕು (ಮುಂಗಡ ಘೋಷಣೆ ಅಗತ್ಯವಿಲ್ಲ; C ಕ್ರಿಯೆಗಳಿಗೆ 'ಬಾಹ್ಯ' ಬಳಸಿ)");
        f->body = parse_block(p);
        TopDecl *d = xcalloc(1, sizeof(TopDecl));
        d->kind = D_FUNC;
        d->func = f;
        vec_push(&prog->decls, d);
        return;
    }
    case K_EXTERN:
        parse_extern(p, prog);
        return;
    default:
        break;
    }
    if (kind(p) == K_CONST || is_type_start(p)) {
        Vec vars = {0};
        parse_var_decls(p, &vars, true);
        expect(p, P_SEMI);
        for (int i = 0; i < vars.len; i++) {
            TopDecl *d = xcalloc(1, sizeof(TopDecl));
            d->kind = D_VAR;
            d->var = vars.items[i];
            vec_push(&prog->decls, d);
        }
        free(vars.items);
        return;
    }
    syntax_error(p, "ಘೋಷಣೆ ನಿರೀಕ್ಷಿಸಲಾಗಿತ್ತು ('ಕ್ರಿಯೆ', 'ರಚನೆ', 'ಬಾಹ್ಯ' ಅಥವಾ ಚರ), ಆದರೆ %s ಸಿಕ್ಕಿತು",
                 describe(tok(p)));
}

static void sync_top_level(Parser *p) {
    int depth = 0;
    while (kind(p) != T_EOF) {
        TokKind k = kind(p);
        if (depth == 0 && (k == K_FUNC || k == K_EXTERN || k == K_STRUCT)) return;
        if (k == P_LBRACE) depth++;
        if (k == P_RBRACE && depth > 0) {
            depth--;
            next(p);
            if (depth == 0) return;
            continue;
        }
        next(p);
        if (k == P_SEMI && depth == 0) return;
    }
}

void parse_imports(Parser *p, Vec *imports) {
    while (kind(p) == K_IMPORT) {
        if (setjmp(p->recover) == 0) {
            next(p);
            Token *t = tok(p);
            if (t->kind != T_STR) syntax_error(p, "'ಆಮದು' ನಂತರ ಘಟಕದ ಹೆಸರು \"...\" ಬೇಕು");
            next(p);
            vec_push(imports, t);
            expect(p, P_SEMI);
        } else {
            sync_top_level(p);
        }
    }
}

void parse_file_body(Parser *p, Program *prog) {
    while (kind(p) != T_EOF) {
        int start = p->pos;
        if (setjmp(p->recover) == 0) {
            parse_top_level(p, prog);
        } else {
            if (p->pos == start) next(p);
            sync_top_level(p);
        }
    }
}
