/*
 * convert_tokens.c
 * Walks a TokenArray and produces annotated text nodes with feature bitmasks.
 *
 * Compile (GCC/Clang):
 *   gcc -O2 -c convert_tokens.c
 */

#include "convert_tokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Feature registry                                                     */
/* ------------------------------------------------------------------ */

int feature_registry_add(FeatureRegistry *reg,
                         const char *tag_name,
                         const char *feat_name)
{
    if (reg->count >= MAX_FEATURES) return 0;

    /* check if this feature name already has a bit assigned */
    uint32_t bit = 0;
    for (int i = 0; i < reg->count; i++) {
        if (strncmp(reg->entries[i].feat_name, feat_name, MAX_FEAT_NAME) == 0) {
            bit = reg->entries[i].bit;
            break;
        }
    }
    /* new feature name — assign next bit */
    if (bit == 0) {
        uint32_t used = 0;
        for (int i = 0; i < reg->count; i++)
            used |= reg->entries[i].bit;
        int pos = 0;
        while (used & (1u << pos)) pos++;
        bit = 1u << pos;
    }

    FeatureEntry *e = &reg->entries[reg->count++];
    strncpy(e->tag_name,  tag_name,  MAX_TAG_NAME  - 1); e->tag_name[MAX_TAG_NAME-1]   = '\0';
    strncpy(e->feat_name, feat_name, MAX_FEAT_NAME - 1); e->feat_name[MAX_FEAT_NAME-1] = '\0';
    e->bit = bit;

    /* update first char mask */
    char fc = tag_name[0] | 32;  /* to lowercase */
    if (fc >= 'a' && fc <= 'z')
        reg->first_char_mask |= (1u << (fc - 'a'));

    return 1;
}

/* ------------------------------------------------------------------ */
/* Internal: look up a tag name in the registry                        */
/* ------------------------------------------------------------------ */
static uint32_t registry_lookup(const FeatureRegistry *reg,
                                const char *tag_name, size_t tag_len)
{
    for (int i = 0; i < reg->count; i++) {
        size_t elen = strlen(reg->entries[i].tag_name);
        if (elen == tag_len &&
            strncasecmp(reg->entries[i].tag_name, tag_name, tag_len) == 0)
            return reg->entries[i].bit;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Internal: extract tag name length                                   */
/* ------------------------------------------------------------------ */
static size_t tag_name_len(const char *name_start, size_t max)
{
    size_t i = 0;
    while (i < max) {
        unsigned char c = (unsigned char)name_start[i];
        if (c == '>' || c == '/' || c == ' ' || c == '\t' ||
            c == '\n' || c == '\r' || c == '\0')
            break;
        i++;
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* Internal: text node array                                            */
/* ------------------------------------------------------------------ */
static void tna_init(TextNodeArray *tna)
{
    tna->cap   = 1 << 14;
    tna->count = 0;
    tna->data  = (TextNode *)malloc(tna->cap * sizeof(TextNode));
    if (!tna->data) { fprintf(stderr, "OOM\n"); exit(1); }
}

static void tna_push(TextNodeArray *tna,
                     const char *start, size_t len, uint32_t features)
{
    if (tna->count == tna->cap) {
        tna->cap *= 2;
        tna->data = (TextNode *)realloc(tna->data,
                                        tna->cap * sizeof(TextNode));
        if (!tna->data) { fprintf(stderr, "OOM\n"); exit(1); }
    }
    tna->data[tna->count].start    = start;
    tna->data[tna->count].len      = len;
    tna->data[tna->count].features = features;
    tna->count++;
}

/* ------------------------------------------------------------------ */
/* Public: convert_tokens_to_instructions                              */
/* ------------------------------------------------------------------ */
ConvertResult convert_tokens_to_instructions(const TokenArray *ta,
                                             const FeatureRegistry *reg)
{
    ConvertResult result;
    result.registry = *reg;
    tna_init(&result.nodes);

    uint32_t active = 0;

    for (size_t i = 0; i < ta->count; i++) {
        const Token *t = &ta->data[i];

        switch (t->type) {

        case TOK_OPEN_TAG: {
            if (t->len < 2) break;
            const char *name = t->start + 1;
            char fc = *name | 32;
            if (fc >= 'a' && fc <= 'z' &&
                (reg->first_char_mask & (1u << (fc - 'a')))) {
                size_t nlen = tag_name_len(name, t->len - 1);
                uint32_t bit = registry_lookup(reg, name, nlen);
                if (bit) active |= bit;
            }
            break;
        }

        case TOK_CLOSE_TAG: {
            if (t->len < 3) break;
            const char *name = t->start + 2;
            char fc = *name | 32;
            if (fc >= 'a' && fc <= 'z' &&
                (reg->first_char_mask & (1u << (fc - 'a')))) {
                size_t nlen = tag_name_len(name, t->len - 2);
                uint32_t bit = registry_lookup(reg, name, nlen);
                if (bit) active &= ~bit;
            }
            break;
        }

        case TOK_SYNTHETIC_CLOSE:
            break;

        case TOK_TEXT:
            if (t->start && t->len > 0)
                tna_push(&result.nodes, t->start, t->len, active);
            break;

        default:
            break;
        }
    }

    return result;
}