#include "aktc.h"

#include <ctype.h>
#include <stdarg.h>

typedef struct {
    const char *name;
    TokenKind kind;
} Keyword;

static const Keyword keywords[] = {
    {"fn", TK_FN}, {"const", TK_CONST}, {"static", TK_STATIC},
    {"type", TK_TYPE}, {"struct", TK_STRUCT}, {"enum", TK_ENUM},
    {"managed", TK_MANAGED},
    {"impl", TK_IMPL}, {"pub", TK_PUB}, {"use", TK_USE},
    {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE},
    {"for", TK_FOR}, {"in", TK_IN}, {"loop", TK_LOOP},
    {"switch", TK_SWITCH}, {"case", TK_CASE}, {"default", TK_DEFAULT},
    {"break", TK_BREAK}, {"continue", TK_CONTINUE}, {"return", TK_RETURN},
    {"defer", TK_DEFER}, {"arena", TK_ARENA}, {"shared", TK_SHARED},
    {"weak", TK_WEAK}, {"manual", TK_MANUAL}, {"auto", TK_AUTO},
    {"cinclude", TK_CINCLUDE}, {"cimport", TK_CIMPORT}, {"clink", TK_CLINK}, {"cfn", TK_CFN},
    {"cstruct", TK_CSTRUCT}, {"cunion", TK_CUNION},
    {"async", TK_ASYNC}, {"await", TK_AWAIT}, {"inline", TK_INLINE},
    {"test", TK_TEST}, {"true", TK_TRUE}, {"false", TK_FALSE},
    {"null", TK_NULL}, {"self", TK_SELF}, {"as", TK_AS},
    {"and", TK_AND}, {"or", TK_OR}, {"not", TK_NOT},
    {"step", TK_STEP}, {"move", TK_MOVE}, {"volatile", TK_VOLATILE},
    {NULL, TK_EOF}
};

