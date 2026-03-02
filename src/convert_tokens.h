#ifndef CONVERT_TOKENS_H
#define CONVERT_TOKENS_H

#include "tokenizer.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Feature registry                                                     */
/* Maps a tag name to a feature name and bit position.                 */
/* Built once from user config, read-only at runtime.                  */
/* ------------------------------------------------------------------ */
#define MAX_FEATURES 32
#define MAX_TAG_NAME  16
#define MAX_FEAT_NAME 32

typedef struct {
    char     tag_name[MAX_TAG_NAME];   /* e.g. "strong"  */
    char     feat_name[MAX_FEAT_NAME]; /* e.g. "bold"    */
    uint32_t bit;                      /* e.g. 1 << 0    */
} FeatureEntry;

typedef struct {
    FeatureEntry entries[MAX_FEATURES];
    int          count;
    uint32_t     first_char_mask;  /* bit per lowercase letter: bit 0='a', bit 25='z' */
} FeatureRegistry;

/* ------------------------------------------------------------------ */
/* Text node                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *start;    /* pointer into original buffer — no copy */
    size_t      len;
    uint32_t    features; /* bitmask of active features              */
} TextNode;

typedef struct {
    TextNode *data;
    size_t    count;
    size_t    cap;
} TextNodeArray;

/* ------------------------------------------------------------------ */
/* Result                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    TextNodeArray  nodes;
    FeatureRegistry registry;
} ConvertResult;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/* Register a tag->feature mapping before calling convert.
   Call once per mapping entry. Returns 0 on failure (too many features). */
int feature_registry_add(FeatureRegistry *reg,
                         const char *tag_name,
                         const char *feat_name);

/* Walk the token array and produce annotated text nodes.
   Caller must free result.nodes.data when done. */
ConvertResult convert_tokens_to_instructions(const TokenArray *ta,
                                             const FeatureRegistry *reg);

#endif /* CONVERT_TOKENS_H */