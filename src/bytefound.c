/* bytefound.c - Byte-found v0.4
   A C compiler written from scratch, targeting 16-bit x86 real mode.
   Supports:  functions with parameters, function calls, return,
              local int variables, assignment, integer arithmetic,
              comparisons, if/else and while.

   Calling convention: arguments are pushed left to right, the caller
   cleans up the stack, and the result comes back in AX.

   Build:     gcc -std=c99 -Wall -Wextra -O2 src/bytefound.c -o bytefound.exe */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =============== tokenizer =============== */

typedef enum {
    TK_NUM,       /* numeric literal */
    TK_IDENT,     /* identifier      */
    TK_KEYWORD,   /* reserved word   */
    TK_PUNCT,     /* punctuator      */
    TK_EOF        /* end of file     */
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind   kind;
    Token      *next;
    long        val;   /* set when kind == TK_NUM   */
    const char *loc;   /* position in source buffer */
    int         len;   /* lexeme length             */
};

static const char *src_start;   /* start of source, used for error reporting */
static const char *src_path;    /* current file name                         */

/* Report an error pointing at the exact line and column. */
static void error_at(const char *loc, const char *msg) {
    int line = 1;
    const char *line_start = src_start;
    for (const char *p = src_start; p < loc; p++)
        if (*p == '\n') { line++; line_start = p + 1; }

    int col = (int)(loc - line_start) + 1;
    fprintf(stderr, "%s:%d:%d: error: %s\n", src_path, line, col, msg);

    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    fprintf(stderr, "  %.*s\n", (int)(line_end - line_start), line_start);
    fprintf(stderr, "  %*s^\n", col - 1, "");
    exit(1);
}

static Token *new_token(TokenKind kind, const char *loc, int len) {
    Token *t = calloc(1, sizeof(Token));
    if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
    t->kind = kind;
    t->loc  = loc;
    t->len  = len;
    return t;
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_char(int c)  { return isalnum(c) || c == '_'; }

static const char *KEYWORDS[] = {
    "int", "return", "if", "else", "while", NULL
};

static int is_keyword(const char *s, int len) {
    for (int i = 0; KEYWORDS[i]; i++)
        if ((int)strlen(KEYWORDS[i]) == len && !strncmp(KEYWORDS[i], s, len))
            return 1;
    return 0;
}

/* Two-character punctuators are matched before single ones, so that
   '==' never gets split into two '=' tokens. */
static int read_punct(const char *p) {
    static const char *two[] = { "==", "!=", "<=", ">=", NULL };
    for (int i = 0; two[i]; i++)
        if (!strncmp(p, two[i], 2)) return 2;
    return strchr("+-*/()[]{};,=<>!&", *p) ? 1 : 0;
}

static Token *tokenize(const char *p) {
    Token head = {0};
    Token *cur = &head;

    while (*p) {
        if (isspace((unsigned char)*p)) { p++; continue; }

        /* line comment */
        if (!strncmp(p, "//", 2)) {
            while (*p && *p != '\n') p++;
            continue;
        }

        /* block comment */
        if (!strncmp(p, "/*", 2)) {
            const char *start = p;
            p += 2;
            while (*p && strncmp(p, "*/", 2)) p++;
            if (!*p) error_at(start, "unterminated block comment");
            p += 2;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)*p)) {
            const char *start = p;
            long v = 0;
            while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; }
            cur = cur->next = new_token(TK_NUM, start, (int)(p - start));
            cur->val = v;
            continue;
        }

        /* identifier or keyword */
        if (is_ident_start((unsigned char)*p)) {
            const char *start = p;
            while (is_ident_char((unsigned char)*p)) p++;
            int len = (int)(p - start);
            cur = cur->next = new_token(
                is_keyword(start, len) ? TK_KEYWORD : TK_IDENT, start, len);
            continue;
        }

        /* punctuator */
        int plen = read_punct(p);
        if (plen) {
            cur = cur->next = new_token(TK_PUNCT, p, plen);
            p += plen;
            continue;
        }

        error_at(p, "invalid character");
    }

    cur->next = new_token(TK_EOF, p, 0);
    return head.next;
}

/* =============== parser helpers =============== */

static Token *tk;      /* current token    */
static FILE  *out;     /* .asm output file */

static int equal(Token *t, const char *s) {
    return (int)strlen(s) == t->len && !strncmp(t->loc, s, t->len);
}

