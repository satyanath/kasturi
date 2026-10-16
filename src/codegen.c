/* codegen.c - emits C99 from the checked AST */
#include "codegen.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "sema.h"

static StrBuf out;
static int indent_level;

static void emit(const char *s) { sb_puts(&out, s); }

static void emitf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = xvformat(fmt, ap);
    va_end(ap);
    sb_puts(&out, s);
    free(s);
}

static void indent(void) {
    for (int i = 0; i < indent_level; i++) emit("    ");
}

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

static const char *base_c_name(Type *t) {
    switch (t->kind) {
    case TY_VOID: return "void";
    case TY_BOOL: return "kst_bool";
    case TY_CHAR: return "char";
    case TY_INT: return "int";
    case TY_LONG: return "long long";
    case TY_DOUBLE: return "double";
    case TY_VALIST: return "va_list";
    case TY_STRUCT: return t->sdef->cname;
    case TY_NULLPTR: return "void *";
    default: return "int";
    }
}

/* C declaration of 'name' with type 't' (name may be ""). */
static char *c_decl(Type *t, const char *name) {
    char *inner = xstrdup(name);
    Type *cur = t;
    while (cur->kind == TY_PTR || cur->kind == TY_ARRAY) {
        char *next;
        if (cur->kind == TY_PTR) {
            if (cur->base->kind == TY_ARRAY)
                next = xformat("(*%s)", inner);
            else
                next = xformat("*%s", inner);
        } else {
            next = xformat("%s[%lld]", inner, cur->len < 1 ? 1LL : cur->len);
        }
        free(inner);
        inner = next;
        cur = cur->base;
    }
    char *r = *inner ? xformat("%s %s", base_c_name(cur), inner) : xstrdup(base_c_name(cur));
    free(inner);
    return r;
}

static void emit_decl(Type *t, const char *name) {
    char *d = c_decl(t, name);
    emit(d);
    free(d);
}

/* ------------------------------------------------------------------ */
/* literals                                                            */
/* ------------------------------------------------------------------ */

static void emit_c_string(const char *s, size_t n) {
    emit("\"");
    size_t chunk = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (chunk >= 2000) {
            emit("\" \"");
            chunk = 0;
        }
        switch (c) {
        case '"': emit("\\\""); break;
        case '\\': emit("\\\\"); break;
        case '\n': emit("\\n"); break;
        case '\t': emit("\\t"); break;
        case '\r': emit("\\r"); break;
        case '?': emit("\\?"); break;
        default:
            if (c < 32 || c >= 127)
                emitf("\\%03o", c);
            else
                sb_putc(&out, (char)c);
        }
        chunk++;
    }
    emit("\"");
}

static void emit_int_literal(long long v) {
    if (v == LLONG_MIN)
        emit("(-9223372036854775807LL - 1)");
    else if (v < 0)
        emitf("(%lld%s)", v, v < INT_MIN ? "LL" : "");
    else
        emitf("%lld%s", v, v > INT_MAX ? "LL" : "");
}

static void emit_double_literal(double d) {
    /* C compilers reject a literal 0.0/0.0 but accept an overflowing product */
    if (isnan(d)) {
        emit("(1e308 * 10.0 * 0.0)");
        return;
    }
    if (isinf(d)) {
        emit(d > 0 ? "(1e308 * 10.0)" : "(-1e308 * 10.0)");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof buf, "%.17g", d);
    if (!strpbrk(buf, ".eE")) strcat(buf, ".0");
    if (d < 0)
        emitf("(%s)", buf);
    else
        emit(buf);
}