static void advance(Lexer *lx) {
    if (lx->src[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static char peek(Lexer *lx) { return lx->src[lx->pos]; }
static char peek2(Lexer *lx) { return lx->src[lx->pos] ? lx->src[lx->pos + 1] : 0; }

Lexer lexer_new(const char *src, const char *file) {
    Lexer lx;
    lx.src = src;
    lx.file = file;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    if ((unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF)
        lx.pos = 3;
    return lx;
}

static Token make_token(TokenKind kind, char *text, int line, int col) {
    Token t;
    t.kind = kind;
    t.text = text;
    t.ival = 0;
    t.fval = 0;
    t.line = line;
    t.col = col;
    return t;
}

static void skip_space_and_comments(Lexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '/') {
            while (peek(lx) && peek(lx) != '\n') advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '*') {
            int start_line = lx->line, start_col = lx->col;
            advance(lx);
            advance(lx);
            while (peek(lx) && !(peek(lx) == '*' && peek2(lx) == '/')) advance(lx);
            if (!peek(lx)) fatal_at(lx->file, start_line, start_col, "unterminated block comment");
            advance(lx);
            advance(lx);
            continue;
        }
        break;
    }
}

static char *read_ident(Lexer *lx) {
    size_t start = lx->pos;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_' || (unsigned char)peek(lx) >= 0x80)
        advance(lx);
    return xstrndup(lx->src + start, lx->pos - start);
}

static void buf_push(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 1 >= *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *buf = (char *)xrealloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *read_quoted(Lexer *lx, char quote, int *out_ok) {
    size_t cap = 64, len = 0;
    char *buf = (char *)xmalloc(cap);
    advance(lx); /* opening quote */
    while (peek(lx) && peek(lx) != quote) {
        char c = peek(lx);
        if (c == '\n') {
            free(buf);
            *out_ok = 0;
            return NULL;
        }
        if (c == '\\') {
            advance(lx);
            char e = peek(lx);
            switch (e) {
                case 'n': buf_push(&buf, &len, &cap, '\n'); advance(lx); break;
                case 't': buf_push(&buf, &len, &cap, '\t'); advance(lx); break;
                case 'r': buf_push(&buf, &len, &cap, '\r'); advance(lx); break;
                case '0': buf_push(&buf, &len, &cap, '\0'); advance(lx); break;
                case '\\': buf_push(&buf, &len, &cap, '\\'); advance(lx); break;
                case '\'': buf_push(&buf, &len, &cap, '\''); advance(lx); break;
                case '"': buf_push(&buf, &len, &cap, '"'); advance(lx); break;
                case 'x': {
                    advance(lx);
                    int a = hexval(peek(lx));
                    if (a >= 0) advance(lx);
                    int b = hexval(peek(lx));
                    if (b >= 0) advance(lx);
                    buf_push(&buf, &len, &cap, (char)((a < 0 ? 0 : a) * 16 + (b < 0 ? 0 : b)));
                    break;
                }
                default:
                    buf_push(&buf, &len, &cap, e);
                    if (e) advance(lx);
                    break;
            }
            continue;
        }
        buf_push(&buf, &len, &cap, c);
        advance(lx);
    }
    if (peek(lx) != quote) {
        free(buf);
        *out_ok = 0;
        return NULL;
    }
    advance(lx);
    buf_push(&buf, &len, &cap, 0);
    *out_ok = 1;
    return buf;
}

static char *read_fstring(Lexer *lx, int *out_ok) {
    size_t cap = 128, len = 0;
    char *buf = (char *)xmalloc(cap);
    advance(lx); /* opening quote */
    int depth = 0;
    while (peek(lx)) {
        char c = peek(lx);
        if (c == '\n') {
            free(buf);
            *out_ok = 0;
            return NULL;
        }
        if (depth == 0 && c == '"') {
            advance(lx);
            buf_push(&buf, &len, &cap, 0);
            *out_ok = 1;
            return buf;
        }
        if (c == '\\') {
            buf_push(&buf, &len, &cap, c);
            advance(lx);
            if (peek(lx)) {
                buf_push(&buf, &len, &cap, peek(lx));
                advance(lx);
            }
            continue;
        }
        if (depth == 0 && c == '{') {
            if (peek2(lx) == '{') {
                buf_push(&buf, &len, &cap, '{');
                advance(lx);
                advance(lx);
                continue;
            }
            depth = 1;
            buf_push(&buf, &len, &cap, c);
            advance(lx);
            continue;
        }
        if (depth == 0 && c == '}') {
            if (peek2(lx) == '}') {
                buf_push(&buf, &len, &cap, '}');
                advance(lx);
                advance(lx);
                continue;
            }
            buf_push(&buf, &len, &cap, c);
            advance(lx);
            continue;
        }
        if (depth > 0) {
            if (c == '"' || c == '\'') {
                char q = c;
                buf_push(&buf, &len, &cap, c);
                advance(lx);
                while (peek(lx) && peek(lx) != q) {
                    if (peek(lx) == '\\') {
                        buf_push(&buf, &len, &cap, peek(lx));
                        advance(lx);
                        if (peek(lx)) {
                            buf_push(&buf, &len, &cap, peek(lx));
                            advance(lx);
                        }
                    } else {
                        buf_push(&buf, &len, &cap, peek(lx));
                        advance(lx);
                    }
                }
                if (peek(lx) == q) {
                    buf_push(&buf, &len, &cap, q);
                    advance(lx);
                }
                continue;
            }
            if (c == '{') depth++;
            else if (c == '}') depth--;
            buf_push(&buf, &len, &cap, c);
            advance(lx);
            continue;
        }
        buf_push(&buf, &len, &cap, c);
        advance(lx);
    }
    free(buf);
    *out_ok = 0;
    return NULL;
}

static char *read_number(Lexer *lx, TokenKind *kind, long long *ival, double *fval) {
    size_t start = lx->pos;
    *kind = TK_INT;
    if (peek(lx) == '0' && (peek2(lx) == 'x' || peek2(lx) == 'X')) {
        advance(lx);
        advance(lx);
        while (isxdigit((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
        char *s = xstrndup(lx->src + start, lx->pos - start);
        *ival = (long long)strtoull(s + 2, NULL, 16);
        free(s);
        return xstrndup(lx->src + start, lx->pos - start);
    }
    if (peek(lx) == '0' && (peek2(lx) == 'b' || peek2(lx) == 'B')) {
        advance(lx);
        advance(lx);
        while (peek(lx) == '0' || peek(lx) == '1' || peek(lx) == '_') advance(lx);
        char *s = xstrndup(lx->src + start, lx->pos - start);
        *ival = (long long)strtoull(s + 2, NULL, 2);
        free(s);
        return xstrndup(lx->src + start, lx->pos - start);
    }
    while (isdigit((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    if (peek(lx) == '.' && peek2(lx) != '.') {
        *kind = TK_FLOAT;
        advance(lx);
        while (isdigit((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    }
    if (peek(lx) == 'e' || peek(lx) == 'E') {
        *kind = TK_FLOAT;
        advance(lx);
        if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
        while (isdigit((unsigned char)peek(lx))) advance(lx);
    }
    if ((peek(lx) == 'f' || peek(lx) == 'F') &&
        !(isalnum((unsigned char)peek2(lx)) || peek2(lx) == '_')) {
        *kind = TK_FLOAT;
        advance(lx);
    }
    char *s = xstrndup(lx->src + start, lx->pos - start);
    if (*kind == TK_FLOAT) {
        *fval = strtod(s, NULL);
        *ival = 0;
    } else {
        *ival = (long long)strtoll(s, NULL, 10);
        *fval = 0;
    }
    return s;
}

Token lexer_next(Lexer *lx) {
    skip_space_and_comments(lx);
    int line = lx->line, col = lx->col;
    char c = peek(lx);
    if (!c) return make_token(TK_EOF, xstrdup(""), line, col);

    if (isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80) {
        char *id = read_ident(lx);
        if ((id[0] == 'f') && id[1] == 0 && peek(lx) == '"') {
            int ok = 0;
            char *raw = read_fstring(lx, &ok);
            if (!ok) fatal_at(lx->file, line, col, "unterminated f-string");
            Token t = make_token(TK_FSTRING, raw, line, col);
            free(id);
            return t;
        }
        if ((id[0] == 'r') && id[1] == 0 && peek(lx) == '"') {
            /* raw string: no escape processing */
            advance(lx);
            size_t start = lx->pos;
            while (peek(lx) && peek(lx) != '"') advance(lx);
            if (!peek(lx)) fatal_at(lx->file, line, col, "unterminated raw string");
            char *s = xstrndup(lx->src + start, lx->pos - start);
            advance(lx);
            free(id);
            return make_token(TK_STRING, s, line, col);
        }
        for (int i = 0; keywords[i].name; i++) {
            if (strcmp(id, keywords[i].name) == 0) {
                Token t = make_token(keywords[i].kind, id, line, col);
                return t;
            }
        }
        return make_token(TK_IDENT, id, line, col);
    }

    if (isdigit((unsigned char)c)) {
        TokenKind kind;
        long long iv = 0;
        double fv = 0;
        char *text = read_number(lx, &kind, &iv, &fv);
        Token t = make_token(kind, text, line, col);
        t.ival = iv;
        t.fval = fv;
        return t;
    }

    if (c == '"') {
        int ok = 0;
        char *s = read_quoted(lx, '"', &ok);
        if (!ok) fatal_at(lx->file, line, col, "unterminated string");
        return make_token(TK_STRING, s, line, col);
    }
    if (c == '\'') {
        int ok = 0;
        char *s = read_quoted(lx, '\'', &ok);
        if (!ok) fatal_at(lx->file, line, col, "unterminated char literal");
        Token t = make_token(TK_CHAR, s, line, col);
        unsigned char first = (unsigned char)s[0];
        t.ival = first;
        return t;
    }

#define TWO(a, b, k) if (c == a && peek2(lx) == b) { advance(lx); advance(lx); return make_token(k, xstrdup(#a #b), line, col); }
#define THREE(a, b, d, k) if (c == a && peek2(lx) == b && lx->src[lx->pos + 2] == d) { advance(lx); advance(lx); advance(lx); return make_token(k, xstrdup(#a #b #d), line, col); }

    THREE('.', '.', '=', TK_RANGE_INCL);
    THREE('.', '.', '.', TK_ELLIPSIS);
    TWO('.', '.', TK_RANGE);
    TWO('*', '*', TK_POW);
    TWO(':', ':', TK_COLONCOLON);
    TWO('-', '>', TK_ARROW);
    TWO('=', '>', TK_FATARROW);
    TWO('=', '=', TK_EQEQ);
    TWO('!', '=', TK_NEQ);
    TWO('<', '=', TK_LE);
    TWO('>', '=', TK_GE);
    TWO('<', '<', TK_SHL);
    TWO('>', '>', TK_SHR);
    TWO('&', '&', TK_ANDAND);
    TWO('|', '|', TK_OROR);
    TWO('+', '=', TK_PLUSEQ);
    TWO('-', '=', TK_MINUSEQ);
    TWO('*', '=', TK_STAREQ);
    TWO('/', '=', TK_SLASHEQ);
    TWO('%', '=', TK_PERCENTEQ);
    TWO('?', '?', TK_QQ);

    advance(lx);
    switch (c) {
        case '+': return make_token(TK_PLUS, xstrdup("+"), line, col);
        case '-': return make_token(TK_MINUS, xstrdup("-"), line, col);
        case '*': return make_token(TK_STAR, xstrdup("*"), line, col);
        case '/': return make_token(TK_SLASH, xstrdup("/"), line, col);
        case '%': return make_token(TK_PERCENT, xstrdup("%"), line, col);
        case '&': return make_token(TK_AMP, xstrdup("&"), line, col);
        case '|': return make_token(TK_PIPE, xstrdup("|"), line, col);
        case '^': return make_token(TK_CARET, xstrdup("^"), line, col);
        case '~': return make_token(TK_TILDE, xstrdup("~"), line, col);
        case '!': return make_token(TK_BANG, xstrdup("!"), line, col);
        case '=': return make_token(TK_ASSIGN, xstrdup("="), line, col);
        case '<': return make_token(TK_LT, xstrdup("<"), line, col);
        case '>': return make_token(TK_GT, xstrdup(">"), line, col);
        case '?': return make_token(TK_QUESTION, xstrdup("?"), line, col);
        case ':': return make_token(TK_COLON, xstrdup(":"), line, col);
        case '.':
            if (peek2(lx) == '.' && lx->src[lx->pos + 2] == '.') {
                advance(lx);
                advance(lx);
                advance(lx);
                return make_token(TK_ELLIPSIS, xstrdup("..."), line, col);
            }
            return make_token(TK_DOT, xstrdup("."), line, col);
        case ',': return make_token(TK_COMMA, xstrdup(","), line, col);
        case ';': return make_token(TK_SEMI, xstrdup(";"), line, col);
        case '(': return make_token(TK_LPAREN, xstrdup("("), line, col);
        case ')': return make_token(TK_RPAREN, xstrdup(")"), line, col);
        case '{': return make_token(TK_LBRACE, xstrdup("{"), line, col);
        case '}': return make_token(TK_RBRACE, xstrdup("}"), line, col);
        case '[': return make_token(TK_LBRACKET, xstrdup("["), line, col);
        case ']': return make_token(TK_RBRACKET, xstrdup("]"), line, col);
        case '@': return make_token(TK_AT, xstrdup("@"), line, col);
        default:
            fatal_at(lx->file, line, col, "unexpected character '%c' (0x%02x)", c, (unsigned char)c);
    }
    return make_token(TK_EOF, xstrdup(""), line, col);
#undef TWO
#undef THREE
}

Token *lex_all(const char *src, const char *file, int *out_count) {
    Lexer lx = lexer_new(src, file);
    int cap = 256, n = 0;
    Token *tokens = (Token *)xmalloc((size_t)cap * sizeof(Token));
    for (;;) {
        Token t = lexer_next(&lx);
        if (n + 1 >= cap) {
            cap *= 2;
            tokens = (Token *)xrealloc(tokens, (size_t)cap * sizeof(Token));
        }
        tokens[n++] = t;
        if (t.kind == TK_EOF) break;
    }
    *out_count = n;
    return tokens;
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TK_EOF: return "end of file";
        case TK_IDENT: return "identifier";
        case TK_INT: return "integer";
        case TK_FLOAT: return "float";
        case TK_STRING: return "string";
        case TK_FSTRING: return "f-string";
        case TK_CHAR: return "char";
        case TK_FN: return "fn";
        case TK_STRUCT: return "struct";
        case TK_ENUM: return "enum";
        case TK_LBRACE: return "{";
        case TK_RBRACE: return "}";
        case TK_LPAREN: return "(";
        case TK_RPAREN: return ")";
        case TK_LBRACKET: return "[";
        case TK_RBRACKET: return "]";
        case TK_COMMA: return ",";
        case TK_SEMI: return ";";
        case TK_ELLIPSIS: return "...";
        case TK_ASSIGN: return "=";
        case TK_RETURN: return "return";
        default: return "token";
    }
}