/* Consume the token if it matches; return 1 when consumed. */
static int consume(const char *s) {
    if (equal(tk, s)) { tk = tk->next; return 1; }
    return 0;
}

/* Require the given token or abort with an error. */
static void expect(const char *s) {
    if (!equal(tk, s)) {
        char msg[128];
        snprintf(msg, sizeof msg, "expected '%s'", s);
        error_at(tk->loc, msg);
    }
    tk = tk->next;
}

static long expect_number(void) {
    if (tk->kind != TK_NUM) error_at(tk->loc, "expected a number");
    long v = tk->val;
    tk = tk->next;
    return v;
}

/* Copy a token's text into a fixed buffer. */
static void token_text(Token *t, char *buf, size_t size) {
    size_t n = (size_t)t->len < size - 1 ? (size_t)t->len : size - 1;
    memcpy(buf, t->loc, n);
    buf[n] = '\0';
}

/* =============== labels ===============
   NASM scopes labels starting with '.' to the preceding global label,
   so per-function numbering never collides across functions. */

static int label_id;

static int new_label(void) { return label_id++; }

/* =============== variables ===============
   Parameters live above BP, locals below it. With arguments pushed
   left to right, the frame of f(a, b, c) looks like:

       [bp+8]  a          <- pushed first
       [bp+6]  b
       [bp+4]  c          <- pushed last
       [bp+2]  return address
       [bp+0]  saved bp
       [bp-2]  first local
       [bp-4]  second local                                              */

#define MAX_VARS   64
#define MAX_PARAMS 16

typedef struct {
    char name[64];
    int  offset;      /* signed offset from BP */
} Var;

static Var vars[MAX_VARS];
static int nvars;         /* entries in the table, params included */
static int nlocal_slots;  /* locals only, used to compute offsets   */

static Var *find_var(Token *t) {
    for (int i = 0; i < nvars; i++)
        if ((int)strlen(vars[i].name) == t->len &&
            !strncmp(vars[i].name, t->loc, (size_t)t->len))
            return &vars[i];
    return NULL;
}

static Var *add_var(Token *t, int offset) {
    if (find_var(t)) error_at(t->loc, "name already declared");
    if (nvars >= MAX_VARS) error_at(t->loc, "too many variables");
    if (t->len >= (int)sizeof(vars[0].name))
        error_at(t->loc, "name too long");

    Var *v = &vars[nvars];
    token_text(t, v->name, sizeof v->name);
    v->offset = offset;
    nvars++;
    return v;
}

/* Count declarations ahead of time so the prologue knows how much
   stack space to reserve. Expects t to point at the opening '{'. */
