/* bytefound.c - Bytefound v0.1
   A C compiler written from scratch, targeting 16-bit x86 real mode.
   Supports:  int <name>() { return <expression>; }
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

static const char *KEYWORDS[] = { "int", "return", NULL };

static int is_keyword(const char *s, int len) {
    for (int i = 0; KEYWORDS[i]; i++)
        if ((int)strlen(KEYWORDS[i]) == len && !strncmp(KEYWORDS[i], s, len))
            return 1;
    return 0;
}

/* Two-character punctuators, ready for ==, <=, etc. */
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

/* =============== expression code generation =============== */
/* Every rule leaves its result in AX.
     expr    := term  (('+' | '-') term)*
     term    := unary (('*' | '/') unary)*
     unary   := ('+' | '-')? primary
     primary := NUMBER | '(' expr ')'                                     */

static void gen_expr(void);

static void gen_primary(void) {
    if (consume("(")) {
        gen_expr();
        expect(")");
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

        fprintf(out, "    push ax\n");    /* save left operand    */
        gen_unary();                       /* right operand in ax  */
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

static void gen_expr(void) {
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

/* =============== statements and functions =============== */

static void gen_epilogue(void) {
    fprintf(out, "    mov sp, bp\n");
    fprintf(out, "    pop bp\n");
    fprintf(out, "    ret\n");
}

/* stmt := 'return' expr ';' */
static void gen_stmt(void) {
    if (consume("return")) {
        gen_expr();
        expect(";");
        gen_epilogue();
        return;
    }
    error_at(tk->loc, "statement not supported yet");
}

/* function := 'int' IDENT '(' ')' '{' stmt* '}' */
static void gen_function(void) {
    expect("int");

    if (tk->kind != TK_IDENT)
        error_at(tk->loc, "expected a function name");

    char name[256];
    int n = tk->len < 255 ? tk->len : 255;
    memcpy(name, tk->loc, (size_t)n);
    name[n] = '\0';
    tk = tk->next;

    expect("(");
    expect(")");
    expect("{");

    fprintf(out, "%s:\n", name);
    fprintf(out, "    push bp\n");
    fprintf(out, "    mov bp, sp\n");

    while (!equal(tk, "}")) {
        if (tk->kind == TK_EOF) error_at(tk->loc, "missing '}'");
        gen_stmt();
    }
    expect("}");

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

    fprintf(out, "; generated by Bytefound v0.1 - 16-bit real mode\n");
    fprintf(out, "bits 16\n\n");

    while (tk->kind != TK_EOF)
        gen_function();

    fclose(out);
    free(text);

    printf("[ok] %s -> %s\n", path, outpath);
    printf("     assemble with: nasm -f bin %s -o output.bin\n", outpath);
    printf("     or %%include it from your kernel source\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Bytefound v0.1 - 16-bit real mode C compiler\n");
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
