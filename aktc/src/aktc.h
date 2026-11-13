#ifndef AKTC_H
#define AKTC_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AKT_VERSION "0.9.0"

typedef enum {
    TK_EOF = 0,
    TK_IDENT, TK_INT, TK_FLOAT, TK_STRING, TK_FSTRING, TK_CHAR,
    TK_FN, TK_CONST, TK_STATIC, TK_TYPE, TK_STRUCT, TK_ENUM, TK_IMPL,
    TK_MANAGED,
    TK_PUB, TK_USE, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_IN, TK_LOOP,
    TK_SWITCH, TK_CASE, TK_DEFAULT, TK_BREAK, TK_CONTINUE, TK_RETURN,
    TK_DEFER, TK_ARENA, TK_SHARED, TK_WEAK, TK_MANUAL, TK_AUTO,
    TK_CINCLUDE, TK_CIMPORT, TK_CLINK, TK_CFN, TK_CSTRUCT, TK_CUNION,
    TK_ASYNC, TK_AWAIT, TK_INLINE, TK_TEST, TK_TRUE, TK_FALSE, TK_NULL,
    TK_SELF, TK_AS, TK_AND, TK_OR, TK_NOT, TK_STEP, TK_MOVE, TK_VOLATILE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT, TK_POW,
    TK_AMP, TK_PIPE, TK_CARET, TK_TILDE, TK_SHL, TK_SHR,
    TK_ANDAND, TK_OROR, TK_BANG,
    TK_EQ, TK_EQEQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_ASSIGN, TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ, TK_PERCENTEQ,
    TK_QUESTION, TK_QQ, TK_COLON, TK_COLONCOLON, TK_DOT, TK_ELLIPSIS, TK_COMMA, TK_SEMI,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
    TK_ARROW, TK_RANGE, TK_RANGE_INCL, TK_FATARROW, TK_AT
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    long long ival;
    double fval;
    int line;
    int col;
} Token;

typedef struct {
    const char *src;
    const char *file;
    size_t pos;
    int line;
    int col;
} Lexer;

Lexer lexer_new(const char *src, const char *file);
Token lexer_next(Lexer *lx);
const char *token_kind_name(TokenKind kind);
typedef struct Program Program;
void program_add_source(Program *prog, const char *src);
void program_add_import(Program *prog, const char *inc);
typedef enum {
    TY_UNKNOWN = 0, TY_VOID, TY_INT, TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64, TY_F32, TY_F64, TY_BOOL, TY_STRING,
    TY_CHAR, TY_CCHAR, TY_NAMED, TY_ARRAY, TY_SLICE, TY_PTR, TY_REF,
    TY_VECTOR, TY_OPTION, TY_RESULT, TY_MAP, TY_SET, TY_SHARED, TY_WEAK, TY_TASK, TY_CFUNC
} TypeKind;

typedef struct Type Type;
struct Type {
    TypeKind kind;
    char *name;
    Type *elem;
    Type *elem2;
    long long array_len;
    int is_const;
};

Type *type_new(TypeKind kind, const char *name);
Type *type_named(const char *name);
Type *type_array(Type *elem, long long len);
Type *type_slice(Type *elem);
Type *type_ptr(Type *elem);
Type *type_ref(Type *elem);
Type *type_vector(Type *elem);
Type *type_map(Type *key, Type *value);
Type *type_set(Type *elem);
Type *type_shared(Type *elem);
Type *type_weak(Type *elem);
Type *type_task(Type *elem);
void type_alias_define(const char *name, Type *type);
Type *type_alias_find(const char *name);
char *type_c_text(Type *t);
Type *type_cfunc(Type *ret, Type **params, int nparams);
char *type_c_decl(Type *t, const char *name);
char *cimport_from_source(const char *src);
const char *cimport_builtin(const char *inc);
Type *type_clone(const Type *t);
const char *type_name(const Type *t);
int type_is_int(const Type *t);
int type_is_float(const Type *t);
int type_is_number(const Type *t);
int type_is_cstring(const Type *t);
int type_is_string(const Type *t);
int type_is_bool(const Type *t);
int type_is_void(const Type *t);
int type_equal(const Type *a, const Type *b);

typedef struct Stmt Stmt;

typedef struct SwitchCase SwitchCase;