static int count_locals(Token *t) {
    int depth = 0, n = 0;
    for (; t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{")) { depth++; continue; }
        if (equal(t, "}")) { if (--depth == 0) break; continue; }
        if (equal(t, "int") && t->next->kind == TK_IDENT) n++;
    }
    return n;
}

/* =============== expression code generation =============== */
/* Every rule leaves its result in AX.
     expr       := assign
     assign     := IDENT '=' assign | equality
     equality   := relational (('==' | '!=') relational)*
     relational := add (('<' | '<=' | '>' | '>=') add)*
     add        := term  (('+' | '-') term)*
     term       := unary (('*' | '/') unary)*
     unary      := ('+' | '-')? primary
     primary    := NUMBER | IDENT | IDENT '(' args ')' | '(' expr ')'     */

static void gen_expr(void);

static void gen_funcall(void) {
    char fname[256];
    token_text(tk, fname, sizeof fname);
    tk = tk->next->next;              /* skip the name and '(' */

    int nargs = 0;
    if (!equal(tk, ")")) {
        for (;;) {
            gen_expr();
            fprintf(out, "    push ax\n");   /* left to right */
            nargs++;
            if (!consume(",")) break;
        }
    }
    expect(")");

    fprintf(out, "    call %s\n", fname);
    if (nargs) fprintf(out, "    add sp, %d\n", nargs * 2);
}

static void gen_primary(void) {
    if (consume("(")) {
        gen_expr();
        expect(")");
        return;
    }

    if (tk->kind == TK_IDENT) {
        if (equal(tk->next, "(")) { gen_funcall(); return; }

        Var *v = find_var(tk);
        if (!v) error_at(tk->loc, "undeclared variable");
        fprintf(out, "    mov ax, [bp%+d]\n", v->offset);
        tk = tk->next;
        return;
    }

    const char *loc = tk->loc;
    long v = expect_number();
    if (v > 32767 || v < -32768)
        error_at(loc, "literal out of range for a 16-bit int");
    fprintf(out, "    mov ax, %ld\n", v);
}

static void gen_unary(void) {
    if (consume("+")) { gen_unary(); return; }
    if (consume("-")) { gen_unary(); fprintf(out, "    neg ax\n"); return; }
    gen_primary();
}

static void gen_term(void) {
    gen_unary();
    for (;;) {
        int mul = equal(tk, "*");
        int div = equal(tk, "/");
        if (!mul && !div) return;
        tk = tk->next;

        fprintf(out, "    push ax\n");    /* save left operand   */
        gen_unary();                       /* right operand in ax */
        fprintf(out, "    mov cx, ax\n");
        fprintf(out, "    pop ax\n");
        if (mul) {
            fprintf(out, "    imul ax, cx\n");
        } else {
            fprintf(out, "    cwd\n");     /* sign-extend ax into dx:ax */
            fprintf(out, "    idiv cx\n");
        }
    }
}

static void gen_add(void) {
    gen_term();
    for (;;) {
        int add = equal(tk, "+");
        int sub = equal(tk, "-");
        if (!add && !sub) return;
        tk = tk->next;

        fprintf(out, "    push ax\n");
        gen_term();
        fprintf(out, "    mov cx, ax\n");
        fprintf(out, "    pop ax\n");
        fprintf(out, "    %s ax, cx\n", add ? "add" : "sub");
    }
}

/* Comparisons leave 1 or 0 in AX, matching C semantics. */
static void gen_compare(const char *setcc, void (*next)(void)) {
    fprintf(out, "    push ax\n");
    next();
    fprintf(out, "    mov cx, ax\n");
    fprintf(out, "    pop ax\n");
    fprintf(out, "    cmp ax, cx\n");
    fprintf(out, "    %s al\n", setcc);
    fprintf(out, "    mov ah, 0\n");
}

static void gen_relational(void) {
    gen_add();
    for (;;) {
        const char *setcc;
        if      (equal(tk, "<"))  setcc = "setl";
        else if (equal(tk, "<=")) setcc = "setle";
        else if (equal(tk, ">"))  setcc = "setg";
        else if (equal(tk, ">=")) setcc = "setge";
        else return;
        tk = tk->next;
        gen_compare(setcc, gen_add);
    }
}

static void gen_equality(void) {
    gen_relational();
    for (;;) {
        const char *setcc;
        if      (equal(tk, "==")) setcc = "sete";
        else if (equal(tk, "!=")) setcc = "setne";
        else return;
        tk = tk->next;
        gen_compare(setcc, gen_relational);
    }
}

/* Assignment is right-associative, so a = b = 3 works. */
static void gen_assign(void) {
    if (tk->kind == TK_IDENT && equal(tk->next, "=")) {
        Var *v = find_var(tk);
        if (!v) error_at(tk->loc, "undeclared variable");
        tk = tk->next->next;              /* skip IDENT and '=' */
        gen_assign();
        fprintf(out, "    mov [bp%+d], ax\n", v->offset);
        return;
    }
    gen_equality();
}

static void gen_expr(void) {
    gen_assign();
}

/* =============== statements and functions =============== */

static void gen_epilogue(void) {
    fprintf(out, "    mov sp, bp\n");
    fprintf(out, "    pop bp\n");
    fprintf(out, "    ret\n");
}

static void gen_stmt(void);

/* Consumes statements until the matching '}'. */
static void gen_block(void) {
    while (!equal(tk, "}")) {
        if (tk->kind == TK_EOF) error_at(tk->loc, "missing '}'");
        gen_stmt();
    }
    expect("}");
}

/* stmt := '{' stmt* '}'
         | 'if' '(' expr ')' stmt ('else' stmt)?
         | 'while' '(' expr ')' stmt
         | 'return' expr ';'
         | 'int' IDENT ('=' expr)? ';'
         | expr ';'                                                       */
static void gen_stmt(void) {
    if (consume("{")) {
        gen_block();
        return;
    }

    if (consume("if")) {
        int l = new_label();
        expect("(");
        gen_expr();
        expect(")");
        fprintf(out, "    cmp ax, 0\n");
        fprintf(out, "    je .L%d_else\n", l);
        gen_stmt();
        fprintf(out, "    jmp .L%d_end\n", l);
        fprintf(out, ".L%d_else:\n", l);
        if (consume("else")) gen_stmt();
        fprintf(out, ".L%d_end:\n", l);
        return;
    }

    if (consume("while")) {
        int l = new_label();
        fprintf(out, ".L%d_begin:\n", l);
        expect("(");
        gen_expr();
        expect(")");
        fprintf(out, "    cmp ax, 0\n");
        fprintf(out, "    je .L%d_end\n", l);
        gen_stmt();
        fprintf(out, "    jmp .L%d_begin\n", l);
        fprintf(out, ".L%d_end:\n", l);
        return;
    }

    if (consume("return")) {
        gen_expr();
        expect(";");
        gen_epilogue();
        return;
    }

    if (consume("int")) {
        if (tk->kind != TK_IDENT)
            error_at(tk->loc, "expected a variable name");
        Var *v = add_var(tk, -2 * (nlocal_slots + 1));
        nlocal_slots++;
        tk = tk->next;

        if (consume("=")) {
            gen_expr();
            fprintf(out, "    mov [bp%+d], ax\n", v->offset);
        }
        expect(";");
        return;
    }

    gen_expr();
    expect(";");
}

/* function := 'int' IDENT '(' params? ')' '{' stmt* '}'
   params   := 'int' IDENT (',' 'int' IDENT)*                            */
static void gen_function(void) {
    expect("int");

    if (tk->kind != TK_IDENT)
        error_at(tk->loc, "expected a function name");

    char name[256];
    token_text(tk, name, sizeof name);
    tk = tk->next;

    expect("(");

    Token *ptok[MAX_PARAMS];
    int nparams = 0;

    if (!equal(tk, ")")) {
        for (;;) {
            expect("int");
            if (tk->kind != TK_IDENT)
                error_at(tk->loc, "expected a parameter name");
            if (nparams >= MAX_PARAMS)
                error_at(tk->loc, "too many parameters");
            ptok[nparams++] = tk;
            tk = tk->next;
            if (!consume(",")) break;
        }
    }
    expect(")");

    /* Reset per-function state. */
    nvars        = 0;
    nlocal_slots = 0;
    label_id     = 0;

    /* Leftmost argument sits deepest, because args are pushed left to right. */
    for (int i = 0; i < nparams; i++)
        add_var(ptok[i], 4 + 2 * (nparams - 1 - i));

    int frame = count_locals(tk) * 2;

    expect("{");

    fprintf(out, "%s:\n", name);
    fprintf(out, "    push bp\n");
    fprintf(out, "    mov bp, sp\n");
    if (frame) fprintf(out, "    sub sp, %d\n", frame);

    gen_block();

    /* Fallback in case the function did not end with a return. */
    fprintf(out, "    mov ax, 0\n");
    gen_epilogue();
    fprintf(out, "\n");
}

/* =============== driver =============== */

static const char *get_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[error] cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static void compile_file(const char *path) {
    if (strcmp(get_ext(path), "c") != 0) {
        printf("[skip] not a .c file: %s\n", path);
        return;
    }

    char *text = read_file(path);
    if (!text) return;

    src_start = text;
    src_path  = path;

    char outpath[1024];
    const char *dot = strrchr(path, '.');
    snprintf(outpath, sizeof outpath, "%.*s.asm", (int)(dot - path), path);

    out = fopen(outpath, "w");
    if (!out) {
        fprintf(stderr, "[error] cannot write %s\n", outpath);
        free(text);
        return;
    }

    tk = tokenize(text);

    fprintf(out, "; generated by Byte-found v0.4 - 16-bit real mode\n");
    fprintf(out, "; calling convention: args pushed left to right,\n");
    fprintf(out, "; caller cleans the stack, result returned in AX\n");
    fprintf(out, "bits 16\n\n");

    while (tk->kind != TK_EOF)
        gen_function();

    fclose(out);
    free(text);

    printf("[ok] %s -> %s\n", path, outpath);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Byte-found v0.4 - 16-bit real mode C compiler\n");
        printf("Usage: bytefound file.c ...\n");
        printf("Or drop .c files onto the executable.\n\n");
        printf("Press Enter to exit...");
        getchar();
        return 0;
    }

    for (int i = 1; i < argc; i++)
        compile_file(argv[i]);

    printf("\nDone.\n");
    return 0;
}
