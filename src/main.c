/*
 * main.c
 * CLI driver: loads an HTML file, tokenizes it, writes results.
 *
 * Usage:  tokenize.exe <file.html>
 * Output: <file.html>.tokens.txt
 *
 * Compile (GCC/Clang):
 *   gcc -O2 -msse2 -o tokenize main.c tokenizer.c
 *
 * MSVC:
 *   cl /O2 /arch:SSE2 main.c tokenizer.c
 */

#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* Timing                                                               */
/* ------------------------------------------------------------------ */
static double now_sec(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

/* ------------------------------------------------------------------ */
/* Output                                                               */
/* ------------------------------------------------------------------ */
#define MAX_VAL_LEN 120

static void write_tokens(const TokenArray *ta, const char *outpath,
                         double t_load, double t_scan_tok,
                         double t_write_start, size_t file_bytes)
{
    FILE *f = fopen(outpath, "wb");
    if (!f) { perror("fopen output"); exit(1); }

    fprintf(f, "# HTML Tokenizer Output\n");
    fprintf(f, "# File size : %zu bytes\n", file_bytes);
    fprintf(f, "# Tokens    : %zu\n", ta->count);
    fprintf(f, "# Load time : %.3f ms\n", t_load * 1000.0);
    fprintf(f, "# Scan+tok  : %.3f ms\n", t_scan_tok * 1000.0);
    fprintf(f, "#\n");
    fprintf(f, "# FORMAT: TYPE | value (truncated to %d chars)\n", MAX_VAL_LEN);
    fprintf(f, "#\n");

    for (size_t i = 0; i < ta->count; i++) {
        const Token *t = &ta->data[i];

        /* synthetic close has no source text — print tag id instead */
        if (t->type == TOK_SYNTHETIC_CLOSE) {
            fprintf(f, "%-16s | (implicit close, tag id=%zu)\n",
                    TOKEN_NAMES[t->type], t->len);
            continue;
        }

        size_t vlen = t->len < MAX_VAL_LEN ? t->len : MAX_VAL_LEN;

        fprintf(f, "%-16s | ", TOKEN_NAMES[t->type]);

        for (size_t j = 0; j < vlen; j++) {
            unsigned char ch = (unsigned char)t->start[j];
            if (ch == '\n' || ch == '\r' || ch == '\t') fputc(' ', f);
            else if (ch < 32 || ch == 127)              fputc('?', f);
            else                                         fputc(ch, f);
        }
        if (t->len > MAX_VAL_LEN)
            fprintf(f, "...(+%zu)", t->len - MAX_VAL_LEN);
        fputc('\n', f);
    }

    double t_write = now_sec() - t_write_start;

    fprintf(f, "#\n");
    fprintf(f, "# Write time: %.3f ms\n", t_write * 1000.0);
    fprintf(f, "# TOTAL time: %.3f ms\n", (t_load + t_scan_tok + t_write) * 1000.0);
    fclose(f);

    printf("Tokens written : %zu\n",    ta->count);
    printf("Load time      : %.3f ms\n", t_load * 1000.0);
    printf("Scan+tok time  : %.3f ms\n", t_scan_tok * 1000.0);
    printf("Write time     : %.3f ms\n", t_write * 1000.0);
    printf("TOTAL          : %.3f ms\n", (t_load + t_scan_tok + t_write) * 1000.0);
    printf("Output         : %s\n",      outpath);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: tokenize <file.html>\n");
        return 1;
    }

    /* 1. Load */
    double t0 = now_sec();

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fprintf(stderr, "OOM\n"); return 1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        fprintf(stderr, "Read error\n"); return 1;
    }
    buf[fsize] = '\0';
    fclose(f);

    double t_load = now_sec() - t0;

    /* 2+3. Scan + Tokenize */
    double t1 = now_sec();
    TokenArray ta = html_tokenize(buf, (size_t)fsize);
    double t_scan_tok = now_sec() - t1;

    /* 4. Write */
    size_t inlen   = strlen(argv[1]);
    char  *outpath = (char *)malloc(inlen + 16);
    if (!outpath) { fprintf(stderr, "OOM\n"); return 1; }
    memcpy(outpath, argv[1], inlen);
    memcpy(outpath + inlen, ".tokens.txt", 12);

    double t_write_start = now_sec();
    write_tokens(&ta, outpath, t_load, t_scan_tok,
                 t_write_start, (size_t)fsize);

    free(ta.data);
    free(buf);
    free(outpath);
    return 0;
}