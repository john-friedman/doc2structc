/*
 * main.c
 * CLI driver: loads an HTML file, tokenizes it, converts to text nodes,
 * writes results.
 *
 * Usage:  tokenize.exe <file.html> [--config <config.json>]
 * Output: <file.html>.nodes.txt
 *
 * Compile (GCC/Clang):
 *   gcc -O2 -msse2 -o tokenize main.c tokenizer.c convert_tokens.c -lcjson
 *
 * MSVC:
 *   cl /O2 /arch:SSE2 main.c tokenizer.c convert_tokens.c cjson.lib
 */

#include "tokenizer.h"
#include "convert_tokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cjson/cJSON.h>
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
/* Config loading                                                       */
/* ------------------------------------------------------------------ */
static int load_config(const char *path, FeatureRegistry *reg)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen config"); return 0; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fprintf(stderr, "OOM\n"); fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "Config read error\n"); free(buf); fclose(f); return 0;
    }
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        fprintf(stderr, "Config JSON parse error: %s\n", cJSON_GetErrorPtr());
        return 0;
    }

    /* navigate: root -> "features" -> "html_tags" */
    cJSON *features  = cJSON_GetObjectItemCaseSensitive(root, "features");
    cJSON *html_tags = features
                     ? cJSON_GetObjectItemCaseSensitive(features, "html_tags")
                     : NULL;

    if (!html_tags || !cJSON_IsObject(html_tags)) {
        fprintf(stderr, "Config missing features.html_tags\n");
        cJSON_Delete(root);
        return 0;
    }

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, html_tags) {
        if (!cJSON_IsString(entry)) continue;
        if (!feature_registry_add(reg, entry->string, entry->valuestring)) {
            fprintf(stderr, "Warning: too many features, skipping %s\n",
                    entry->string);
        }
    }

    cJSON_Delete(root);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Default registry (used when no --config is provided)                */
/* ------------------------------------------------------------------ */
static void load_default_registry(FeatureRegistry *reg)
{
    feature_registry_add(reg, "b",      "bold");
    feature_registry_add(reg, "strong", "bold");
    feature_registry_add(reg, "i",      "italic");
    feature_registry_add(reg, "em",     "italic");
    feature_registry_add(reg, "u",      "underline");
    feature_registry_add(reg, "ins",    "underline");
    feature_registry_add(reg, "strike", "strikethrough");
    feature_registry_add(reg, "s",      "strikethrough");
    feature_registry_add(reg, "sup",    "superscript");
    feature_registry_add(reg, "sub",    "subscript");
}

/* ------------------------------------------------------------------ */
/* Output                                                               */
/* ------------------------------------------------------------------ */
#define MAX_VAL_LEN 120

static void write_nodes(const ConvertResult *r, const char *outpath,
                        double t_load, double t_tok, double t_conv,
                        double t_write_start, size_t file_bytes)
{
    FILE *f = fopen(outpath, "wb");
    if (!f) { perror("fopen output"); exit(1); }

    const TextNodeArray   *nodes = &r->nodes;
    const FeatureRegistry *reg   = &r->registry;

    fprintf(f, "# HTML Text Node Output\n");
    fprintf(f, "# File size  : %zu bytes\n", file_bytes);
    fprintf(f, "# Text nodes : %zu\n", nodes->count);
    fprintf(f, "# Load time  : %.3f ms\n", t_load * 1000.0);
    fprintf(f, "# Tok time   : %.3f ms\n", t_tok  * 1000.0);
    fprintf(f, "# Conv time  : %.3f ms\n", t_conv * 1000.0);
    fprintf(f, "#\n");
    fprintf(f, "# Feature map:\n");
    uint32_t printed = 0;
    for (int i = 0; i < reg->count; i++) {
        if (reg->entries[i].bit & printed) continue;
        fprintf(f, "#   bit 0x%08x = %s\n",
                reg->entries[i].bit, reg->entries[i].feat_name);
        printed |= reg->entries[i].bit;
    }
    fprintf(f, "#\n");
    fprintf(f, "# FORMAT: features(hex) | text (truncated to %d chars)\n",
            MAX_VAL_LEN);
    fprintf(f, "#\n");

    for (size_t i = 0; i < nodes->count; i++) {
        const TextNode *n    = &nodes->data[i];
        size_t          vlen = n->len < MAX_VAL_LEN ? n->len : MAX_VAL_LEN;

        fprintf(f, "0x%08x | ", n->features);

        for (size_t j = 0; j < vlen; j++) {
            unsigned char ch = (unsigned char)n->start[j];
            if (ch == '\n' || ch == '\r' || ch == '\t') fputc(' ', f);
            else if (ch < 32 || ch == 127)              fputc('?', f);
            else                                         fputc(ch, f);
        }
        if (n->len > MAX_VAL_LEN)
            fprintf(f, "...(+%zu)", n->len - MAX_VAL_LEN);
        fputc('\n', f);
    }

    double t_write = now_sec() - t_write_start;

    fprintf(f, "#\n");
    fprintf(f, "# Write time : %.3f ms\n", t_write * 1000.0);
    fprintf(f, "# TOTAL time : %.3f ms\n",
            (t_load + t_tok + t_conv + t_write) * 1000.0);
    fclose(f);

    printf("Text nodes   : %zu\n",    nodes->count);
    printf("Load time    : %.3f ms\n", t_load * 1000.0);
    printf("Tok time     : %.3f ms\n", t_tok  * 1000.0);
    printf("Conv time    : %.3f ms\n", t_conv * 1000.0);
    printf("Write time   : %.3f ms\n", t_write * 1000.0);
    printf("TOTAL        : %.3f ms\n",
           (t_load + t_tok + t_conv + t_write) * 1000.0);
    printf("Output       : %s\n", outpath);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: tokenize <file.html> [--config <config.json>]\n");
        return 1;
    }

    const char *html_path   = argv[1];
    const char *config_path = NULL;

    /* parse optional --config flag */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    /* 1. Load */
    double t0 = now_sec();

    FILE *f = fopen(html_path, "rb");
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

    /* 2. Tokenize */
    double t1 = now_sec();
    TokenArray ta = html_tokenize(buf, (size_t)fsize);
    double t_tok = now_sec() - t1;

    /* 3. Build registry */
    FeatureRegistry reg = {0};
    if (config_path) {
        if (!load_config(config_path, &reg)) return 1;
        printf("Config       : %s\n", config_path);
    } else {
        load_default_registry(&reg);
        printf("Config       : (default)\n");
    }

    /* 4. Convert */
    double t2 = now_sec();
    ConvertResult r = convert_tokens_to_instructions(&ta, &reg);
    double t_conv = now_sec() - t2;

    /* 5. Write */
    size_t inlen   = strlen(html_path);
    char  *outpath = (char *)malloc(inlen + 16);
    if (!outpath) { fprintf(stderr, "OOM\n"); return 1; }
    memcpy(outpath, html_path, inlen);
    memcpy(outpath + inlen, ".nodes.txt", 11);
    outpath[inlen + 11] = '\0';

    double t_write_start = now_sec();
    write_nodes(&r, outpath, t_load, t_tok, t_conv,
                t_write_start, (size_t)fsize);

    free(r.nodes.data);
    free(ta.data);
    free(buf);
    free(outpath);
    return 0;
}