static void emit_const(ConstVal c, Type *t) {
    if (t->kind == TY_DOUBLE) {
        emit_double_literal(c.is_float ? c.f : (double)c.i);
        return;
    }
    long long v = c.is_float ? (long long)c.f : c.i;
    if (t->kind == TY_INT || t->kind == TY_LONG) {
        emit_int_literal(v);
    } else {
        emitf("((%s)", base_c_name(t));
        emit_int_literal(v);
        emit(")");
    }
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static void gen_expr(Expr *e);

static const char *c_op(int op) {
    switch (op) {
    case P_PLUS: return "+";
    case P_MINUS: return "-";
    case P_STAR: return "*";
    case P_SLASH: return "/";
    case P_PERCENT: return "%";
    case P_AMP: return "&";
    case P_PIPE: return "|";
    case P_CARET: return "^";
    case P_SHL: return "<<";
    case P_SHR: return ">>";
    case P_EQ: return "==";
    case P_NE: return "!=";
    case P_LT: return "<";
    case P_GT: return ">";
    case P_LE: return "<=";
    case P_GE: return ">=";
    case P_ANDAND: return "&&";
    case P_OROR: return "||";
    case P_BANG: return "!";
    case P_TILDE: return "~";
    case P_ASSIGN: return "=";
    case P_PLUSEQ: return "+=";
    case P_MINUSEQ: return "-=";
    case P_STAREQ: return "*=";
    case P_SLASHEQ: return "/=";
    case P_PERCENTEQ: return "%=";
    case P_AMPEQ: return "&=";
    case P_PIPEEQ: return "|=";
    case P_CARETEQ: return "^=";
    case P_SHLEQ: return "<<=";
    case P_SHREQ: return ">>=";
    case P_INC: return "++";
    case P_DEC: return "--";
    default: return "?";
    }
}

static void gen_args(Vec *args) {
    for (int i = 0; i < args->len; i++) {
        if (i) emit(", ");
        gen_expr(args->items[i]);
    }
}

static void gen_expr(Expr *e) {
    if (e->folded) {
        emit_const(e->cv, e->type);
        return;
    }
    switch (e->kind) {
    case E_INT:
        emit_int_literal(e->ival);
        break;
    case E_FLOAT:
        emit(e->text);
        break;
    case E_STR:
        emit_c_string(e->text, e->len);
        break;
    case E_CHAR:
        if (e->wide)
            emitf("%lld", e->ival);
        else if (e->ival >= 32 && e->ival < 127 && e->ival != '\'' && e->ival != '\\')
            emitf("'%c'", (int)e->ival);
        else
            emitf("((char)%lld)", e->ival);
        break;
    case E_BOOL:
        emit(e->ival ? "((kst_bool)1)" : "((kst_bool)0)");
        break;
    case E_NULL:
        emit("((void *)0)");
        break;
    case E_VAR:
        emit(e->sym->cname);
        break;
    case E_UNARY:
        emitf("(%s", e->op == P_MINUS ? "-" : e->op == P_PLUS ? "+" : c_op(e->op));
        gen_expr(e->a);
        emit(")");
        break;
    case E_BINARY:
        if (e->op == P_SLASH && e->type && e->type->kind == TY_DOUBLE) {
            /* constant x / 0.0 is rejected by some C compilers; emit the IEEE result */
            ConstVal c = const_eval(e);
            if (c.ok) {
                emit_const(c, e->type);
                break;
            }
        }
        if (e->str_compare) {
            emit("(kst_str_cmp(");
            gen_expr(e->a);
            emit(", ");
            gen_expr(e->b);
            emitf(") %s 0)", c_op(e->op));
        } else {
            emit("(");
            gen_expr(e->a);
            emitf(" %s ", c_op(e->op));
            gen_expr(e->b);
            emit(")");
        }
        break;
    case E_COND:
        emit("(");
        gen_expr(e->a);
        emit(" ? ");
        gen_expr(e->b);
        emit(" : ");
        gen_expr(e->c);
        emit(")");
        break;
    case E_CALL:
        emitf("%s(", e->sym->cname);
        gen_args(&e->args);
        emit(")");
        break;
    case E_INDEX:
        emit("(");
        gen_expr(e->a);
        emit(")[");
        gen_expr(e->b);
        emit("]");
        break;
    case E_MEMBER:
        emit("(");
        gen_expr(e->a);
        emitf(")%s%s", e->arrow ? "->" : ".", e->field->cname);
        break;
    case E_CAST: {
        Type *to = e->to_type;
        if (to->kind == TY_BOOL) {
            emit("((kst_bool)((");
            gen_expr(e->a);
            emit(") != 0))");
        } else if (to->kind == TY_VOID) {
            emit("((void)(");
            gen_expr(e->a);
            emit("))");
        } else if (to->kind == TY_STRUCT) {
            gen_expr(e->a);
        } else {
            emit("((");
            emit_decl(to, "");
            emit(")(");
            gen_expr(e->a);
            emit("))");
        }
        break;
    }
    case E_SIZEOF_TYPE:
        emit("((long long)sizeof(");
        emit_decl(e->to_type, "");
        emit("))");
        break;
    case E_SIZEOF_EXPR:
        emit("((long long)sizeof(");
        gen_expr(e->a);
        emit("))");
        break;
    case E_ADDR:
        if (e->a->type->kind == TY_ARRAY) {
            emit("(&(");
            gen_expr(e->a);
            emit(")[0])");
        } else {
            emit("(&");
            gen_expr(e->a);
            emit(")");
        }
        break;
    case E_DEREF:
        emit("(*");
        gen_expr(e->a);
        emit(")");
        break;
    case E_VA_START:
        emit("va_start(");
        gen_expr(e->a);
        emitf(", %s)", e->va_last ? e->va_last : "0");
        break;
    case E_VA_ARG: {
        Type *t = e->to_type;
        if (t->kind == TY_BOOL || t->kind == TY_CHAR) {
            emitf("((%s)va_arg(", base_c_name(t));
            gen_expr(e->a);
            emit(", int))");
        } else {
            emit("va_arg(");
            gen_expr(e->a);
            emit(", ");
            emit_decl(t, "");
            emit(")");
        }
        break;
    }
    case E_VA_END:
        emit("va_end(");
        gen_expr(e->a);
        emit(")");
        break;
    case E_VA_COPY:
        emit("va_copy(");
        gen_expr(e->a);
        emit(", ");
        gen_expr(e->b);
        emit(")");
        break;
    case E_INITLIST:
        emit("{");
        for (int i = 0; i < e->args.len; i++) {
            if (i) emit(", ");
            gen_expr(e->args.items[i]);
        }
        if (e->args.len == 0) emit("0");
        emit("}");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static void gen_stmt(Stmt *s);

static void gen_zero_init(Type *t) {
    switch (t->kind) {
    case TY_ARRAY:
    case TY_STRUCT: emit(" = {0}"); break;
    case TY_VALIST: break;
    default: emit(" = 0"); break;
    }
}

static void gen_var(VarDecl *v, bool global) {
    if (v->is_const && type_is_arith(v->type)) emit("const ");
    emit_decl(v->type, v->cname);
    if (v->init) {
        emit(" = ");
        gen_expr(v->init);
    } else if (!global) {
        gen_zero_init(v->type);
    }
    emit(";\n");
}

/* simple statement as a C expression (no ';') */
static void gen_simple_expr(Stmt *s) {
    switch (s->kind) {
    case S_ASSIGN:
        gen_expr(s->lhs);
        emitf(" %s ", c_op(s->op));
        gen_expr(s->rhs);
        break;
    case S_INCDEC:
        if (s->prefix) {
            emit(c_op(s->op));
            gen_expr(s->lhs);
        } else {
            gen_expr(s->lhs);
            emit(c_op(s->op));
        }
        break;
    case S_EXPR:
        gen_expr(s->expr);
        break;
    default:
        break;
    }
}

static void gen_body(Stmt *s) {
    if (s->kind == S_BLOCK) {
        gen_stmt(s);
        return;
    }
    emit("{\n");
    indent_level++;
    gen_stmt(s);
    indent_level--;
    indent();
    emit("}\n");
}

static void gen_print_value(Expr *a) {
    Type *t = a->type;
    if (t->kind == TY_ARRAY || (t->kind == TY_PTR && t->base->kind == TY_CHAR)) {
        emit("kst_print_str(");
    } else {
        switch (t->kind) {
        case TY_BOOL: emit("kst_print_bool("); break;
        case TY_CHAR: emit("kst_print_char("); break;
        case TY_INT:
        case TY_LONG: emit("kst_print_i64((long long)"); break;
        case TY_DOUBLE: emit("kst_print_f64("); break;
        default: emit("kst_print_ptr((const void *)"); break;
        }
    }
    gen_expr(a);
    emit(");\n");
}

static void gen_read_value(Expr *a) {
    Type *t = a->type;
    switch (t->kind) {
    case TY_ARRAY:
        emit("kst_read_word(");
        gen_expr(a);
        emitf(", %lldLL);\n", t->len);
        return;
    case TY_PTR:
        gen_expr(a);
        emit(" = kst_read_word_alloc();\n");
        return;
    case TY_INT: emit("kst_read_i32(&"); break;
    case TY_LONG: emit("kst_read_i64(&"); break;
    case TY_DOUBLE: emit("kst_read_f64(&"); break;
    case TY_CHAR: emit("kst_read_char(&"); break;
    case TY_BOOL: emit("kst_read_bool(&"); break;
    default: emit("(void)("); break;
    }
    gen_expr(a);
    emit(");\n");
}

static void gen_stmt(Stmt *s) {
    switch (s->kind) {
    case S_BLOCK:
        emit("{\n");
        indent_level++;
        for (int i = 0; i < s->stmts.len; i++) {
            indent();
            gen_stmt(s->stmts.items[i]);
        }
        indent_level--;
        indent();
        emit("}\n");
        break;
    case S_DECL:
        for (int i = 0; i < s->decls.len; i++) {
            if (i) indent();
            gen_var(s->decls.items[i], false);
        }
        break;
    case S_ASSIGN:
    case S_INCDEC:
    case S_EXPR:
        gen_simple_expr(s);
        emit(";\n");
        break;
    case S_IF:
        emit("if (");
        gen_expr(s->cond);
        emit(") ");
        gen_body(s->then);
        if (s->els) {
            indent();
            emit("else ");
            gen_body(s->els);
        }
        break;
    case S_WHILE:
        emit("while (");
        gen_expr(s->cond);
        emit(") ");
        gen_body(s->body);
        break;
    case S_DOWHILE:
        emit("do ");
        gen_body(s->body);
        indent();
        emit("while (");
        gen_expr(s->cond);
        emit(");\n");
        break;
    case S_FOR: {
        bool decl_init = s->init.len == 1 && ((Stmt *)s->init.items[0])->kind == S_DECL;
        if (decl_init) {
            emit("{\n");
            indent_level++;
            indent();
            gen_stmt(s->init.items[0]);
            indent();
            emit("for (;");
        } else {
            emit("for (");
            for (int i = 0; i < s->init.len; i++) {
                if (i) emit(", ");
                gen_simple_expr(s->init.items[i]);
            }
            emit(";");
        }
        if (s->cond) {
            emit(" ");
            gen_expr(s->cond);
        }
        emit(";");
        for (int i = 0; i < s->update.len; i++) {
            emit(i ? ", " : " ");
            gen_simple_expr(s->update.items[i]);
        }
        emit(") ");
        gen_body(s->body);
        if (decl_init) {
            indent_level--;
            indent();
            emit("}\n");
        }
        break;
    }
    case S_SWITCH:
        emit("switch (");
        gen_expr(s->cond);
        emit(") {\n");
        for (int i = 0; i < s->cases.len; i++) {
            SwitchCase *c = s->cases.items[i];
            indent();
            if (c->value) {
                emit("case ");
                emit_int_literal(c->cval);
                emit(": {\n");
            } else {
                emit("default: {\n");
            }
            indent_level++;
            for (int k = 0; k < c->stmts.len; k++) {
                indent();
                gen_stmt(c->stmts.items[k]);
            }
            indent_level--;
            indent();
            emit("}\n");
        }
        indent();
        emit("}\n");
        break;
    case S_BREAK:
        emit("break;\n");
        break;
    case S_CONTINUE:
        emit("continue;\n");
        break;
    case S_RETURN:
        if (s->expr) {
            emit("return ");
            gen_expr(s->expr);
            emit(";\n");
        } else {
            emit("return;\n");
        }
        break;
    case S_PRINT:
        emit("{\n");
        indent_level++;
        for (int i = 0; i < s->args.len; i++) {
            indent();
            gen_print_value(s->args.items[i]);
        }
        if (s->newline) {
            indent();
            emit("kst_print_newline();\n");
        }
        indent_level--;
        indent();
        emit("}\n");
        break;
    case S_READ:
        emit("{\n");
        indent_level++;
        for (int i = 0; i < s->args.len; i++) {
            indent();
            gen_read_value(s->args.items[i]);
        }
        indent_level--;
        indent();
        emit("}\n");
        break;
    case S_EMPTY:
        emit(";\n");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* program                                                             */
/* ------------------------------------------------------------------ */

static void gen_prototype(FuncDecl *f) {
    StrBuf params;
    sb_init(&params);
    sb_printf(&params, "%s(", f->cname);
    if (f->params.len == 0 && !f->variadic) sb_puts(&params, "void");
    for (int i = 0; i < f->params.len; i++) {
        Param *p = f->params.items[i];
        if (i) sb_puts(&params, ", ");
        const char *pname = (!f->is_extern && p->sym) ? p->sym->cname : "";
        char *d = c_decl(p->type, pname);
        sb_puts(&params, d);
        free(d);
    }
    if (f->variadic) sb_puts(&params, ", ...");
    sb_puts(&params, ")");
    emit_decl(f->ret, params.data);
    sb_free(&params);
}

static void gen_struct(StructDef *s) {
    if (s->mark == 3 || !s->complete) return;
    s->mark = 3;
    for (int i = 0; i < s->fields.len; i++) {
        Field *f = s->fields.items[i];
        Type *t = f->type;
        while (t->kind == TY_ARRAY) t = t->base;
        if (t->kind == TY_STRUCT) gen_struct(t->sdef);
    }
    emitf("struct %s {\n", s->cname);
    for (int i = 0; i < s->fields.len; i++) {
        Field *f = s->fields.items[i];
        emit("    ");
        emit_decl(f->type, f->cname);
        emit(";\n");
    }
    if (s->fields.len == 0) emit("    char kst_empty;\n");
    emit("};\n\n");
}

static const char *runtime_prototypes =
    "/* Kasturi runtime (see runtime/kasturi_rt.h) */\n"
    "typedef unsigned char kst_bool;\n"
    "void kst_rt_init(int argc, char **argv);\n"
    "void kst_rt_finish(void);\n"
    "int kst_rt_argc(void);\n"
    "char **kst_rt_argv(void);\n"
    "void kst_print_i64(long long v);\n"
    "void kst_print_f64(double v);\n"
    "void kst_print_str(const char *s);\n"
    "void kst_print_char(int c);\n"
    "void kst_print_bool(int b);\n"
    "void kst_print_ptr(const void *p);\n"
    "void kst_print_newline(void);\n"
    "int kst_read_i32(int *p);\n"
    "int kst_read_i64(long long *p);\n"
    "int kst_read_f64(double *p);\n"
    "int kst_read_char(char *p);\n"
    "int kst_read_bool(kst_bool *p);\n"
    "int kst_read_word(char *buf, long long size);\n"
    "char *kst_read_word_alloc(void);\n"
    "int kst_str_cmp(const char *a, const char *b);\n";

char *codegen_program(Program *prog, FuncDecl *main_fn, const char *source_name) {
    sb_init(&out);
    indent_level = 0;
    emit("/* Generated by the Kasturi compiler (kasturi) - do not edit.\n");
    emit(" * source: ");
    for (const char *p = source_name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 32 && c < 127 && !(c == '*' && p[1] == '/'))
            sb_putc(&out, (char)c);
        else
            sb_putc(&out, '?');
    }
    emit("\n */\n");
    emit("#include <stdarg.h>\n");
    emit("#ifndef va_copy\n#define va_copy(d, s) ((d) = (s))\n#endif\n\n");
    emit(runtime_prototypes);
    emit("\n");

    /* structs */
    Vec *structs = struct_list();
    for (int i = 0; i < structs->len; i++) {
        StructDef *s = structs->items[i];
        if (s->declared) emitf("typedef struct %s %s;\n", s->cname, s->cname);
    }
    emit("\n");
    for (int i = 0; i < structs->len; i++) gen_struct(structs->items[i]);

    /* external declarations (deduplicated by C name) */
    StrMap seen = {0};
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind == D_FUNC && d->func->is_extern) {
            if (map_get(&seen, d->func->cname)) continue;
            map_put(&seen, d->func->cname, d);
            gen_prototype(d->func);
            emit(";\n");
        } else if (d->kind == D_VAR && d->var->is_extern) {
            if (map_get(&seen, d->var->cname)) continue;
            map_put(&seen, d->var->cname, d);
            emit("extern ");
            emit_decl(d->var->type, d->var->cname);
            emit(";\n");
        }
    }
    emit("\n");

    /* global variables */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind == D_VAR && !d->var->is_extern) gen_var(d->var, true);
    }
    emit("\n");

    /* prototypes */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind == D_FUNC && !d->func->is_extern) {
            gen_prototype(d->func);
            emit(";\n");
        }
    }
    emit("\n");

    /* function bodies */
    for (int i = 0; i < prog->decls.len; i++) {
        TopDecl *d = prog->decls.items[i];
        if (d->kind != D_FUNC || d->func->is_extern) continue;
        gen_prototype(d->func);
        emit(" ");
        gen_stmt(d->func->body);
        emit("\n");
    }

    if (main_fn) {
        emit("int main(int argc, char **argv) {\n");
        emit("    int kst_status = 0;\n");
        emit("    kst_rt_init(argc, argv);\n");
        emit("    ");
        if (main_fn->ret->kind != TY_VOID) emit("kst_status = ");
        if (main_fn->params.len == 2)
            emitf("%s(kst_rt_argc(), kst_rt_argv());\n", main_fn->cname);
        else
            emitf("%s();\n", main_fn->cname);
        emit("    kst_rt_finish();\n");
        emit("    return kst_status;\n");
        emit("}\n");
    }
    return out.data;
}