typedef enum {
    EX_INT, EX_FLOAT, EX_STRING, EX_FSTRING, EX_CHAR, EX_BOOL, EX_NULL,
    EX_UNIT, EX_IDENT, EX_BINARY, EX_UNARY, EX_CALL, EX_MEMBER, EX_INDEX,
    EX_CAST, EX_TERNARY, EX_COALESCE, EX_STRUCT, EX_ARRAY, EX_MAP, EX_RANGE,
    EX_STATIC, EX_SWITCH, EX_CLOSURE
} ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    int line;
    int col;
    Type *type;
    int shared_ref;
    union {
        long long ival;
        double fval;
        char *sval;
        struct { char **parts; Expr **exprs; int *prec; int nparts; } fstring;
        struct { int op; Expr *left; Expr *right; } binary;
        struct { int op; Expr *expr; } unary;
        struct { Expr *callee; Expr **args; int argc; } call;
        struct { Expr *obj; char *name; } member;
        struct { Expr *obj; Expr *index; } index;
        struct { Type *target; Expr *expr; } cast;
        struct { Expr *cond; Expr *then_expr; Expr *else_expr; } ternary;
        struct { char *type_name; char **field_names; Expr **field_vals; int nfields; } struct_init;
        struct { Expr **items; int count; } array;
        struct { Expr **keys; Expr **vals; int count; } map;
        struct { Expr *start; Expr *end; Expr *step; int inclusive; } range;
        struct { char *type_name; char *name; int is_variant; } path;
        struct { Expr *value; SwitchCase *cases; Expr **values; int ncases; } switch_expr;
        struct { char **params; int nparams; Expr *body; Stmt *block; } closure;
    } as;
};

typedef enum {
    ST_VAR, ST_ASSIGN, ST_EXPR, ST_RETURN, ST_IF, ST_WHILE, ST_FOR,
    ST_FORIN, ST_BLOCK, ST_BREAK, ST_CONTINUE, ST_DEFER, ST_SWITCH
} StmtKind;

struct SwitchCase {
    Expr *match;
    Stmt *body;
    int is_default;
    char *pattern;
    char *binding;
    char *enum_type;
    char *enum_variant;
    char **bindings;
    int nbindings;
};

struct Stmt {
    StmtKind kind;
    int line;
    int col;
    union {
        struct { Type *type; char *name; Expr *init; int is_auto; int is_c_macro; } var;
        struct { Expr *target; int op; Expr *value; } assign;
        Expr *expr;
        Expr *ret;
        struct { Expr *cond; Stmt *then_stmt; Stmt *else_stmt; } if_stmt;
        struct { Expr *cond; Stmt *body; } while_stmt;
        struct { Stmt *init; Expr *cond; Expr *step; Stmt *body; } for_stmt;
        struct { char *name; char *index_name; Expr *iter; Stmt *body; } forin;
        struct { Stmt **items; int count; } block;
        struct { Expr *value; SwitchCase *cases; int ncases; } switch_stmt;
        Stmt *defer_stmt;
    } as;
};

typedef struct Param {
    Type *type;
    char *name;
} Param;

typedef struct Field {
    Type *type;
    char *name;
    Expr *default_value;
} Field;

typedef struct Func {
    char *name;
    Param *params;
    int nparams;
    Type *ret;
    Stmt *body;
    int is_pub;
    int is_static;
    int is_test;
    int is_method;
    int is_async;
    int is_cfunc;
    int is_variadic;
    int is_declared;
    struct StructDecl *owner;
} Func;

typedef struct StructDecl {
    char *name;
    Field *fields;
    int nfields;
    Func **methods;
    int nmethods;
    int is_cstruct;
    int is_cunion;
    int from_binding;
    int from_header;
    int imported;
    int is_managed;
} StructDecl;

typedef struct EnumDecl {
    char *name;
    char **variants;
    int nvariants;
    Type ***variant_types;
    char ***variant_field_names;
    int *variant_nfields;
    int has_payload;
} EnumDecl;

typedef struct Program {
    Func **funcs;
    int nfuncs;
    StructDecl **structs;
    int nstructs;
    EnumDecl **enums;
    int nenums;
    Stmt **globals;
    int nglobals;
    char **c_includes;
    int nc_includes;
    char **c_imports;
    int nc_imports;
    char **c_sources;
    int nc_sources;
    char **c_links;
    int nc_links;
    char *file;
} Program;

typedef struct {
    Token *tokens;
    int count;
    int pos;
    Program *program;
    const char *file;
    int had_error;
} Parser;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...);
void fatal_at(const char *file, int line, int col, const char *fmt, ...);
void fatal(const char *fmt, ...);

Lexer lexer_new(const char *src, const char *file);
Token lexer_next(Lexer *lx);
Token *lex_all(const char *src, const char *file, int *out_count);

Program *parse_program(const char *src, const char *file);
Stmt *parse_block(Parser *p);
Expr *parse_expression(Parser *p);

int check_program(Program *prog);
int generate_c(Program *prog, const char *output_path, const char *source_name);

#endif

